#include "sync/sync_factory.h"
#include "sync/sp_cir_sync.h"
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
#include "third_party/volk/volk_cpu.h"

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
    float evm = 0.0f;
    uint64_t air_packets = 0;
    uint64_t rs_corrected = 0;
    uint64_t rs_dropped = 0;
    uint64_t viterbi_drops = 0;
    uint64_t drop_blocks = 0;
    uint64_t decoder_resets = 0;
    uint64_t sequence_gaps = 0;
    float ts_packet_rate = 0.0f;
    std::string service_name;
    std::string epg_now;
    std::string epg_next;
    float cfo_hz = 0.0f;
    float sco_ppm = 0.0f;
    std::string sync_state = "Acquisition";
};

struct SharedPlaybackStatus {
    std::mutex mtx;
    PlaybackStatusSnapshot snap;
};

static void reset_play_reception_stream(
    IOFDMSynchronizer& sync,
    OneSegDemodulator& demod,
    bool& ofdm_locked,
    DspMetrics* metrics = nullptr,
    uint64_t* acc_rs_corrected = nullptr,
    uint64_t* acc_rs_dropped = nullptr,
    uint64_t* acc_viterbi_drops = nullptr) {

    if (acc_rs_corrected) *acc_rs_corrected += demod.corrected_rs_packets();
    if (acc_rs_dropped) *acc_rs_dropped += demod.uncorrectable_rs_packets();
    if (acc_viterbi_drops) *acc_viterbi_drops += demod.viterbi_sync_drops();

    ofdm_locked = false;
    sync.reset();
    demod.reset();

    if (metrics) {
        metrics->decoder_resets++;
    }
}

static void print_players_box(const std::string& vlc_url) {
    std::cout << " [Stream UDP] URL: " << vlc_url << "\n\n";
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
             int ofdm_mode, const std::string& input_file, const std::string& output_ts_file, bool headless,
             int duration_s, const std::string& metrics_csv_file) {
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
        ofdm_mode == 2 ? OFDMAlgorithm::SpCir : OFDMAlgorithm::VanDeBeek,
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
            if (test_pattern) {
                reload_mpv_player(udp_port);
            }
        }
    }

    constexpr int VISIBLE_CHANNELS = 8;
    constexpr int FIRST_CHANNEL_ROW = 15;
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
        const int row_capacity = std::max(1, sz.height - FIRST_CHANNEL_ROW - 2);
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

        const int first_row = FIRST_CHANNEL_ROW;
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

    SharedPlaybackStatus shared_status;
    std::string last_rendered_epg_now;
    std::string last_rendered_epg_next;

    auto render_epg_panel = [&](const std::string& now_text, const std::string& next_text) {
        if (headless) return;
        TerminalSize sz = get_terminal_size();
        int width = std::max(60, std::min(88, sz.width - 2));

        std::string now_str = now_text.empty() ? "Aguardando guia de programacao..." : now_text;
        std::string next_str = next_text.empty() ? "Aguardando guia de programacao..." : next_text;

        auto format_box_line = [&](const std::string& prefix, const std::string& content) {
            std::string line = "| " + prefix + " " + content;
            int inner_width = width - 1;
            if (static_cast<int>(line.length()) > inner_width) {
                line = (inner_width > 4) ? line.substr(0, inner_width - 3) + "..." : line.substr(0, inner_width);
            }
            if (static_cast<int>(line.length()) < inner_width) {
                line += std::string(inner_width - line.length(), ' ');
            }
            line += "|";
            return line;
        };

        std::string top_border = "+-- Programacao (EPG) " + std::string(std::max(0, width - 23), '-') + "+";
        std::string bot_border = "+" + std::string(std::max(0, width - 1), '-') + "+";

        std::cout << "\033[9;1H\033[2K" << top_border
                  << "\033[10;1H\033[2K" << format_box_line("▶ AGORA  ", now_str)
                  << "\033[11;1H\033[2K" << format_box_line("⏭ SEGUIR ", next_str)
                  << "\033[12;1H\033[2K" << bot_border << std::flush;

        last_rendered_epg_now = now_str;
        last_rendered_epg_next = next_str;
    };

    auto render_dynamic_ui = [&]() {
        std::string cur_now, cur_next;
        {
            std::lock_guard<std::mutex> lock(shared_status.mtx);
            cur_now = shared_status.snap.epg_now;
            cur_next = shared_status.snap.epg_next;
        }
        render_epg_panel(cur_now, cur_next);
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

        std::vector<uint8_t> discard_buf(chunk_bytes);

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
                if (input_file.empty()) {
                    // Modo ao vivo com fila SPSC cheia: le e descarta 1 bloco completo do RTL-SDR
                    // para manter o fluxo USB drenado sem estourar o buffer do kernel ou emitir erros de I/O.
                    int n_discard = 0;
                    rtlsdr_read_sync(dev, discard_buf.data(), static_cast<int>(chunk_bytes), &n_discard);
                    raw_queue->record_drop();
                    ++sequence;
                    pending_discontinuity = true;
                    continue;
                } else {
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                    continue;
                }
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
        int tmcc_unlock_counter = 0;
        bool reception_started = false;
        bool channel_transition_pending = !test_pattern;
        int transition_wait_blocks = 0;
        uint64_t total_air_packets = 0;
        uint64_t total_standby_packets = 0;
        uint64_t session_rs_corrected = 0;
        uint64_t session_rs_dropped = 0;
        uint64_t session_viterbi_drops = 0;
        uint64_t last_rate_pkts = 0;
        float current_ts_rate = 0.0f;
        auto last_rate_calc_time = std::chrono::steady_clock::now();

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
                {
                    std::lock_guard<std::mutex> lock(shared_status.mtx);
                    shared_status.snap.service_name.clear();
                    shared_status.snap.epg_now.clear();
                    shared_status.snap.epg_next.clear();
                }
                channel_transition_pending = true;
                transition_wait_blocks = 0;
                reset_play_reception_stream(*sync, demod, ofdm_locked, &dsp_metrics);
                total_air_packets = 0;
                session_rs_corrected = 0;
                session_rs_dropped = 0;
                session_viterbi_drops = 0;
                tmcc_unlock_counter = 0;
                current_ts_rate = 0.0f;
                last_rate_pkts = 0;
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
                reset_play_reception_stream(*sync, demod, ofdm_locked, &dsp_metrics,
                                            &session_rs_corrected, &session_rs_dropped, &session_viterbi_drops);
                reception_started = false;
                unlock_counter = 0;
                tmcc_unlock_counter = 0;
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
                    ofdm_locked = synchronizer_locked;
                    reception_started = true;
                    unlock_counter = 0;
                } else {
                    ofdm_locked = false;
                    if (reception_started) {
                        if (++unlock_counter > 200) {
                            reset_play_reception_stream(*sync, demod, ofdm_locked, &dsp_metrics,
                                                        &session_rs_corrected, &session_rs_dropped, &session_viterbi_drops);
                            reception_started = false;
                            unlock_counter = 0;
                            tmcc_unlock_counter = 0;
                        }
                    }
                }

                if (!useful_symbols.empty()) {
                    for (size_t s = 0; s + FFT_LEN <= useful_symbols.size(); s += FFT_LEN) {
                        demod.process_symbol(&useful_symbols[s], decoded_pkts);
                        dsp_metrics.processed_symbols++;
                    }
                    if (demod.is_tmcc_locked()) {
                        sync->notify_frame_sync((demod.sp_phase() + 1) & 3, demod.integer_freq_offset_bins());
                        tmcc_unlock_counter = 0;
                    } else {
                        sync->notify_frame_lost();
                        if (reception_started) {
                            // Watchdog: ~180 blocos (~3 segundos a 62 blocos/s) sem TMCC lock
                            // apos ter iniciado recepcao indica perda severa de sinal ou desancoragem.
                            if (++tmcc_unlock_counter > 180) {
                                reset_play_reception_stream(*sync, demod, ofdm_locked, &dsp_metrics,
                                                            &session_rs_corrected, &session_rs_dropped, &session_viterbi_drops);
                                reception_started = false;
                                tmcc_unlock_counter = 0;
                                unlock_counter = 0;
                            }
                        }
                    }

                    for (const auto& pkt : decoded_pkts) {
                        psi.parse_packet(pkt.data());
                    }

                    if (channel_transition_pending) {
                        // Aguarda a PMT ser decodificada nos primeiros blocos do novo canal (ou timeout de 40 blocos)
                        // para garantir que a PAT sintetizada e a PMT precedam qualquer pacote continuo de midia
                        if (!decoded_pkts.empty()) {
                            transition_wait_blocks++;
                            if (psi.has_pmt() || transition_wait_blocks >= 40) {
                                streamer.send_discontinuity_burst();

                                uint8_t pat_pkt[188];
                                if (psi.build_pat_packet(pat_pkt)) {
                                    streamer.send_ts_packet(pat_pkt);
                                    if (file_ts_out.is_open()) {
                                        file_ts_out.write(reinterpret_cast<const char*>(pat_pkt), 188);
                                    }
                                }

                                for (const auto& pkt : decoded_pkts) {
                                    streamer.send_ts_packet(pkt.data());
                                    if (file_ts_out.is_open()) {
                                        file_ts_out.write(reinterpret_cast<const char*>(pkt.data()), 188);
                                    }
                                    total_air_packets++;
                                    dsp_metrics.valid_ts_packets++;
                                }
                                streamer.flush();

                                // Notifica MPV via IPC agora que os primeiros pacotes validos e a PAT/PMT
                                // do novo canal ja estao disponiveis no socket UDP
                                reload_mpv_player(udp_port);

                                last_pat_time = std::chrono::steady_clock::now();
                                channel_transition_pending = false;
                                transition_wait_blocks = 0;
                            }
                        } else {
                            // Enquanto o novo canal ainda esta em aquisicao OFDM/TMCC, mantem keep-alive
                            auto now_keepalive = std::chrono::steady_clock::now();
                            if (std::chrono::duration_cast<std::chrono::milliseconds>(now_keepalive - last_null_burst).count() >= 50) {
                                streamer.send_null_burst(7);
                                last_null_burst = now_keepalive;
                            }
                        }
                    } else if (!decoded_pkts.empty()) {
                        auto now_pat = std::chrono::steady_clock::now();
                        if (std::chrono::duration_cast<std::chrono::milliseconds>(now_pat - last_pat_time).count() >= 100) {
                            uint8_t pat_pkt[188];
                            if (psi.build_pat_packet(pat_pkt)) {
                                streamer.send_ts_packet(pat_pkt);
                                if (file_ts_out.is_open()) {
                                    file_ts_out.write(reinterpret_cast<const char*>(pat_pkt), 188);
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
                    double dt_rate = std::chrono::duration<double>(now - last_rate_calc_time).count();
                    if (dt_rate >= 0.5) {
                        current_ts_rate = static_cast<float>((total_air_packets - last_rate_pkts) / dt_rate);
                        last_rate_pkts = total_air_packets;
                        last_rate_calc_time = now;
                    }

                    std::string s_name = psi.get_primary_service_name();
                    {
                        std::lock_guard<std::mutex> lock(shared_status.mtx);
                        shared_status.snap.evm = demod.average_evm();
                        shared_status.snap.cfo_hz = sync->estimated_cfo_hz();
                        shared_status.snap.sco_ppm = sync->estimated_sco_ppm();
                        shared_status.snap.sync_state = sync->detailed_state_str();
                        shared_status.snap.rssi_dbfs = rssi_dbfs;
                        shared_status.snap.peak_corr = last_peak_corr;
                        shared_status.snap.ofdm_locked = ofdm_locked;
                        shared_status.snap.tmcc_locked = demod.is_tmcc_locked();
                        shared_status.snap.snr_db = demod.average_snr_db();
                        shared_status.snap.air_packets = total_air_packets;
                        shared_status.snap.rs_corrected = session_rs_corrected + demod.corrected_rs_packets();
                        shared_status.snap.rs_dropped = session_rs_dropped + demod.uncorrectable_rs_packets();
                        shared_status.snap.viterbi_drops = session_viterbi_drops + demod.viterbi_sync_drops();
                        shared_status.snap.drop_blocks = raw_queue->dropped_blocks();
                        shared_status.snap.decoder_resets = dsp_metrics.decoder_resets;
                        shared_status.snap.sequence_gaps = dsp_metrics.sequence_gaps;
                        shared_status.snap.ts_packet_rate = current_ts_rate;
                        if (!s_name.empty()) {
                            shared_status.snap.service_name = s_name;
                        }
                        auto pres = psi.get_present_event();
                        auto foll = psi.get_following_event();
                        std::string now_str;
                        if (pres && !pres->title.empty()) {
                            if (!pres->start_time_str.empty()) {
                                now_str += "[" + pres->start_time_str;
                                if (!pres->end_time_str.empty()) now_str += " - " + pres->end_time_str;
                                now_str += "] ";
                            }
                            now_str += pres->title;
                            if (!pres->rating.empty()) now_str += " (" + pres->rating + ")";
                            if (!pres->genre.empty()) now_str += " [" + pres->genre + "]";
                        }
                        std::string next_str;
                        if (foll && !foll->title.empty()) {
                            if (!foll->start_time_str.empty()) {
                                next_str += "[" + foll->start_time_str;
                                if (!foll->end_time_str.empty()) next_str += " - " + foll->end_time_str;
                                next_str += "] ";
                            }
                            next_str += foll->title;
                            if (!foll->rating.empty()) next_str += " (" + foll->rating + ")";
                            if (!foll->genre.empty()) next_str += " [" + foll->genre + "]";
                        }
                        shared_status.snap.epg_now = now_str;
                        shared_status.snap.epg_next = next_str;
                    }
                    last_status_update = now;
                }
            }

            raw_queue->release_read(view);
        }
    });

    if (headless) {
        std::ofstream csv_out;
        if (!metrics_csv_file.empty()) {
            csv_out.open(metrics_csv_file);
            if (csv_out.is_open()) {
                csv_out << "timestamp_s,elapsed_s,rssi_dbfs,peak_corr,ofdm_locked,tmcc_locked,snr_db,evm,"
                        << "valid_ts_packets,ts_rate,rs_corrected,rs_dropped,viterbi_drops,spsc_drops,seq_gaps,resets,cfo_hz,sco_ppm,sync_state\n";
            }
        }

        auto start_time = std::chrono::steady_clock::now();
        auto last_report_time = start_time;
        int second_counter = 0;

        if (verbose) {
            std::cout << "[Headless] Iniciando monitoramento "
                      << (duration_s > 0 ? ("por " + std::to_string(duration_s) + " segundos") : "continuo")
                      << "...\n" << std::flush;
        }

        while (g_running && reader_running.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto now = std::chrono::steady_clock::now();
            double elapsed_s = std::chrono::duration<double>(now - start_time).count();

            if (duration_s > 0 && elapsed_s >= duration_s) {
                if (verbose) {
                    std::cout << "\n[Duracao] Limite de " << duration_s << "s atingido. Encerrando graciosamente...\n" << std::flush;
                }
                g_running = false;
                break;
            }

            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_report_time).count() >= 1000) {
                last_report_time = now;
                second_counter++;
                PlaybackStatusSnapshot snap;
                {
                    std::lock_guard<std::mutex> lock(shared_status.mtx);
                    snap = shared_status.snap;
                }

                if (csv_out.is_open()) {
                    csv_out << second_counter << ","
                            << std::fixed << std::setprecision(2) << elapsed_s << ","
                            << snap.rssi_dbfs << ","
                            << snap.peak_corr << ","
                            << (snap.ofdm_locked ? 1 : 0) << ","
                            << (snap.tmcc_locked ? 1 : 0) << ","
                            << snap.snr_db << ","
                            << snap.evm << ","
                            << snap.air_packets << ","
                            << std::fixed << std::setprecision(1) << snap.ts_packet_rate << ","
                            << snap.rs_corrected << ","
                            << snap.rs_dropped << ","
                            << snap.viterbi_drops << ","
                            << snap.drop_blocks << ","
                            << snap.sequence_gaps << ","
                            << snap.decoder_resets << ","
                            << snap.cfo_hz << ","
                            << snap.sco_ppm << ","
                            << snap.sync_state << "\n";
                    csv_out.flush();
                }

                if (verbose) {
                    std::cout << "[" << std::setw(3) << second_counter << "s"
                              << (duration_s > 0 ? ("/" + std::to_string(duration_s) + "s") : "")
                              << "] RSSI: " << std::fixed << std::setprecision(1) << snap.rssi_dbfs << " dBFS | "
                              << "OFDM: " << (snap.ofdm_locked ? "LOCK" : "SYNC") << " (" << std::setprecision(2) << snap.peak_corr << ") | "
                              << "TMCC: " << (snap.tmcc_locked ? "LOCK" : "SYNC") << " | "
                              << "SNR: " << std::fixed << std::setprecision(1) << snap.snr_db << " dB | "
                              << "TS: " << snap.air_packets << " pkts (" << std::fixed << std::setprecision(0) << snap.ts_packet_rate << " p/s) | "
                              << "RS: " << snap.rs_corrected << " corr / " << snap.rs_dropped << " drop | "
                              << "Resets: " << snap.decoder_resets << " | "
                              << "Drop: " << snap.drop_blocks << " | State: " << snap.sync_state << "\n" << std::flush;
                }
            }
        }

        if (csv_out.is_open()) {
            csv_out.close();
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
            } else if ((key == 0 || key == 224) && console_kbhit()) {
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
                    if (c2 == '[' && console_kbhit()) {
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

                        // 4. Notifica recarga via IPC ao MPV (tanto modo integrado quanto scripts externos)
                        reload_mpv_player(udp_port);

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

            if (snap.epg_now != last_rendered_epg_now || snap.epg_next != last_rendered_epg_next) {
                render_epg_panel(snap.epg_now, snap.epg_next);
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
                status_ss << "[Status] Ch " << active_channel_num << " | SIMD: " << volk_cpu.active_kernel_name() << " | RSSI: " << std::fixed << std::setprecision(1) << snap.rssi_dbfs << " dBFS | "
                          << "OFDM: " << (snap.ofdm_locked ? "LOCK" : "SYNC") << " (" << std::setprecision(2) << snap.peak_corr << ") | "
                          << "TMCC: " << (snap.tmcc_locked ? "LOCK" : "SYNC") << " | "
                          << "SNR: " << std::fixed << std::setprecision(1) << snap.snr_db << " dB | "
                          << "TS: " << snap.air_packets << " pkts (" << std::fixed << std::setprecision(0) << snap.ts_packet_rate << " p/s) | "
                          << "RS: " << snap.rs_corrected << " corr / " << snap.rs_dropped << " drop | "
                          << "Resets: " << snap.decoder_resets << " | "
                          << "Drop: " << snap.drop_blocks;

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
