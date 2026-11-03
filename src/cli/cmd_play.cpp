#include "sync/sync_factory.h"
#include <fstream>
#include "cli/channel_manager.hpp"
#include "radio/frequency_plan.hpp"
#include "radio/rtlsdr_device.hpp"
#include "platform/platform_utils.hpp"
#include "dsp/constants.hpp"
#include "dsp/dsp_metrics.hpp"
#include "dsp/spsc_capture_queue.hpp"
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
    IOFDMSynchronizer& sync,
    OneSegDemodulator& demod,
    bool& ofdm_locked,
    DspMetrics* metrics = nullptr) {

    ofdm_locked = false;
    sync.reset();
    demod.reset();

    if (metrics) {
        metrics->decoder_resets++;
    }
}

static void print_players_box(const std::string& vlc_url) {
    const int inner_width = 84;
    std::string h_border = "+" + std::string(inner_width + 2, '-') + "+";

    auto print_line = [inner_width](const std::string& text) {
        std::cout << "| " << text;
        if (static_cast<int>(text.length()) < inner_width) {
            std::cout << std::string(inner_width - text.length(), ' ');
        }
        std::cout << " |\n";
    };

    std::cout << h_border << "\n";
    print_line("Reprodutores de Video Disponiveis:");
    print_line("  1. MPV Player com Interpolacao em Tempo Real (Recomendado):");
    print_line("     Execute: .\\scripts\\play_mpv.bat (ou .\\scripts\\play_mpv.ps1)");
    print_line("  2. VLC Media Player:");
    std::string vlc_line = "     URL: " + vlc_url + " | Cache: --network-caching=2000 --clock-jitter=1000";
    print_line(vlc_line);
    std::cout << h_border << "\n\n";
}

static std::string format_channel_line(const ChannelInfo& ch, bool is_selected, bool is_active) {
    std::stringstream ss;
    ss << (is_selected ? "  -> " : "     ")
       << "[" << std::setw(2) << ch.channel_number << "] "
       << pad_string(ch.name, 22) << " ("
       << std::fixed << std::setprecision(3) << (ch.frequency_hz / 1e6) << " MHz)";
    if (is_active) {
        ss << "  \033[1;32m[AO VIVO]\033[0m";
    }
    return ss.str();
}

int cmd_play(int channel_number, const std::string& name_query, uint16_t udp_port,
             int dev_idx, bool test_pattern, double gain_db, bool auto_player, bool verbose,
             int ofdm_mode, const std::string& input_file, const std::string& output_ts_file, bool headless) {
#ifdef _WIN32
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
#endif

    std::ofstream file_ts_out;
    if (!output_ts_file.empty()) {
        file_ts_out.open(output_ts_file, std::ios::binary);
    }

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

    rtlsdr_dev_t* dev = nullptr;
    constexpr uint32_t sample_rate = constants::SAMPLE_RATE;
    SyncParams sync_params;
    sync_params.sample_rate = static_cast<float>(sample_rate);
    auto sync = make_synchronizer(
        ofdm_mode == 2 ? OFDMAlgorithm::RobustGRISDBT : OFDMAlgorithm::VanDeBeek,
        sync_params);
    if (!sync) {
        return 1;
    }

    if (!input_file.empty()) {
        ch_name = "Arquivo IQ";
        active_channel_num = (channel_number > 0) ? channel_number : 18;
    } else {
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

        // 1. Inicializa o hardware RTL-SDR ANTES de desenhar a interface
        if (rtlsdr_open(&dev, static_cast<uint32_t>(dev_idx)) != 0) {
            std::cerr << "[ERRO] Falha ao abrir dongle RTL-SDR #" << dev_idx << "\n";
            return 1;
        }

        rtlsdr_reset_buffer(dev);
        rtlsdr_set_sample_rate(dev, sample_rate);
        rtlsdr_set_tuner_gain_mode(dev, 1);
        int gain_tenths = static_cast<int>(std::round(gain_db * 10.0));
        rtlsdr_set_tuner_gain(dev, gain_tenths);
        rtlsdr_set_agc_mode(dev, 0);
        safe_set_freq(dev, freq_hz);
    }

    // 2. Inicializa o streamer UDP
    UdpStreamer streamer("127.0.0.1", udp_port);
    if (!streamer.start()) {
        if (dev) rtlsdr_close(dev);
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

    if (!headless) {
        // 3. Limpa a tela para eliminar mensagens de inicializacao do driver librtlsdr
        std::cout << "\033[2J\033[H" << std::flush;

        // 4. Exibe cabecalho e reprodutores de video
        print_cli_header();
        print_players_box(streamer.url());
        if (auto_player) {
            launch_mpv(udp_port);
        }
    }

    constexpr int VISIBLE_CHANNELS = 8;
    std::vector<ChannelInfo> channels = saved_channels;
    int visible_count = 1;
    bool has_scroll = false;
    bool show_scroll_markers = false;
    int previous_channel_first_row = 0;
    int previous_channel_line_count = 0;

    int selected_index = 0;
    for (size_t i = 0; i < channels.size(); ++i) {
        if (channels[i].channel_number == active_channel_num) {
            selected_index = static_cast<int>(i);
            break;
        }
    }

    int window_start = 0;
    auto update_layout = [&]() {
        const TerminalSize sz = get_terminal_size();
        const int row_capacity = std::max(1, sz.height - 3);
        const int max_visible = std::min(VISIBLE_CHANNELS, row_capacity);
        visible_count = channels.empty()
            ? 1
            : std::min(static_cast<int>(channels.size()), max_visible);
        has_scroll = channels.size() > static_cast<size_t>(visible_count);
        if (has_scroll && row_capacity >= 3) {
            visible_count = std::min(VISIBLE_CHANNELS, row_capacity - 2);
        }
        show_scroll_markers = has_scroll && row_capacity >= visible_count + 2;
    };
    auto update_window = [&]() {
        if (!has_scroll) {
            window_start = 0;
            return;
        }
        if (selected_index < window_start) {
            window_start = selected_index;
        } else if (selected_index >= window_start + visible_count) {
            window_start = selected_index - visible_count + 1;
        }
        int max_start = std::max(0, static_cast<int>(channels.size()) - visible_count);
        window_start = std::clamp(window_start, 0, max_start);
    };
    update_layout();
    update_window();

    std::string last_status_str;
    if (verbose) {
        std::stringstream ss;
        ss << "[Status] Ch " << active_channel_num << " | Aguardando sinal RF...";
        last_status_str = ss.str();
    }

    auto render_bottom_bars = [&]() {
        TerminalSize sz = get_terminal_size();

        // 1. Antepenultima linha (height - 2): Status (apenas com flag -v)
        if (verbose) {
            std::string safe_status = last_status_str;
            int max_w = std::max(0, sz.width - 1);
            if (static_cast<int>(safe_status.length()) > max_w) {
                safe_status = (max_w > 4) ? safe_status.substr(0, max_w - 3) + "..." : safe_status.substr(0, max_w);
            }
            std::cout << "\033[" << (sz.height - 2) << ";1H\033[2K" << safe_status;
        }

        // 2. Penultima linha (height - 1): Linha limpa de respiro
        std::cout << "\033[" << (sz.height - 1) << ";1H\033[2K";

        // 3. Ultima linha (height): Barra de navegacao (fundo branco, letras pretas)
        std::string nav_bar = " Comandos: [▲/▼] Navegar | [Enter] Sintonizar | [q] Sair";
        int target_len = std::max(0, sz.width - 1);
        if (static_cast<int>(nav_bar.length()) > target_len) {
            nav_bar = nav_bar.substr(0, target_len);
        } else if (static_cast<int>(nav_bar.length()) < target_len) {
            nav_bar += std::string(target_len - nav_bar.length(), ' ');
        }
        std::cout << "\033[" << sz.height << ";1H\033[2K\033[30;47m" << nav_bar << "\033[0m" << std::flush;
    };

    auto render_channels = [&]() {
        const TerminalSize sz = get_terminal_size();
        update_layout();
        update_window();

        std::vector<std::string> lines;
        if (channels.empty()) {
            lines.emplace_back("  Nenhum canal salvo. Execute scan para realizar a busca de canais");
        } else {
            if (show_scroll_markers) {
                if (window_start > 0) {
                    lines.emplace_back("  ▲ (mais " + std::to_string(window_start) + " canais acima)");
                } else {
                    lines.emplace_back("");
                }
            }
            for (int i = 0; i < visible_count; ++i) {
                int idx = window_start + i;
                bool is_sel = (idx == selected_index);
                bool is_act = (channels[idx].channel_number == active_channel_num);
                lines.push_back(format_channel_line(channels[idx], is_sel, is_act));
            }
            if (show_scroll_markers) {
                int rem = static_cast<int>(channels.size()) - (window_start + visible_count);
                if (rem > 0) {
                    lines.emplace_back("  ▼ (mais " + std::to_string(rem) + " canais abaixo)");
                } else {
                    lines.emplace_back("");
                }
            }
        }

        const int first_row = std::max(1, sz.height - 2 - static_cast<int>(lines.size()));
        for (int i = 0; i < previous_channel_line_count; ++i) {
            const int row = previous_channel_first_row + i;
            if (row <= sz.height) {
                std::cout << "\033[" << row << ";1H\033[2K";
            }
        }
        for (size_t i = 0; i < lines.size(); ++i) {
            std::cout << "\033[" << (first_row + static_cast<int>(i)) << ";1H\033[2K"
                      << lines[i];
        }
        previous_channel_first_row = first_row;
        previous_channel_line_count = static_cast<int>(lines.size());
        std::cout << std::flush;
    };

    auto render_dynamic_ui = [&]() {
        render_channels();
        render_bottom_bars();
    };

    // 5. Renderizacao inicial consistente (apenas modo interativo)
    if (!headless) {
        render_dynamic_ui();
    }

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

    // THREAD 1: Captura continua (USB ou Arquivo IQ)
    std::thread reader_thread([&]() {
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
        std::uint64_t sequence = 0;
        bool pending_discontinuity = false;

        std::ifstream in_file;
        if (!input_file.empty()) {
            in_file.open(input_file, std::ios::binary);
            if (!in_file.is_open()) {
                std::cerr << "[ERRO] Nao foi possivel abrir arquivo de entrada: " << input_file << "\n";
                reader_running.store(false, std::memory_order_release);
                return;
            }
        }

        while (reader_running.load(std::memory_order_relaxed) && g_running) {
            uint32_t req_freq = retune_target_freq_hz.load(std::memory_order_acquire);
            if (req_freq != 0) {
                if (dev) safe_set_freq(dev, req_freq);
                retune_target_freq_hz.store(0, std::memory_order_release);
                pending_discontinuity = true;
                retune_in_progress.store(false, std::memory_order_release);
            }

            SpscCaptureQueue<64>::WriteView view;
            if (!raw_queue->try_acquire_write(view)) {
                if (input_file.empty()) raw_queue->record_drop();
                if (input_file.empty()) {
                    ++sequence;
                    pending_discontinuity = true;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(200));
                continue;
            }

            auto* block = view.block;
            block->sequence = sequence++;
            block->size = 0;
            block->discontinuity_before = pending_discontinuity;

            if (!input_file.empty()) {
                in_file.read(reinterpret_cast<char*>(block->raw.data()), chunk_bytes);
                std::streamsize n_read = in_file.gcount();
                if (n_read <= 0) {
                    break;
                }
                block->size = static_cast<size_t>(n_read);
                raw_queue->publish_write(view);
                pending_discontinuity = false;
            } else {
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
        }
        reader_running.store(false, std::memory_order_release);
    });

    // THREAD 2: Worker de DSP e Streaming UDP dedicado (continua rodando mesmo com menu aberto!)
    std::thread dsp_worker_thread([&]() {
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif
        constexpr size_t FFT_LEN = 2048;
        auto last_pat_time = std::chrono::steady_clock::now();
        auto last_null_burst = std::chrono::steady_clock::now();

        OneSegDemodulator demod(OfdmMode::MODE_1_2K, GuardInterval::GI_1_16);
        PsiParser psi;
        TsGenerator channel_ts_gen;

        std::vector<std::complex<float>> iq_buf(chunk_samples);
        std::vector<std::complex<float>> useful_symbols;
        useful_symbols.reserve(chunk_samples);

        bool ofdm_locked = false;
        float last_peak_corr = 0.0f;
        int unlock_counter = 0;
        bool reception_started = false;
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
                reset_play_reception_stream(*sync, demod, ofdm_locked, &dsp_metrics);
                total_air_packets = 0;
                have_expected_sequence = false;
                reception_started = false;
                unlock_counter = 0;
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
                if (!reader_running.load(std::memory_order_relaxed)) {
                    break;
                }
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
                reset_play_reception_stream(*sync, demod, ofdm_locked, &dsp_metrics);
                reception_started = false;
                unlock_counter = 0;
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

                sync->process(iq_buf.data(), chunk_samples, useful_symbols);
                last_peak_corr = sync->peak_correlation();
                bool synchronizer_locked = sync->state() == SyncState::Tracking;
                bool has_synchronized_symbols = !useful_symbols.empty();
                std::vector<std::vector<uint8_t>> decoded_pkts;

                if (synchronizer_locked || has_synchronized_symbols) {
                    ofdm_locked = true;
                    reception_started = true;
                    unlock_counter = 0;
                } else {
                    if (reception_started) {
                        ofdm_locked = true;
                        if (++unlock_counter > 200) {
                            reset_play_reception_stream(*sync, demod, ofdm_locked, &dsp_metrics);
                            reception_started = false;
                            unlock_counter = 0;
                        }
                    } else {
                        ofdm_locked = false;
                    }
                }

                if (ofdm_locked) {
                    for (size_t s = 0; s + FFT_LEN <= useful_symbols.size(); s += FFT_LEN) {
                        demod.process_symbol(&useful_symbols[s], decoded_pkts);
                        dsp_metrics.processed_symbols++;
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
                                    if (file_ts_out.is_open()) {
                                        file_ts_out.write(reinterpret_cast<const char*>(pat_pkt), 188);
                                    }
                                }
                            }
                            last_pat_time = now_pat;
                        }
                        for (const auto& pkt : decoded_pkts) {
                            streamer.send_ts_packet(pkt.data());
                            if (file_ts_out.is_open()) {
                                file_ts_out.write(reinterpret_cast<const char*>(pkt.data()), 188);
                            }
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

    if (headless) {
        if (reader_thread.joinable()) {
            reader_thread.join();
        }
        if (dsp_worker_thread.joinable()) {
            dsp_worker_thread.join();
        }
    } else {
        // THREAD 3 (Main): Gerenciamento de CLI, menu e teclado interativo
        auto last_render_time = std::chrono::steady_clock::now();
        bool channel_name_learned = false;

        // Oculta o cursor durante a execucao para evitar piscamento
        std::cout << "\033[?25l" << std::flush;

        while (g_running) {
        if (console_kbhit()) {
            int key = console_getch();
            bool need_redraw = false;

            if (key == 'q' || key == 'Q') {
                g_running = false;
                break;
            } else if (key == 'k' || key == 'K') {
                if (!channels.empty()) {
                    selected_index = (selected_index > 0) ? (selected_index - 1) : static_cast<int>(channels.size() - 1);
                    update_window();
                    need_redraw = true;
                }
            } else if (key == 'j' || key == 'J') {
                if (!channels.empty()) {
                    selected_index = (selected_index + 1 < static_cast<int>(channels.size())) ? (selected_index + 1) : 0;
                    update_window();
                    need_redraw = true;
                }
            } else if (key == 0 || key == 224) {
                int ext = console_getch();
                if (ext == 72) { // Seta para Cima
                    if (!channels.empty()) {
                        selected_index = (selected_index > 0) ? (selected_index - 1) : static_cast<int>(channels.size() - 1);
                        update_window();
                        need_redraw = true;
                    }
                } else if (ext == 80) { // Seta para Baixo
                    if (!channels.empty()) {
                        selected_index = (selected_index + 1 < static_cast<int>(channels.size())) ? (selected_index + 1) : 0;
                        update_window();
                        need_redraw = true;
                    }
                }
            } else if (key == 27) { // ANSI escape sequence no Linux (\033[A ou \033[B)
                if (console_kbhit()) {
                    int c2 = console_getch();
                    if (c2 == '[') {
                        int c3 = console_getch();
                        if (c3 == 'A') { // Up
                            if (!channels.empty()) {
                                selected_index = (selected_index > 0) ? (selected_index - 1) : static_cast<int>(channels.size() - 1);
                                update_window();
                                need_redraw = true;
                            }
                        } else if (c3 == 'B') { // Down
                            if (!channels.empty()) {
                                selected_index = (selected_index + 1 < static_cast<int>(channels.size())) ? (selected_index + 1) : 0;
                                update_window();
                                need_redraw = true;
                            }
                        }
                    }
                }
            } else if (key == '\r' || key == '\n') {
                if (!channels.empty() && selected_index >= 0 && selected_index < static_cast<int>(channels.size())) {
                    int new_channel = channels[selected_index].channel_number;
                    if (new_channel != active_channel_num && is_valid_channel(new_channel)) {
                        active_channel_num = new_channel;
                        freq_hz = get_channel_freq(active_channel_num);
                        const auto* info = mgr.find_by_number(active_channel_num);
                        ch_name = (info != nullptr) ? info->name : ("Canal " + std::to_string(active_channel_num));

                        // Solicita retune a reader_thread
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
                        update_window();
                        need_redraw = true;
                    }
                }
            }

            if (need_redraw) {
                render_dynamic_ui();
            }
        }

        // Renderiza linha de status formatada e limpa (apenas se verbose estiver ativado)
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
                for (auto& c : channels) {
                    if (c.channel_number == active_channel_num) {
                        c.name = snap.service_name;
                        break;
                    }
                }
                render_dynamic_ui();
            } else if (verbose) {
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

                last_status_str = status_ss.str();
                render_bottom_bars();
            }

            last_render_time = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // Restaura o cursor do terminal
    TerminalSize final_sz = get_terminal_size();
    std::cout << "\033[" << final_sz.height << ";1H\033[2K\033[0m\033[?25h\n[Encerrando] Fechando player, dongle RTL-SDR e encerrando stream UDP...\n";
    }

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
    if (dev) {
        rtlsdr_reset_buffer(dev);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        rtlsdr_close(dev);
    }
    streamer.stop();
    if (file_ts_out.is_open()) {
        file_ts_out.close();
    }
    return 0;
}

} // namespace isdbt
