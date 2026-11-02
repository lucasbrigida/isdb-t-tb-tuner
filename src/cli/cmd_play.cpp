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
#include <rtl-sdr.h>

#ifdef _WIN32
    #include <windows.h>
#endif

namespace isdbt {

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

int cmd_play(int channel_number, const std::string& name_query, uint16_t udp_port,
             int dev_idx, bool test_pattern, double gain_db, bool auto_player) {
#ifdef _WIN32
    // Eleva a prioridade do processo para proteger o fluxo de DSP contra picos de outros aplicativos
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
#endif

    ChannelManager mgr;
    const ChannelInfo* target = nullptr;

    if (channel_number > 0) {
        target = mgr.find_by_number(channel_number);
    } else if (!name_query.empty()) {
        target = mgr.find_by_name(name_query);
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
        std::cerr << "\033[1;31m[ERRO] Canal nao especificado. Use --channel <num> ou --name <nome>\033[0m\n";
        return 1;
    }

    std::cout << "\033[1;36m========================================================\033[0m\n";
    std::cout << "\033[1;36m  SINTONIZANDO: " << ch_name << " (" << (freq_hz / 1e6) << " MHz)\033[0m\n";
    std::cout << "\033[1;36m========================================================\033[0m\n";

    UdpStreamer streamer("127.0.0.1", udp_port);
    if (!streamer.start()) {
        return 1;
    }

    // Envia bloco de teste apenas se explicitamente solicitado (--test-pattern)
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
    std::cout << "  1. \033[1;32mMPV Player com Interpolacao em Tempo Real (Recomendado):\033[0m\n";
    std::cout << "     Execute: \033[1;33m.\\scripts\\play_mpv.bat\033[0m (ou .\\scripts\\play_mpv.ps1)\n";
    std::cout << "  2. \033[1;36mVLC Media Player:\033[0m\n";
    std::cout << "     URL: \033[1;32m" << streamer.url() << "\033[0m | Cache: \033[1;33m--network-caching=2000 --clock-jitter=1000\033[0m\n";
    if (auto_player) {
        launch_mpv(udp_port);
    }
    std::cout << "  \033[1;35m[DSP Engine]\033[0m Pipeline: Fila SPSC Lock-Free (64 slots) | Linhas de Atraso Estaticas | GF Fast O(1)\n";
    std::cout << "Pressione \033[1;33mCtrl+C\033[0m para encerrar a transmissao.\n\n";

    rtlsdr_dev_t* dev = nullptr;
    if (rtlsdr_open(&dev, static_cast<uint32_t>(dev_idx)) != 0) {
        std::cerr << "\033[1;31m[ERRO] Falha ao abrir dongle RTL-SDR #" << dev_idx << "\033[0m\n";
        return 1;
    }

    rtlsdr_reset_buffer(dev);
    constexpr uint32_t sample_rate = constants::SAMPLE_RATE;
    rtlsdr_set_sample_rate(dev, sample_rate);
    rtlsdr_set_tuner_gain_mode(dev, 1); // Ganho manual otimizado
    int gain_tenths = static_cast<int>(std::round(gain_db * 10.0));
    rtlsdr_set_tuner_gain(dev, gain_tenths);
    rtlsdr_set_agc_mode(dev, 0);
    safe_set_freq(dev, freq_hz);

    constexpr size_t FFT_LEN = 2048;
    // O intervalo de guarda (1/4, 1/8, 1/16, 1/32) e detectado automaticamente pelo OfdmSync.
    size_t GI_LEN  = 128;
    size_t SYM_LEN = FFT_LEN + GI_LEN;
    float frac_pos = 0.0f;           // posicao fracionaria do proximo simbolo (rastreio de deriva de clock)
    auto last_pat_time = std::chrono::steady_clock::now();

    OfdmSync sync(OfdmMode::MODE_1_2K, GuardInterval::GI_1_16, static_cast<float>(sample_rate));
    OneSegDemodulator demod(OfdmMode::MODE_1_2K, GuardInterval::GI_1_16);
    PsiParser psi;
    TsGenerator channel_ts_gen;

    const size_t chunk_samples = 32768;
    const size_t chunk_bytes = chunk_samples * 2;
    auto raw_queue = std::make_unique<SpscCaptureQueue<64>>();
    std::atomic<bool> reader_running{true};
    DspMetrics dsp_metrics{};

    std::thread reader_thread([&]() {
#ifdef _WIN32
        // Eleva a prioridade da thread USB para evitar buffer overflows e perda de amostras
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
        std::uint64_t sequence = 0;
        bool pending_discontinuity = false;

        while (reader_running.load(std::memory_order_relaxed) && g_running) {
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

    std::vector<std::complex<float>> iq_buf(chunk_samples);
    IqStreamBuffer stream_iq_buf(chunk_samples * 2);

    bool ofdm_locked = false;
    float tracked_ffo_hz = 0.0f;
    float last_peak_corr = 0.0f;
    int unlock_counter = 0;

    uint64_t total_air_packets = 0;
    uint64_t total_standby_packets = 0;
    auto last_time = std::chrono::steady_clock::now();
    bool channel_name_learned = false;

    std::uint64_t expected_sequence = 0;
    bool have_expected_sequence = false;

    while (g_running) {
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
                    if (unlock_counter > 200) { // ~3.2 segundos de perda real de sinal
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
                    // Em recepcao parcial 1-Seg o PMT vem no PID 0x1FC8 sem PAT correspondente:
                    // injeta uma PAT sintetica (~10x/s) apontando para ele, para o VLC/MPV achar o programa
                    auto now_pat = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(now_pat - last_pat_time).count() >= 100) {
                        uint8_t pat_pkt[188];
                        if (psi.build_pat_packet(pat_pkt)) {
                            streamer.send_ts_packet(pat_pkt);
                        }
                        last_pat_time = now_pat;
                    }
                    // Envia todos os pacotes MPEG-TS reais decodificados do ar para o player (em blocos de 7x188 = 1316B)
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
            }

            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count() >= 300) {
                std::string s_name = psi.get_primary_service_name();
                if (!channel_name_learned && !s_name.empty() && active_channel_num > 0) {
                    // Aprendizado dinamico: persiste o nome descoberto em channels.json caso fosse generico ou desconhecido
                    ChannelInfo updated_info;
                    updated_info.channel_number = active_channel_num;
                    updated_info.frequency_hz = freq_hz;
                    updated_info.rssi_dbfs = rssi_dbfs;
                    updated_info.name = s_name;
                    if (!psi.get_services().empty()) {
                        updated_info.service_id = psi.get_services().begin()->first;
                    }
                    mgr.add_or_update(updated_info);
                    mgr.save();
                    channel_name_learned = true;
                }

                std::stringstream status_ss;
                status_ss << "[Status] RSSI: " << std::fixed << std::setprecision(1) << rssi_dbfs << " dBFS | "
                          << "OFDM: " << (ofdm_locked ? "LOCK" : "SYNC") << " (" << std::setprecision(2) << last_peak_corr << ") | "
                          << "TMCC: " << (demod.is_tmcc_locked() ? "LOCK" : "SYNC") << " | "
                          << "SNR: " << std::fixed << std::setprecision(1) << demod.average_snr_db() << " dB | "
                          << "TS: " << total_air_packets << " pkts (RS: " << demod.corrected_rs_packets() << " corr / " << demod.uncorrectable_rs_packets() << " drop)";
                status_ss << " | Drop: " << raw_queue->dropped_blocks();
                if (!s_name.empty()) {
                    status_ss << " | " << s_name;
                }
                std::cout << "\r" << status_ss.str() << "\033[K" << std::flush;
                last_time = now;
            }
        }

        raw_queue->release_read(view);
    }

    reader_running.store(false, std::memory_order_release);
    g_running = false;
    if (reader_thread.joinable()) {
        reader_thread.join();
    }

    std::cout << "\n\n[Encerrando] Fechando player, dongle RTL-SDR e encerrando stream UDP...\n";
    stop_player();
    rtlsdr_reset_buffer(dev);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    rtlsdr_close(dev);
    streamer.stop();
    return 0;
}

} // namespace isdbt

