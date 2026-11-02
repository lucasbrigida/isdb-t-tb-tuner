#include "cli/commands.hpp"
#include "cli/channel_manager.hpp"
#include "radio/frequency_plan.hpp"
#include "radio/rtlsdr_device.hpp"
#include "platform/platform_utils.hpp"
#include "dsp/constants.hpp"
#include "dsp/dsp_metrics.hpp"
#include "dsp/spsc_capture_queue.hpp"
#include "dsp/iq_stream_buffer.hpp"
#include "dsp/ofdm_sync.hpp"
#include "dsp/oneseg_demodulator.hpp"
#include "streaming/udp_streamer.hpp"
#include "mpegts/psi_parser.hpp"
#include "mpegts/ts_generator.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <complex>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <mutex>
#include <rtl-sdr.h>

#ifdef _WIN32
    #include <windows.h>
#endif

namespace isdbt {

struct PlaybackStatusSnapshot {
    float rssi_dbfs = -99.0f;
    float peak_corr = 0.0f;
    bool ofdm_locked = false;
    bool tmcc_locked = false;
    float snr_db = 0.0f;
    uint64_t air_packets = 0;
    uint32_t rs_corrected = 0;
    uint32_t rs_dropped = 0;
    uint64_t drop_blocks = 0;
    std::string service_name;
};

struct SharedPlaybackStatus {
    std::mutex mtx;
    PlaybackStatusSnapshot snap;
};

static void reset_play_reception_stream(
    OfdmSync& sync,
    OneSegDemodulator& demod,
    IqStreamBuffer& stream_iq_buf,
    bool& ofdm_locked,
    float& tracked_ffo_hz,
    float& frac_pos,
    std::size_t& gi_len,
    std::size_t& sym_len,
    int& unlock_counter,
    DspMetrics* metrics = nullptr) {

    stream_iq_buf.clear();
    frac_pos = 0.0f;
    ofdm_locked = false;
    tracked_ffo_hz = 0.0f;
    unlock_counter = 0;
    gi_len = 0;
    sym_len = 0;

    sync.reset();
    demod.reset();

    if (metrics) {
        metrics->decoder_resets++;
    }
}

static int get_prev_channel(const std::vector<ChannelInfo>& list, int current_ch) {
    if (list.empty()) {
        int prev = current_ch - 1;
        return (prev >= 14) ? prev : 69;
    }
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].channel_number == current_ch) {
            return (i > 0) ? list[i - 1].channel_number : list.back().channel_number;
        }
    }
    return list.front().channel_number;
}

static int get_next_channel(const std::vector<ChannelInfo>& list, int current_ch) {
    if (list.empty()) {
        int next = current_ch + 1;
        return (next <= 69) ? next : 14;
    }
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].channel_number == current_ch) {
            return (i + 1 < list.size()) ? list[i + 1].channel_number : list.front().channel_number;
        }
    }
    return list.front().channel_number;
}

static int show_interactive_channel_menu(const std::vector<ChannelInfo>& list, int current_ch) {
    std::cout << "\n\n========================================================\n";
    std::cout << "                 MENU DE CANAIS SALVOS                  \n";
    std::cout << "========================================================\n";
    if (list.empty()) {
        std::cout << "Nenhum canal salvo na lista. Pressione qualquer tecla para voltar.\n";
        console_getch();
        return -1;
    }
    for (size_t i = 0; i < list.size(); ++i) {
        const auto& ch = list[i];
        bool is_curr = (ch.channel_number == current_ch);
        std::stringstream freq_ss;
        freq_ss << std::fixed << std::setprecision(1) << (ch.frequency_hz / 1e6) << " MHz";
        std::cout << (is_curr ? " -> " : "    ")
                  << "[" << std::setw(2) << ch.channel_number << "] "
                  << pad_string(ch.name, 22) << " ("
                  << freq_ss.str() << ")\n";
    }
    std::cout << "--------------------------------------------------------\n";
    std::cout << "Digite o numero do canal desejado (ou Enter para cancelar): " << std::flush;

    std::string input;
    while (g_running) {
        int c = console_getch();
        if (c == '\r' || c == '\n') {
            std::cout << "\n";
            break;
        } else if (c == 8 || c == 127) { // Backspace
            if (!input.empty()) {
                input.pop_back();
                std::cout << "\b \b" << std::flush;
            }
        } else if (c >= '0' && c <= '9') {
            input.push_back(static_cast<char>(c));
            std::cout << static_cast<char>(c) << std::flush;
        }
    }
    if (input.empty()) return -1;
    return std::atoi(input.c_str());
}

int cmd_play(int channel_number, const std::string& name_query, uint16_t udp_port,
             int dev_idx, bool test_pattern, double gain_db, bool auto_player) {
    print_cli_header();

#ifdef _WIN32
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
#endif

    ChannelManager mgr;
    const auto& saved_channels = mgr.get_channels();
    const ChannelInfo* target = nullptr;

    if (channel_number > 0) {
        target = mgr.find_by_number(channel_number);
    } else if (!name_query.empty()) {
        target = mgr.find_by_name(name_query);
    } else if (!saved_channels.empty()) {
        target = &saved_channels.front();
    }

    uint32_t freq_hz = 0;
    std::string ch_name = "Canal Desconhecido";
    int active_channel_num = channel_number;

    if (target) {
        freq_hz = target->frequency_hz;
        ch_name = target->name;
        active_channel_num = target->channel_number;
    } else if (channel_number > 0) {
        freq_hz = get_channel_freq(channel_number);
        ch_name = "Canal " + std::to_string(channel_number);
    } else {
        std::cerr << "[ERRO] Nenhum canal especificado ou salvo. Use --channel <num> ou execute 'scan'.\n";
        return 1;
    }

    std::cout << "========================================================\n";
    std::cout << "  SINTONIZANDO: " << ch_name << " (" << (freq_hz / 1e6) << " MHz)\n";
    std::cout << "========================================================\n";

    UdpStreamer streamer("127.0.0.1", udp_port);
    if (!streamer.start()) {
        return 1;
    }

    if (test_pattern) {
        TsGenerator ts_gen;
        std::vector<std::vector<uint8_t>> standby_packets;
        ts_gen.get_standby_block(standby_packets);
        for (auto &pkt : standby_packets) {
            streamer.send_ts_packet(pkt.data());
        }
        streamer.flush();
    }

    std::cout << "Reprodutores de Video Disponiveis:\n";
    std::cout << "  1. MPV Player com Interpolacao em Tempo Real (Recomendado):\n";
    std::cout << "     Execute: .\\scripts\\play_mpv.bat (ou .\\scripts\\play_mpv.ps1)\n";
    std::cout << "  2. VLC Media Player:\n";
    std::cout << "     URL: " << streamer.url() << " | Cache: --network-caching=2000 --clock-jitter=1000\n";
    if (auto_player) {
        launch_mpv(udp_port);
    }
    std::cout << "  [DSP Engine] Pipeline: Fila SPSC Lock-Free (64 slots) | Linhas de Atraso Estaticas | GF Fast O(1)\n\n";

    std::cout << "Comandos interativos de navegacao:\n";
    std::cout << "  [Seta Cima / k] Canal Anterior    [Seta Baixo / j] Proximo Canal\n";
    std::cout << "  [m] Menu de Selecao de Canais     [q / Ctrl+C] Sair\n\n";

    rtlsdr_dev_t* dev = nullptr;
    if (rtlsdr_open(&dev, static_cast<uint32_t>(dev_idx)) != 0) {
        std::cerr << "[ERRO] Falha ao abrir dongle RTL-SDR #" << dev_idx << "\n";
        return 1;
    }

    rtlsdr_reset_buffer(dev);
    constexpr uint32_t sample_rate = constants::SAMPLE_RATE;
    rtlsdr_set_sample_rate(dev, sample_rate);
    rtlsdr_set_tuner_gain_mode(dev, 1);
    int gain_tenths = static_cast<int>(std::round(gain_db * 10.0));
    rtlsdr_set_tuner_gain(dev, gain_tenths);
    rtlsdr_set_agc_mode(dev, 0);
    safe_set_freq(dev, freq_hz);

    const size_t chunk_samples = 32768;
    const size_t chunk_bytes = chunk_samples * 2;
    auto raw_queue = std::make_unique<SpscCaptureQueue<64>>();

    std::atomic<bool> reader_running{true};
    std::atomic<bool> dsp_running{true};
    std::atomic<uint32_t> retune_target_freq_hz{0};
    std::atomic<bool> retune_in_progress{false};
    std::atomic<bool> dsp_reset_requested{false};
    std::atomic<bool> dsp_reset_done{false};

    SharedPlaybackStatus shared_status;

    // THREAD 1: Captura USB continua e retune seguro sem colisao
    std::thread reader_thread([&]() {
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
        std::uint64_t sequence = 0;
        bool pending_discontinuity = false;

        while (reader_running.load(std::memory_order_relaxed) && g_running) {
            uint32_t req_freq = retune_target_freq_hz.load(std::memory_order_acquire);
            if (req_freq != 0) {
                safe_set_freq(dev, req_freq);
                retune_target_freq_hz.store(0, std::memory_order_release);
                pending_discontinuity = true;
                retune_in_progress.store(false, std::memory_order_release);
            }

            SpscCaptureQueue<64>::WriteView view;
            if (!raw_queue->try_acquire_write(view)) {
                raw_queue->record_drop();
                ++sequence;
                pending_discontinuity = true;
                std::this_thread::sleep_for(std::chrono::microseconds(500));
                continue;
            }

            auto* block = view.block;
            block->sequence = sequence++;
            block->size = 0;
            block->discontinuity_before = pending_discontinuity;

            int n_read = 0;
            int r = rtlsdr_read_sync(dev, block->raw.data(), static_cast<int>(chunk_bytes), &n_read);
            if (r < 0 || n_read != static_cast<int>(chunk_bytes)) {
                block->discontinuity_before = true;
                block->size = 0;
                pending_discontinuity = true;
                raw_queue->publish_write(view);
                continue;
            }

            block->size = static_cast<size_t>(n_read);
            raw_queue->publish_write(view);
            pending_discontinuity = false;
        }
    });

    // THREAD 2: Worker de DSP e Streaming UDP dedicado (continua rodando mesmo com menu aberto!)
    std::thread dsp_worker_thread([&]() {
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif
        constexpr size_t FFT_LEN = 2048;
        size_t GI_LEN  = 128;
        size_t SYM_LEN = FFT_LEN + GI_LEN;
        float frac_pos = 0.0f;
        auto last_pat_time = std::chrono::steady_clock::now();
        auto last_null_burst = std::chrono::steady_clock::now();

        OfdmSync sync(OfdmMode::MODE_1_2K, GuardInterval::GI_1_16, static_cast<float>(sample_rate));
        OneSegDemodulator demod(OfdmMode::MODE_1_2K, GuardInterval::GI_1_16);
        PsiParser psi;
        TsGenerator channel_ts_gen;

        std::vector<std::complex<float>> iq_buf(chunk_samples);
        IqStreamBuffer stream_iq_buf(chunk_samples * 2);

        bool ofdm_locked = false;
        float tracked_ffo_hz = 0.0f;
        float last_peak_corr = 0.0f;
        int unlock_counter = 0;
        uint64_t total_air_packets = 0;
        uint64_t total_standby_packets = 0;

        std::uint64_t expected_sequence = 0;
        bool have_expected_sequence = false;
        DspMetrics dsp_metrics{};
        auto last_status_update = std::chrono::steady_clock::now();

        while (dsp_running.load(std::memory_order_relaxed) && g_running) {
            // Verifica solicitacao de reset de stream (troca de canal)
            if (dsp_reset_requested.load(std::memory_order_acquire)) {
                streamer.send_discontinuity_burst();

                // Drena amostras pendentes da fila SPSC
                SpscCaptureQueue<64>::ReadView drain_view;
                while (raw_queue->try_acquire_read(drain_view)) {
                    raw_queue->release_read(drain_view);
                }

                psi.reset();
                reset_play_reception_stream(sync, demod, stream_iq_buf, ofdm_locked, tracked_ffo_hz,
                                            frac_pos, GI_LEN, SYM_LEN, unlock_counter, &dsp_metrics);
                total_air_packets = 0;
                have_expected_sequence = false;
                last_pat_time = std::chrono::steady_clock::now();

                {
                    std::lock_guard<std::mutex> lock(shared_status.mtx);
                    shared_status.snap.air_packets = 0;
                    shared_status.snap.service_name.clear();
                    shared_status.snap.ofdm_locked = false;
                    shared_status.snap.tmcc_locked = false;
                }

                dsp_reset_requested.store(false, std::memory_order_release);
                dsp_reset_done.store(true, std::memory_order_release);
            }

            SpscCaptureQueue<64>::ReadView view;
            if (!raw_queue->try_acquire_read(view)) {
                std::this_thread::sleep_for(std::chrono::microseconds(200));
                continue;
            }

            const auto& block = *view.block;
            bool discontinuity = block.discontinuity_before;
            if (have_expected_sequence && block.sequence != expected_sequence) {
                discontinuity = true;
                dsp_metrics.sequence_gaps++;
            }

            expected_sequence = block.sequence + 1;
            have_expected_sequence = true;
            dsp_metrics.input_blocks++;

            if (discontinuity) {
                reset_play_reception_stream(sync, demod, stream_iq_buf,
                                            ofdm_locked, tracked_ffo_hz, frac_pos,
                                            GI_LEN, SYM_LEN, unlock_counter, &dsp_metrics);
            }

            if (block.size == chunk_bytes) {
                const uint8_t* raw_data = block.raw.data();
                float pwr_sum = 0.0f;
                for (size_t i = 0; i < chunk_samples; ++i) {
                    float iv = (static_cast<float>(raw_data[2 * i + 0]) - 127.5f) / 127.5f;
                    float qv = (static_cast<float>(raw_data[2 * i + 1]) - 127.5f) / 127.5f;
                    iq_buf[i] = std::complex<float>(iv, qv);
                    pwr_sum += (iv * iv + qv * qv);
                }
                float rssi_dbfs = 10.0f * std::log10((pwr_sum / chunk_samples) + 1e-12f);

                auto sync_res = sync.find_symbol_boundary(iq_buf.data(), chunk_samples);
                last_peak_corr = sync_res.peak_correlation;
                std::vector<std::vector<uint8_t>> decoded_pkts;

                if (!ofdm_locked) {
                    if (sync_res.locked) {
                        ofdm_locked = true;
                        unlock_counter = 0;
                        GI_LEN = static_cast<size_t>(sync.gi_size());
                        SYM_LEN = FFT_LEN + GI_LEN;
                        frac_pos = 0.0f;
                        tracked_ffo_hz = sync_res.ffo_hz;
                        sync.reset_phase();
                        sync.correct_frequency_offset(iq_buf.data(), iq_buf.size(), tracked_ffo_hz);

                        size_t gi_start = (sync_res.symbol_offset >= static_cast<int>(GI_LEN))
                                          ? static_cast<size_t>(sync_res.symbol_offset - static_cast<int>(GI_LEN))
                                          : 0;
                        stream_iq_buf.assign(iq_buf.data() + gi_start, iq_buf.size() - gi_start);
                    }
                } else {
                    if (!sync_res.locked) {
                        unlock_counter++;
                        if (unlock_counter > 200) {
                            reset_play_reception_stream(sync, demod, stream_iq_buf,
                                                        ofdm_locked, tracked_ffo_hz, frac_pos,
                                                        GI_LEN, SYM_LEN, unlock_counter, &dsp_metrics);
                        }
                    } else {
                        unlock_counter = 0;
                        const float spacing = static_cast<float>(sample_rate) / static_cast<float>(FFT_LEN);
                        float residual = sync_res.ffo_hz - tracked_ffo_hz;
                        residual -= spacing * std::round(residual / spacing);
                        tracked_ffo_hz += 0.1f * residual;
                    }

                    if (ofdm_locked) {
                        sync.correct_frequency_offset(iq_buf.data(), iq_buf.size(), tracked_ffo_hz);
                        stream_iq_buf.append(iq_buf.data(), iq_buf.size());
                    }
                }

                if (ofdm_locked) {
                    while (static_cast<size_t>(std::lround(frac_pos)) + SYM_LEN + 8 <= stream_iq_buf.available()) {
                        int base_pos = static_cast<int>(std::lround(frac_pos));
                        int max_neg = (base_pos >= 4) ? 4 : base_pos;
                        int best_sh = 0;
                        float best_mag = -1.0f;
                        for (int sh = -max_neg; sh <= 4; ++sh) {
                            std::complex<float> g(0.0f, 0.0f);
                            for (size_t m = 0; m < GI_LEN; ++m) {
                                g += stream_iq_buf[base_pos + sh + m] * std::conj(stream_iq_buf[base_pos + sh + m + FFT_LEN]);
                            }
                            float mag = std::norm(g);
                            if (mag > best_mag) { best_mag = mag; best_sh = sh; }
                        }
                        frac_pos += 0.25f * static_cast<float>(best_sh);
                        int sym_d = std::max(0, static_cast<int>(std::lround(frac_pos)));
                        if (static_cast<size_t>(sym_d) + SYM_LEN > stream_iq_buf.available()) break;

                        demod.process_symbol(stream_iq_buf.data_at(sym_d + GI_LEN), decoded_pkts);
                        frac_pos += static_cast<float>(SYM_LEN);
                        dsp_metrics.processed_symbols++;
                    }

                    int consume_i = static_cast<int>(std::floor(frac_pos)) - 8;
                    if (consume_i > 0 && static_cast<size_t>(consume_i) <= stream_iq_buf.available()) {
                        stream_iq_buf.consume(static_cast<size_t>(consume_i));
                        frac_pos -= static_cast<float>(consume_i);
                    }

                    for (const auto& pkt : decoded_pkts) {
                        psi.parse_packet(pkt.data());
                    }

                    if (!decoded_pkts.empty()) {
                        auto now_pat = std::chrono::steady_clock::now();
                        if (std::chrono::duration_cast<std::chrono::milliseconds>(now_pat - last_pat_time).count() >= 100) {
                            if (psi.has_pmt()) {
                                uint8_t pat_pkt[188];
                                if (psi.build_pat_packet(pat_pkt)) {
                                    streamer.send_ts_packet(pat_pkt);
                                }
                            }
                            last_pat_time = now_pat;
                        }
                        for (const auto& pkt : decoded_pkts) {
                            streamer.send_ts_packet(pkt.data());
                            total_air_packets++;
                            dsp_metrics.valid_ts_packets++;
                        }
                        dsp_metrics.average_evm = demod.average_evm();
                        dsp_metrics.average_snr_db = demod.average_snr_db();
                        dsp_metrics.corrected_rs_packets = demod.corrected_rs_packets();
                        dsp_metrics.uncorrectable_rs_packets = demod.uncorrectable_rs_packets();
                    } else if (test_pattern) {
                        std::vector<std::vector<uint8_t>> standby_pkts;
                        channel_ts_gen.get_standby_block(standby_pkts);
                        for (const auto& pkt : standby_pkts) {
                            streamer.send_ts_packet(pkt.data());
                            total_standby_packets++;
                        }
                        streamer.flush();
                    } else {
                        // Envia pacotes nulos (PID 0x1FFF) a cada 50ms para manter o socket UDP alimentado (keep-alive)
                        auto now_keepalive = std::chrono::steady_clock::now();
                        if (std::chrono::duration_cast<std::chrono::milliseconds>(now_keepalive - last_null_burst).count() >= 50) {
                            streamer.send_null_burst(7);
                            last_null_burst = now_keepalive;
                        }
                    }

                    streamer.flush_if_stale(80);
                } else if (test_pattern) {
                    std::vector<std::vector<uint8_t>> standby_pkts;
                    channel_ts_gen.get_standby_block(standby_pkts);
                    for (const auto& pkt : standby_pkts) {
                        streamer.send_ts_packet(pkt.data());
                        total_standby_packets++;
                    }
                    streamer.flush();
                } else {
                    auto now_keepalive = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(now_keepalive - last_null_burst).count() >= 50) {
                        streamer.send_null_burst(7);
                        last_null_burst = now_keepalive;
                    }
                }

                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_status_update).count() >= 200) {
                    std::string s_name = psi.get_primary_service_name();
                    {
                        std::lock_guard<std::mutex> lock(shared_status.mtx);
                        shared_status.snap.rssi_dbfs = rssi_dbfs;
                        shared_status.snap.peak_corr = last_peak_corr;
                        shared_status.snap.ofdm_locked = ofdm_locked;
                        shared_status.snap.tmcc_locked = demod.is_tmcc_locked();
                        shared_status.snap.snr_db = demod.average_snr_db();
                        shared_status.snap.air_packets = total_air_packets;
                        shared_status.snap.rs_corrected = demod.corrected_rs_packets();
                        shared_status.snap.rs_dropped = demod.uncorrectable_rs_packets();
                        shared_status.snap.drop_blocks = raw_queue->dropped_blocks();
                        if (!s_name.empty()) {
                            shared_status.snap.service_name = s_name;
                        }
                    }
                    last_status_update = now;
                }
            }

            raw_queue->release_read(view);
        }
    });

    // THREAD 3 (Main): Gerenciamento de CLI, menu e teclado interativo
    auto last_render_time = std::chrono::steady_clock::now();
    bool channel_name_learned = false;

    while (g_running) {
        if (console_kbhit()) {
            int key = console_getch();
            int new_channel = -1;

            if (key == 'q' || key == 'Q') {
                g_running = false;
                break;
            } else if (key == 'k' || key == 'K') {
                new_channel = get_prev_channel(mgr.get_channels(), active_channel_num);
            } else if (key == 'j' || key == 'J') {
                new_channel = get_next_channel(mgr.get_channels(), active_channel_num);
            } else if (key == 0 || key == 224) {
                int ext = console_getch();
                if (ext == 72) { // Seta para Cima
                    new_channel = get_prev_channel(mgr.get_channels(), active_channel_num);
                } else if (ext == 80) { // Seta para Baixo
                    new_channel = get_next_channel(mgr.get_channels(), active_channel_num);
                }
            } else if (key == 'm' || key == 'M') {
                // Abre o menu; enquanto o usuario escolhe, a dsp_worker_thread CONTINUA TRANSMITINDO!
                new_channel = show_interactive_channel_menu(mgr.get_channels(), active_channel_num);
            }

            if (new_channel > 0 && new_channel != active_channel_num && is_valid_channel(new_channel)) {
                active_channel_num = new_channel;
                freq_hz = get_channel_freq(active_channel_num);
                const auto* info = mgr.find_by_number(active_channel_num);
                ch_name = (info != nullptr) ? info->name : ("Canal " + std::to_string(active_channel_num));

                std::stringstream freq_ss;
                freq_ss << std::fixed << std::setprecision(3) << (freq_hz / 1e6) << " MHz";
                std::cout << "\n\n>>> Sintonizando: Canal " << active_channel_num << " (" << ch_name
                          << " - " << freq_ss.str() << ")...\n\n";

                // 1. Envia burst de descontinuidade MPEG-TS para players externos (VLC/MPV)
                streamer.send_discontinuity_burst();

                // 2. Solicita retune a reader_thread
                retune_in_progress.store(true, std::memory_order_release);
                retune_target_freq_hz.store(freq_hz, std::memory_order_release);
                while (retune_in_progress.load(std::memory_order_acquire) && g_running) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }

                // 3. Solicita reset ao pipeline de DSP
                dsp_reset_done.store(false, std::memory_order_release);
                dsp_reset_requested.store(true, std::memory_order_release);
                while (!dsp_reset_done.load(std::memory_order_acquire) && g_running) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }

                // 4. Se o MPV player automatico estiver ativo, notifica recarga via IPC sem fechar janela
                if (auto_player) {
                    reload_mpv_player(udp_port);
                }

                channel_name_learned = false;
                last_render_time = std::chrono::steady_clock::now();
            }
        }

        // Renderiza linha de status formatada e limpa
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_render_time).count() >= 300) {
            PlaybackStatusSnapshot snap;
            {
                std::lock_guard<std::mutex> lock(shared_status.mtx);
                snap = shared_status.snap;
            }

            if (!channel_name_learned && !snap.service_name.empty() && active_channel_num > 0) {
                ChannelInfo updated_info;
                updated_info.channel_number = active_channel_num;
                updated_info.frequency_hz = freq_hz;
                updated_info.rssi_dbfs = snap.rssi_dbfs;
                updated_info.name = snap.service_name;
                mgr.add_or_update(updated_info);
                mgr.save();
                channel_name_learned = true;
                ch_name = snap.service_name;
            }

            std::stringstream status_ss;
            status_ss << "[Status] Ch " << active_channel_num << " | RSSI: " << std::fixed << std::setprecision(1) << snap.rssi_dbfs << " dBFS | "
                      << "OFDM: " << (snap.ofdm_locked ? "LOCK" : "SYNC") << " (" << std::setprecision(2) << snap.peak_corr << ") | "
                      << "TMCC: " << (snap.tmcc_locked ? "LOCK" : "SYNC") << " | "
                      << "SNR: " << std::fixed << std::setprecision(1) << snap.snr_db << " dB | "
                      << "TS: " << snap.air_packets << " pkts (RS: " << snap.rs_corrected << " corr / " << snap.rs_dropped << " drop)"
                      << " | Drop: " << snap.drop_blocks;

            std::string display_name = !snap.service_name.empty() ? snap.service_name : ch_name;
            if (!display_name.empty()) {
                status_ss << " | " << display_name;
            }

            std::string status_line = status_ss.str();
            if (status_line.length() < 100) {
                status_line += std::string(100 - status_line.length(), ' ');
            }
            std::cout << "\r" << status_line << "\r" << std::flush;
            last_render_time = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // Sequencia limpa de encerramento
    std::cout << "\n\n[Encerrando] Fechando player, dongle RTL-SDR e encerrando stream UDP...\n";
    reader_running.store(false, std::memory_order_release);
    dsp_running.store(false, std::memory_order_release);
    g_running = false;

    if (dsp_worker_thread.joinable()) {
        dsp_worker_thread.join();
    }
    if (reader_thread.joinable()) {
        reader_thread.join();
    }

    stop_player();
    rtlsdr_reset_buffer(dev);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    rtlsdr_close(dev);
    streamer.stop();
    return 0;
}

} // namespace isdbt
