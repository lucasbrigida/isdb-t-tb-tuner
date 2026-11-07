#include "cli/commands.hpp"
#include "cli/channel_manager.hpp"
#include "cli/epg_manager.hpp"
#include "radio/frequency_plan.hpp"
#include "radio/rtlsdr_device.hpp"
#include "sync/sync_factory.h"
#include "dsp/constants.hpp"
#include "dsp/oneseg_demodulator.hpp"
#include "dsp/spsc_capture_queue.hpp"
#include "mpegts/psi_parser.hpp"
#include "utils/ascii_table.h"
#include "platform/platform_utils.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <rtl-sdr.h>

#ifdef _WIN32
    #include <windows.h>
#endif

namespace isdbt {

struct ChannelEpgData {
    int channel_number = 0;
    std::string name;
    uint32_t frequency_hz = 0;
    bool signal_found = false;
    std::optional<EpgEvent> present;
    std::optional<EpgEvent> following;
    std::vector<EpgEvent> schedule;
};

static std::string format_duration(int sec) {
    if (sec <= 0) return "--";
    int h = sec / 3600;
    int m = (sec % 3600) / 60;
    std::stringstream ss;
    if (h > 0) {
        ss << h << "h" << std::setw(2) << std::setfill('0') << m << "m";
    } else {
        ss << m << " min";
    }
    return ss.str();
}

struct ChannelSummaryItem {
    int channel_number = 0;
    std::string name;
    std::string time_str = "--";
    std::string rating_str = "--";
    std::string genre_str = "--";
    std::string prog_str = "Sem informacao salva";
    std::string short_desc;
    std::string extended_desc;
};

static int render_epg_interactive_single(int channel_number,
                                         const std::string& channel_name,
                                         const std::vector<EpgEvent>& events,
                                         std::chrono::system_clock::time_point now_tp = {}) {
    if (events.empty()) {
        std::cout << "Nenhum evento de programacao (EIT) foi transmitido pela emissora dentro do tempo limite.\n\n";
        return 0;
    }

    AsciiTable base_table({"Status", "Horario", "Duracao", "Classif.", "Genero", "Programa"});
    base_table.set_alignments({Alignment::Center, Alignment::Center, Alignment::Center, Alignment::Center, Alignment::Left, Alignment::Left});
    base_table.set_column_widths({9, 13, 7, 8, 12, 45});

    int first_now_idx = -1;
    for (size_t i = 0; i < events.size(); ++i) {
        const auto& e = events[i];
        std::string status = "AGENDA";
        if (e.is_now) {
            status = "[AGORA]";
            if (first_now_idx < 0) first_now_idx = static_cast<int>(i);
        } else if (e.is_next) {
            status = "[SEGUIR]";
        }

        std::string time_str = e.start_time_str.empty() ? EpgManager::format_local_time(e.start_tp) : e.start_time_str;
        if (!e.end_time_str.empty()) {
            time_str += " - " + e.end_time_str;
        } else if (e.end_tp != std::chrono::system_clock::time_point{}) {
            time_str += " - " + EpgManager::format_local_time(e.end_tp);
        }

        base_table.add_row({status,
                            time_str,
                            format_duration(e.duration_sec),
                            e.rating.empty() ? "--" : e.rating,
                            e.genre.empty() ? "--" : e.genre,
                            e.title});
    }

    int initial_sel = (first_now_idx >= 0) ? first_now_idx : 0;

    if (!is_interactive_terminal()) {
        std::string banner_line(base_table.total_width(), '=');
        std::cout << banner_line << "\n"
                  << " Guia Eletronico de Programacao (EPG) - Canal " << channel_number;
        if (!channel_name.empty()) {
            std::cout << " (" << channel_name << ")";
        }
        std::cout << "\n";
        if (now_tp != std::chrono::system_clock::time_point{}) {
            std::cout << " [Horario Atual da Maquina: " << EpgManager::format_local_time(now_tp) << "]\n";
        }
        std::cout << banner_line << "\n";
        base_table.print(std::cout);
        std::cout << "\n";

        const auto& syn_evt = events[initial_sel];
        if (!syn_evt.short_desc.empty() || !syn_evt.extended_desc.empty()) {
            std::cout << "[Sinopse - " << syn_evt.title << "]\n";
            if (!syn_evt.short_desc.empty()) {
                std::cout << "  " << syn_evt.short_desc << "\n";
            }
            if (!syn_evt.extended_desc.empty() && syn_evt.extended_desc != syn_evt.short_desc) {
                std::cout << "  " << syn_evt.extended_desc << "\n";
            }
            std::cout << "\n";
        }
        return 0;
    }

    int selected_index = initial_sel;
    int window_start_prev = 0;

    std::cout << "\033[?25l\033[2J\033[H" << std::flush;

    auto render_frame = [&]() {
        TerminalSize sz = get_terminal_size();
        int overhead = 17;
        int visible_count = std::max(3, sz.height - overhead);
        visible_count = std::min(visible_count, static_cast<int>(events.size()));
        bool has_scroll = static_cast<int>(events.size()) > visible_count;

        int window_start = 0;
        if (has_scroll) {
            if (selected_index < window_start_prev) {
                window_start = selected_index;
            } else if (selected_index >= window_start_prev + visible_count) {
                window_start = selected_index - visible_count + 1;
            } else {
                window_start = window_start_prev;
            }
            int max_start = std::max(0, static_cast<int>(events.size()) - visible_count);
            window_start = std::clamp(window_start, 0, max_start);
        }
        window_start_prev = window_start;

        std::cout << "\033[H";
        std::string banner_line(base_table.total_width(), '=');
        std::cout << "\033[2K" << banner_line << "\n";
        std::cout << "\033[2K Guia Eletronico de Programacao (EPG) - Canal " << channel_number;
        if (!channel_name.empty()) {
            std::cout << " (" << channel_name << ")";
        }
        std::cout << "\n";
        if (now_tp != std::chrono::system_clock::time_point{}) {
            std::cout << "\033[2K [Horario Atual da Maquina: " << EpgManager::format_local_time(now_tp) << "]\n";
        }
        std::cout << "\033[2K" << banner_line << "\n";

        if (has_scroll && window_start > 0) {
            std::cout << "\033[2K  ▲ (mais " << window_start << " programas acima)\n";
        } else {
            std::cout << "\033[2K\n";
        }

        base_table.set_visible_range(window_start, visible_count);
        base_table.set_selected_row(selected_index);
        std::string tbl_str = base_table.render(base_table.total_width());
        std::stringstream ss(tbl_str);
        std::string line;
        while (std::getline(ss, line)) {
            std::cout << "\033[2K" << line << "\n";
        }

        int rem = static_cast<int>(events.size()) - (window_start + visible_count);
        if (has_scroll && rem > 0) {
            std::cout << "\033[2K  ▼ (mais " << rem << " programas abaixo)\n";
        } else {
            std::cout << "\033[2K\n";
        }

        const auto& syn_evt = events[selected_index];
        std::string syn_title = syn_evt.title.empty() ? "Sem Titulo" : syn_evt.title;
        std::cout << "\033[2K[Sinopse - " << syn_title << "]\n";

        bool has_desc = (!syn_evt.short_desc.empty() || !syn_evt.extended_desc.empty());
        if (has_desc) {
            std::string s1 = !syn_evt.short_desc.empty() ? syn_evt.short_desc : syn_evt.extended_desc;
            size_t max_text_w = static_cast<size_t>(std::max(40, sz.width - 4));
            if (AsciiTable::utf8_display_width(s1) > max_text_w) {
                s1 = AsciiTable::utf8_trim_to_width(s1, max_text_w - 3) + "...";
            }
            std::cout << "\033[2K  " << s1 << "\n";

            std::string s2 = (!syn_evt.short_desc.empty() && !syn_evt.extended_desc.empty() && syn_evt.extended_desc != syn_evt.short_desc)
                                 ? syn_evt.extended_desc : "";
            if (!s2.empty()) {
                if (AsciiTable::utf8_display_width(s2) > max_text_w) {
                    s2 = AsciiTable::utf8_trim_to_width(s2, max_text_w - 3) + "...";
                }
                std::cout << "\033[2K  " << s2 << "\n";
            } else {
                std::cout << "\033[2K\n";
            }
        } else {
            std::cout << "\033[2K  \033[90m[Sinopse indisponivel para este evento]\033[0m\n";
            std::cout << "\033[2K\n";
        }

        std::cout << "\033[" << (sz.height - 1) << ";1H\033[2K";

        std::string nav_bar = " Comandos: [▲/▼] Selecionar Programa | [q] Sair";
        int target_len = std::max(0, sz.width - 1);
        if (static_cast<int>(nav_bar.length()) > target_len) {
            nav_bar = nav_bar.substr(0, target_len);
        } else if (static_cast<int>(nav_bar.length()) < target_len) {
            nav_bar += std::string(target_len - nav_bar.length(), ' ');
        }
        std::cout << "\033[" << sz.height << ";1H\033[2K\033[30;47m" << nav_bar << "\033[0m" << std::flush;
    };

    render_frame();

    while (g_running) {
        if (console_kbhit()) {
            int key = console_getch();
            if (key == 'q' || key == 'Q') {
                break;
            } else if (key == 'k' || key == 'K') {
                selected_index = (selected_index > 0) ? (selected_index - 1) : static_cast<int>(events.size() - 1);
                render_frame();
            } else if (key == 'j' || key == 'J') {
                selected_index = (selected_index + 1 < static_cast<int>(events.size())) ? (selected_index + 1) : 0;
                render_frame();
            } else if (key == 0 || key == 224) {
                int ext = console_getch();
                if (ext == 72) { // Up
                    selected_index = (selected_index > 0) ? (selected_index - 1) : static_cast<int>(events.size() - 1);
                    render_frame();
                } else if (ext == 80) { // Down
                    selected_index = (selected_index + 1 < static_cast<int>(events.size())) ? (selected_index + 1) : 0;
                    render_frame();
                }
            } else if (key == 27) { // ANSI
                if (console_kbhit()) {
                    int c2 = console_getch();
                    if (c2 == '[') {
                        int c3 = console_getch();
                        if (c3 == 'A') { // Up
                            selected_index = (selected_index > 0) ? (selected_index - 1) : static_cast<int>(events.size() - 1);
                            render_frame();
                        } else if (c3 == 'B') { // Down
                            selected_index = (selected_index + 1 < static_cast<int>(events.size())) ? (selected_index + 1) : 0;
                            render_frame();
                        }
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    TerminalSize final_sz = get_terminal_size();
    std::cout << "\033[?25h\033[" << final_sz.height << ";1H\n" << std::flush;
    return 0;
}

static int render_epg_interactive_multi(const std::string& title,
                                        const std::vector<ChannelSummaryItem>& items,
                                        std::chrono::system_clock::time_point now_tp = {}) {
    if (items.empty()) {
        std::cout << "Nenhum canal disponivel para exibicao do guia.\n\n";
        return 0;
    }

    AsciiTable base_table({"Canal", "Emissora", "Horario", "Classif.", "Genero", "Programa Atual (Agora)"});
    base_table.set_alignments({Alignment::Center, Alignment::Left, Alignment::Center, Alignment::Center, Alignment::Center, Alignment::Left});
    base_table.set_column_widths({5, 18, 15, 8, 12, 36});

    for (const auto& it : items) {
        base_table.add_row({std::to_string(it.channel_number),
                            it.name,
                            it.time_str,
                            it.rating_str,
                            it.genre_str,
                            it.prog_str});
    }

    if (!is_interactive_terminal()) {
        std::string banner_line(base_table.total_width(), '=');
        std::cout << banner_line << "\n"
                  << " " << title << "\n";
        if (now_tp != std::chrono::system_clock::time_point{}) {
            std::cout << " [Horario Atual da Maquina: " << EpgManager::format_local_time(now_tp) << "]\n";
        }
        std::cout << banner_line << "\n";
        base_table.print(std::cout);
        std::cout << "\n Dica: Para ver a sinopse e grade completa de um canal, use: .\\isdbtb_rx.exe epg -c <numero> --view\n\n";
        return 0;
    }

    int selected_index = 0;
    int window_start_prev = 0;

    std::cout << "\033[?25l\033[2J\033[H" << std::flush;

    auto render_frame = [&]() {
        TerminalSize sz = get_terminal_size();
        int overhead = 17;
        int visible_count = std::max(3, sz.height - overhead);
        visible_count = std::min(visible_count, static_cast<int>(items.size()));
        bool has_scroll = static_cast<int>(items.size()) > visible_count;

        int window_start = 0;
        if (has_scroll) {
            if (selected_index < window_start_prev) {
                window_start = selected_index;
            } else if (selected_index >= window_start_prev + visible_count) {
                window_start = selected_index - visible_count + 1;
            } else {
                window_start = window_start_prev;
            }
            int max_start = std::max(0, static_cast<int>(items.size()) - visible_count);
            window_start = std::clamp(window_start, 0, max_start);
        }
        window_start_prev = window_start;

        std::cout << "\033[H";
        std::string banner_line(base_table.total_width(), '=');
        std::cout << "\033[2K" << banner_line << "\n";
        std::cout << "\033[2K " << title << "\n";
        if (now_tp != std::chrono::system_clock::time_point{}) {
            std::cout << "\033[2K [Horario Atual da Maquina: " << EpgManager::format_local_time(now_tp) << "]\n";
        }
        std::cout << "\033[2K" << banner_line << "\n";

        if (has_scroll && window_start > 0) {
            std::cout << "\033[2K  ▲ (mais " << window_start << " canais acima)\n";
        } else {
            std::cout << "\033[2K\n";
        }

        base_table.set_visible_range(window_start, visible_count);
        base_table.set_selected_row(selected_index);
        std::string tbl_str = base_table.render(base_table.total_width());
        std::stringstream ss(tbl_str);
        std::string line;
        while (std::getline(ss, line)) {
            std::cout << "\033[2K" << line << "\n";
        }

        int rem = static_cast<int>(items.size()) - (window_start + visible_count);
        if (has_scroll && rem > 0) {
            std::cout << "\033[2K  ▼ (mais " << rem << " canais abaixo)\n";
        } else {
            std::cout << "\033[2K\n";
        }

        const auto& sel_item = items[selected_index];
        std::cout << "\033[2K[Sinopse - Canal " << sel_item.channel_number << " (" << sel_item.name << "): "
                  << sel_item.prog_str << "]\n";

        bool has_desc = (!sel_item.short_desc.empty() || !sel_item.extended_desc.empty());
        if (has_desc) {
            std::string s1 = !sel_item.short_desc.empty() ? sel_item.short_desc : sel_item.extended_desc;
            size_t max_text_w = static_cast<size_t>(std::max(40, sz.width - 4));
            if (AsciiTable::utf8_display_width(s1) > max_text_w) {
                s1 = AsciiTable::utf8_trim_to_width(s1, max_text_w - 3) + "...";
            }
            std::cout << "\033[2K  " << s1 << "\n";

            std::string s2 = (!sel_item.short_desc.empty() && !sel_item.extended_desc.empty() && sel_item.extended_desc != sel_item.short_desc)
                                 ? sel_item.extended_desc : "";
            if (!s2.empty()) {
                if (AsciiTable::utf8_display_width(s2) > max_text_w) {
                    s2 = AsciiTable::utf8_trim_to_width(s2, max_text_w - 3) + "...";
                }
                std::cout << "\033[2K  " << s2 << "\n";
            } else {
                std::cout << "\033[2K\n";
            }
        } else {
            std::cout << "\033[2K  \033[90m[Sinopse indisponivel para este evento]\033[0m\n";
            std::cout << "\033[2K\n";
        }

        std::cout << "\033[" << (sz.height - 1) << ";1H\033[2K";

        std::string nav_bar = " Comandos: [▲/▼] Selecionar Programa | [q] Sair";
        int target_len = std::max(0, sz.width - 1);
        if (static_cast<int>(nav_bar.length()) > target_len) {
            nav_bar = nav_bar.substr(0, target_len);
        } else if (static_cast<int>(nav_bar.length()) < target_len) {
            nav_bar += std::string(target_len - nav_bar.length(), ' ');
        }
        std::cout << "\033[" << sz.height << ";1H\033[2K\033[30;47m" << nav_bar << "\033[0m" << std::flush;
    };

    render_frame();

    while (g_running) {
        if (console_kbhit()) {
            int key = console_getch();
            if (key == 'q' || key == 'Q') {
                break;
            } else if (key == 'k' || key == 'K') {
                selected_index = (selected_index > 0) ? (selected_index - 1) : static_cast<int>(items.size() - 1);
                render_frame();
            } else if (key == 'j' || key == 'J') {
                selected_index = (selected_index + 1 < static_cast<int>(items.size())) ? (selected_index + 1) : 0;
                render_frame();
            } else if (key == 0 || key == 224) {
                int ext = console_getch();
                if (ext == 72) { // Up
                    selected_index = (selected_index > 0) ? (selected_index - 1) : static_cast<int>(items.size() - 1);
                    render_frame();
                } else if (ext == 80) { // Down
                    selected_index = (selected_index + 1 < static_cast<int>(items.size())) ? (selected_index + 1) : 0;
                    render_frame();
                }
            } else if (key == 27) { // ANSI
                if (console_kbhit()) {
                    int c2 = console_getch();
                    if (c2 == '[') {
                        int c3 = console_getch();
                        if (c3 == 'A') { // Up
                            selected_index = (selected_index > 0) ? (selected_index - 1) : static_cast<int>(items.size() - 1);
                            render_frame();
                        } else if (c3 == 'B') { // Down
                            selected_index = (selected_index + 1 < static_cast<int>(items.size())) ? (selected_index + 1) : 0;
                            render_frame();
                        }
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    TerminalSize final_sz = get_terminal_size();
    std::cout << "\033[?25h\033[" << final_sz.height << ";1H\n" << std::flush;
    return 0;
}

// ---------------------------------------------------------------------------
// MODO 1: EPG GERAL CONSOLIDADO (TODOS OS CANAIS SALVOS EM CHANNELS.JSON)
// ---------------------------------------------------------------------------
static int run_epg_all(const std::vector<ChannelInfo>& saved_channels, int dev_idx,
                       int per_channel_timeout_s, bool output_json, bool force_refresh) {
    EpgManager epg_mgr("epg.json");
    epg_mgr.load();
    auto now_tp = std::chrono::system_clock::now();
    epg_mgr.prune_expired(now_tp);

    // Avalia quais canais ja estao no cache com programa valido para o horario atual
    std::vector<ChannelInfo> channels_to_scan;
    std::vector<ChannelEpgData> results;

    for (const auto& ch : saved_channels) {
        ChannelEpgData ch_data;
        ch_data.channel_number = ch.channel_number;
        ch_data.name = ch.name;
        ch_data.frequency_hz = ch.frequency_hz;

        EpgEvent now_evt, next_evt;
        if (!force_refresh && epg_mgr.has_valid_now(ch.channel_number, now_tp, &now_evt, &next_evt)) {
            ch_data.signal_found = true;
            ch_data.present = now_evt;
            if (!next_evt.title.empty()) ch_data.following = next_evt;
            const auto* cached_ch = epg_mgr.get_channel(ch.channel_number);
            if (cached_ch) ch_data.schedule = cached_ch->events;
            results.push_back(ch_data);
        } else {
            channels_to_scan.push_back(ch);
        }
    }

    if (!channels_to_scan.empty()) {
        if (!output_json) {
            if (results.empty()) {
                std::cout << "Sintonizando todos os canais salvos (" << channels_to_scan.size() << " canais)...\n\n";
            } else {
                std::cout << "[" << results.size() << " canais carregados do cache local]. Sintonizando "
                          << channels_to_scan.size() << " canais pendentes no ar...\n\n";
            }
        }

        rtlsdr_dev_t* dev = nullptr;
        int r = rtlsdr_open(&dev, dev_idx);
        if (r < 0 || !dev) {
            std::cerr << "[ERRO] Falha ao abrir dongle RTL-SDR #" << dev_idx << ". Codigo: " << r << "\n";
            return 1;
        }

        rtlsdr_reset_buffer(dev);
        constexpr uint32_t sample_rate = constants::SAMPLE_RATE;
        rtlsdr_set_sample_rate(dev, sample_rate);
        rtlsdr_set_tuner_gain_mode(dev, 1);
        rtlsdr_set_tuner_gain(dev, 280);
        rtlsdr_set_agc_mode(dev, 0);

        const size_t chunk_samples = 32768;
        const size_t chunk_bytes = chunk_samples * 2;
        auto raw_queue = std::make_unique<SpscCaptureQueue<64>>();

        SyncParams sync_params;
        sync_params.sample_rate = static_cast<float>(sample_rate);
        auto sync = make_synchronizer(OFDMAlgorithm::VanDeBeek, sync_params);

        OneSegDemodulator demod(OfdmMode::MODE_1_2K, GuardInterval::GI_1_16);
        constexpr size_t FFT_LEN = 2048;

        std::vector<std::complex<float>> cand_iq_buf(chunk_samples);
        std::vector<std::complex<float>> useful_symbols;
        useful_symbols.reserve(chunk_samples);

        const int effective_timeout = (per_channel_timeout_s > 0) ? per_channel_timeout_s : 5;

        for (size_t ch_idx = 0; ch_idx < channels_to_scan.size() && g_running; ++ch_idx) {
            const auto& ch = channels_to_scan[ch_idx];
            ChannelEpgData ch_data;
            ch_data.channel_number = ch.channel_number;
            ch_data.name = ch.name;
            ch_data.frequency_hz = ch.frequency_hz;

            if (!output_json) {
                std::cout << "[" << (ch_idx + 1) << "/" << channels_to_scan.size() << "] Sintonizando Canal "
                          << ch.channel_number << " (" << ch.name << " - "
                          << std::fixed << std::setprecision(3) << (ch.frequency_hz / 1e6) << " MHz)..." << std::flush;
            }

            safe_set_freq(dev, ch.frequency_hz);

            // Drena pacotes residuais da fila
            SpscCaptureQueue<64>::ReadView drain_view;
            while (raw_queue->try_acquire_read(drain_view)) {
                raw_queue->release_read(drain_view);
            }

            std::atomic<bool> reader_running{true};
            std::thread reader_thread([&]() {
#ifdef _WIN32
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
                        std::this_thread::sleep_for(std::chrono::microseconds(200));
                        continue;
                    }

                    auto* block = view.block;
                    block->sequence = sequence++;
                    block->size = 0;
                    block->discontinuity_before = pending_discontinuity;

                    int n_read = 0;
                    int ret = rtlsdr_read_sync(dev, block->raw.data(), static_cast<int>(chunk_bytes), &n_read);
                    if (ret < 0 || n_read != static_cast<int>(chunk_bytes)) {
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

            sync->reset();
            demod.reset();
            PsiParser psi;

            std::uint64_t expected_sequence = 0;
            bool have_expected_sequence = false;
            auto ch_start = std::chrono::steady_clock::now();
            int lock_wait_blocks = 0;

            while (g_running) {
                auto now = std::chrono::steady_clock::now();
                int elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(now - ch_start).count());
                if (elapsed >= effective_timeout) break;

                SpscCaptureQueue<64>::ReadView view;
                if (!raw_queue->try_acquire_read(view)) {
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                    continue;
                }

                const auto& block = *view.block;
                bool discontinuity = block.discontinuity_before;
                if (have_expected_sequence && block.sequence != expected_sequence) {
                    discontinuity = true;
                }
                expected_sequence = block.sequence + 1;
                have_expected_sequence = true;

                if (discontinuity) {
                    sync->reset();
                    demod.reset();
                }

                if (block.size == chunk_bytes) {
                    const uint8_t* raw_data = block.raw.data();
                    for (size_t i = 0; i < chunk_samples; ++i) {
                        float iv = (static_cast<float>(raw_data[2 * i + 0]) - 127.5f) / 127.5f;
                        float qv = (static_cast<float>(raw_data[2 * i + 1]) - 127.5f) / 127.5f;
                        cand_iq_buf[i] = std::complex<float>(iv, qv);
                    }

                    sync->process(cand_iq_buf.data(), chunk_samples, useful_symbols);
                    if (sync->state() == SyncState::Tracking || !useful_symbols.empty()) {
                        ch_data.signal_found = true;
                    } else {
                        lock_wait_blocks++;
                        // Se apos 2s nenhum sinal OFDM for detectado, passa para o proximo canal rapidamente
                        if (lock_wait_blocks > 50 && elapsed >= 2 && !ch_data.signal_found) {
                            raw_queue->release_read(view);
                            break;
                        }
                    }

                    std::vector<std::vector<uint8_t>> decoded_pkts;
                    for (size_t s = 0; s + FFT_LEN <= useful_symbols.size(); s += FFT_LEN) {
                        demod.process_symbol(&useful_symbols[s], decoded_pkts);
                    }

                    for (const auto& pkt : decoded_pkts) {
                        psi.parse_packet(pkt.data());
                    }

                    std::string s_name = psi.get_primary_service_name();
                    if (!s_name.empty()) {
                        ch_data.name = s_name;
                    }

                    auto pres = psi.get_present_event();
                    auto foll = psi.get_following_event();

                    // Saida rapida assim que capturar o programa atual
                    if (pres.has_value() && !pres->title.empty()) {
                        ch_data.present = pres;
                        ch_data.following = foll;
                        ch_data.schedule = psi.get_schedule_events();
                        raw_queue->release_read(view);
                        break;
                    }
                }

                raw_queue->release_read(view);
            }

            reader_running.store(false, std::memory_order_release);
            if (reader_thread.joinable()) {
                reader_thread.join();
            }

            if (!ch_data.present.has_value()) {
                ch_data.present = psi.get_present_event();
                ch_data.following = psi.get_following_event();
                ch_data.schedule = psi.get_schedule_events();
            }

            // Enriquece no EpgManager com os dados obtidos
            std::vector<EpgEvent> ch_events;
            if (ch_data.present) ch_events.push_back(*ch_data.present);
            if (ch_data.following) ch_events.push_back(*ch_data.following);
            for (const auto& s : ch_data.schedule) ch_events.push_back(s);

            epg_mgr.enrich_channel_events(ch.channel_number, ch_data.name, ch.frequency_hz,
                                         ch_data.signal_found, ch_events, now_tp);

            if (!output_json) {
                if (ch_data.present.has_value() && !ch_data.present->title.empty()) {
                    std::cout << " OK (" << ch_data.present->title << ")\n";
                } else if (ch_data.signal_found) {
                    std::cout << " Sinal OK (Sem EPG no momento)\n";
                } else {
                    std::cout << " Sem Sinal\n";
                }
            }

            results.push_back(ch_data);
        }

        rtlsdr_reset_buffer(dev);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        rtlsdr_close(dev);

        epg_mgr.save();
    } else {
        if (!output_json) {
            std::cout << "[Cache] Todos os " << results.size() << " canais possuem programacao valida no cache local './epg.json'.\n"
                      << "        (Horario da maquina: " << EpgManager::format_local_time(now_tp) << ").\n\n";
        }
        epg_mgr.save();
    }

    // Ordena os resultados por numero de canal
    std::sort(results.begin(), results.end(), [](const ChannelEpgData& a, const ChannelEpgData& b) {
        return a.channel_number < b.channel_number;
    });

    if (output_json) {
        std::cout << epg_mgr.to_json_string() << "\n";
        return 0;
    }

    std::vector<ChannelSummaryItem> items;
    for (const auto& ch : results) {
        ChannelSummaryItem it;
        it.channel_number = ch.channel_number;
        it.name = ch.name;
        if (!ch.signal_found) {
            it.prog_str = "(Sem sinal RF)";
        } else if (ch.present.has_value() && !ch.present->title.empty()) {
            it.prog_str = ch.present->title;
            if (!ch.present->start_time_str.empty()) {
                it.time_str = ch.present->start_time_str;
                if (!ch.present->end_time_str.empty()) it.time_str += " - " + ch.present->end_time_str;
            }
            if (!ch.present->rating.empty()) it.rating_str = ch.present->rating;
            if (!ch.present->genre.empty()) it.genre_str = ch.present->genre;
            it.short_desc = ch.present->short_desc;
            it.extended_desc = ch.present->extended_desc;
        }
        items.push_back(it);
    }

    return render_epg_interactive_multi("Guia Geral de Programacao (EPG) - Todos os Canais Salvos", items, now_tp);
}

// ---------------------------------------------------------------------------
// MODO 2: EPG INDIVIDUAL DETALHADO (CANAL ESPECÍFICO VIA -c OU -n)
// ---------------------------------------------------------------------------
static int run_epg_single(int active_channel_num, const std::string& initial_name, uint32_t freq_hz,
                          int dev_idx, int timeout_s, bool output_json, bool force_refresh) {
    EpgManager epg_mgr("epg.json");
    epg_mgr.load();
    auto now_tp = std::chrono::system_clock::now();
    epg_mgr.prune_expired(now_tp);

    if (!output_json) {
        std::cout << "Sintonizando Canal " << active_channel_num;
        if (!initial_name.empty()) std::cout << " (" << initial_name << ")";
        std::cout << " [" << std::fixed << std::setprecision(3) << (freq_hz / 1e6) << " MHz]...\n";
        if (timeout_s > 0) {
            std::cout << "Aguardando sinal e metadados EPG (max " << timeout_s << "s)...\n" << std::flush;
        } else {
            std::cout << "Monitorando EPG continuamente (Pressione 'q' para sair)...\n" << std::flush;
        }
    }

    rtlsdr_dev_t* dev = nullptr;
    int r = rtlsdr_open(&dev, dev_idx);
    if (r < 0 || !dev) {
        std::cerr << "[ERRO] Falha ao abrir dongle RTL-SDR #" << dev_idx << ". Codigo: " << r << "\n";
        return 1;
    }

    rtlsdr_reset_buffer(dev);
    constexpr uint32_t sample_rate = constants::SAMPLE_RATE;
    rtlsdr_set_sample_rate(dev, sample_rate);
    rtlsdr_set_tuner_gain_mode(dev, 1);
    rtlsdr_set_tuner_gain(dev, 280);
    rtlsdr_set_agc_mode(dev, 0);
    safe_set_freq(dev, freq_hz);

    const size_t chunk_samples = 32768;
    const size_t chunk_bytes = chunk_samples * 2;
    auto raw_queue = std::make_unique<SpscCaptureQueue<64>>();

    std::atomic<bool> reader_running{true};
    std::thread reader_thread([&]() {
#ifdef _WIN32
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
                std::this_thread::sleep_for(std::chrono::microseconds(200));
                continue;
            }

            auto* block = view.block;
            block->sequence = sequence++;
            block->size = 0;
            block->discontinuity_before = pending_discontinuity;

            int n_read = 0;
            int ret = rtlsdr_read_sync(dev, block->raw.data(), static_cast<int>(chunk_bytes), &n_read);
            if (ret < 0 || n_read != static_cast<int>(chunk_bytes)) {
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

    SyncParams sync_params;
    sync_params.sample_rate = static_cast<float>(sample_rate);
    auto sync = make_synchronizer(OFDMAlgorithm::VanDeBeek, sync_params);
    if (!sync) {
        std::cerr << "[ERRO] Falha ao instanciar sincronizador OFDM.\n";
        reader_running.store(false, std::memory_order_release);
        if (reader_thread.joinable()) reader_thread.join();
        rtlsdr_close(dev);
        return 1;
    }

    OneSegDemodulator demod(OfdmMode::MODE_1_2K, GuardInterval::GI_1_16);
    PsiParser psi;
    std::string channel_name = initial_name;

    constexpr size_t FFT_LEN = 2048;
    std::vector<std::complex<float>> cand_iq_buf(chunk_samples);
    std::vector<std::complex<float>> useful_symbols;
    useful_symbols.reserve(chunk_samples);
    std::uint64_t expected_sequence = 0;
    bool have_expected_sequence = false;
    uint64_t total_ts_pkts = 0;

    auto start_time = std::chrono::steady_clock::now();
    auto last_new_event_time = start_time;
    size_t last_event_count = 0;

    while (g_running) {
        // Interrupcao manual via teclado no terminal
        if (console_kbhit()) {
            int key = console_getch();
            if (key == 'q' || key == 'Q' || key == 27) {
                break;
            }
        }

        auto now = std::chrono::steady_clock::now();
        int elapsed_s = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count());
        if (timeout_s > 0 && elapsed_s >= timeout_s) break;

        SpscCaptureQueue<64>::ReadView view;
        if (!raw_queue->try_acquire_read(view)) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            continue;
        }

        const auto& block = *view.block;
        bool discontinuity = block.discontinuity_before;
        if (have_expected_sequence && block.sequence != expected_sequence) {
            discontinuity = true;
        }
        expected_sequence = block.sequence + 1;
        have_expected_sequence = true;

        if (discontinuity) {
            sync->reset();
            demod.reset();
        }

        if (block.size == chunk_bytes) {
            const uint8_t* raw_data = block.raw.data();
            for (size_t i = 0; i < chunk_samples; ++i) {
                float iv = (static_cast<float>(raw_data[2 * i + 0]) - 127.5f) / 127.5f;
                float qv = (static_cast<float>(raw_data[2 * i + 1]) - 127.5f) / 127.5f;
                cand_iq_buf[i] = std::complex<float>(iv, qv);
            }

            sync->process(cand_iq_buf.data(), chunk_samples, useful_symbols);

            std::vector<std::vector<uint8_t>> decoded_pkts;
            for (size_t s = 0; s + FFT_LEN <= useful_symbols.size(); s += FFT_LEN) {
                demod.process_symbol(&useful_symbols[s], decoded_pkts);
            }

            total_ts_pkts += decoded_pkts.size();
            for (const auto& pkt : decoded_pkts) {
                psi.parse_packet(pkt.data());
            }

            std::string s_name = psi.get_primary_service_name();
            if (!s_name.empty()) {
                channel_name = s_name;
            }

            size_t current_event_count = (psi.get_present_event().has_value() ? 1 : 0) +
                                         (psi.get_following_event().has_value() ? 1 : 0) +
                                         psi.get_schedule_events().size();

            if (current_event_count > last_event_count) {
                last_event_count = current_event_count;
                last_new_event_time = now;
            }

            if (!output_json) {
                std::stringstream ss;
                const char* sync_st = (sync->state() == SyncState::Tracking) ? "LOCK" : (sync->state() == SyncState::Acquisition ? "ACQ" : "LOST");
                if (timeout_s > 0) {
                    ss << "\r[EPG] Coletando guia... (" << elapsed_s << "/" << timeout_s << "s) | Sync: " << sync_st << " | TS: " << total_ts_pkts << " | Eventos: " << current_event_count << "   ";
                } else {
                    ss << "\r[EPG Live] (" << elapsed_s << "s) | Sync: " << sync_st << " | TS: " << total_ts_pkts << " | Eventos: " << current_event_count << " (Pressione 'q' para sair)   ";
                }
                std::cout << ss.str() << std::flush;
            }

            // Conclusao antecipada quando possuirmos Agora, Seguir e Schedule estabilizados
            if (timeout_s > 0 &&
                psi.get_present_event().has_value() &&
                psi.get_following_event().has_value() &&
                !psi.get_schedule_events().empty() &&
                elapsed_s >= 6 &&
                std::chrono::duration_cast<std::chrono::seconds>(now - last_new_event_time).count() >= 4) {
                break;
            }
        }

        raw_queue->release_read(view);
    }

    reader_running.store(false, std::memory_order_release);
    if (reader_thread.joinable()) {
        reader_thread.join();
    }

    rtlsdr_reset_buffer(dev);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    rtlsdr_close(dev);

    if (!output_json) {
        std::cout << "\r                                                                                                   \r";
    }

    auto pres = psi.get_present_event();
    auto foll = psi.get_following_event();
    const auto& sched = psi.get_schedule_events();

    std::vector<EpgEvent> all_live_events;
    if (pres) all_live_events.push_back(*pres);
    if (foll) all_live_events.push_back(*foll);
    for (const auto& s : sched) all_live_events.push_back(s);

    // Enriquece cumulativamente no EpgManager mantendo grade anterior e atualizando sinopses
    bool signal_found = (total_ts_pkts > 0 || sync->state() == SyncState::Tracking);
    epg_mgr.enrich_channel_events(active_channel_num, channel_name, freq_hz,
                                 signal_found, all_live_events, now_tp);
    epg_mgr.save();

    if (output_json) {
        std::cout << epg_mgr.to_json_string(active_channel_num) << "\n";
        return 0;
    }

    const auto* enriched_ch = epg_mgr.get_channel(active_channel_num);
    if (!enriched_ch || enriched_ch->events.empty()) {
        std::cout << "Nenhum evento de programacao (EIT) foi transmitido pela emissora dentro do tempo limite.\n\n";
        return 0;
    }

    return render_epg_interactive_single(active_channel_num, channel_name, enriched_ch->events, now_tp);
}

// ---------------------------------------------------------------------------
// MODO 3: VISUALIZAÇÃO OFFLINE DE EPG VIA --view (SEM ACESSO AO HARDWARE)
// ---------------------------------------------------------------------------
static int run_epg_view(int active_channel_num, const std::string& name_query, bool output_json) {
    EpgManager epg_mgr("epg.json");
    if (!epg_mgr.load() || epg_mgr.channels().empty()) {
        if (output_json) {
            std::cout << "{\n  \"total_channels\": 0,\n  \"channels\": [],\n  \"message\": \"Nenhum guia salvo em epg.json. Execute o comando epg para capturar do ar.\"\n}\n";
            return 0;
        }
        std::cout << "[AVISO] Nenhum guia salvo foi encontrado em './epg.json'.\n\n"
                  << "Para capturar a programacao das emissoras do ar pela primeira vez, execute:\n"
                  << "    .\\isdbtb_rx.exe epg\n\n"
                  << "Ou para sintonizar um canal especifico:\n"
                  << "    .\\isdbtb_rx.exe epg -c <numero>  (ex: .\\isdbtb_rx.exe epg -c 18)\n\n";
        return 0;
    }

    auto now_tp = std::chrono::system_clock::now();
    epg_mgr.prune_expired(now_tp);

    ChannelManager ch_mgr;
    const auto& saved_channels = ch_mgr.get_channels();

    // 1. Canal Especifico
    if (active_channel_num > 0 || !name_query.empty()) {
        int target_ch = active_channel_num;
        if (!name_query.empty()) {
            std::string q_upper = name_query;
            std::transform(q_upper.begin(), q_upper.end(), q_upper.begin(), ::toupper);
            for (const auto& [num, ch] : epg_mgr.channels()) {
                std::string c_upper = ch.channel_name;
                std::transform(c_upper.begin(), c_upper.end(), c_upper.begin(), ::toupper);
                if (c_upper.find(q_upper) != std::string::npos) {
                    target_ch = num;
                    break;
                }
            }
            if (target_ch <= 0) {
                const auto* c_info = ch_mgr.find_by_name(name_query);
                if (c_info) target_ch = c_info->channel_number;
            }
        }

        const auto* ch_entry = epg_mgr.get_channel(target_ch);
        if (!ch_entry || ch_entry->events.empty()) {
            if (output_json) {
                std::cout << "{\n  \"channel\": " << target_ch << ",\n  \"events\": [],\n  \"message\": \"Canal sem eventos salvos\"\n}\n";
                return 0;
            }
            std::cout << "[AVISO] Nao ha eventos salvos para o canal " << target_ch << " em './epg.json'.\n"
                      << "Execute '.\\isdbtb_rx.exe epg -c " << target_ch << "' para capturar o guia do ar.\n\n";
            return 0;
        }

        if (output_json) {
            std::cout << epg_mgr.to_json_string(target_ch) << "\n";
            return 0;
        }

        return render_epg_interactive_single(target_ch, ch_entry->channel_name, ch_entry->events, now_tp);
    }

    // 2. Todos os Canais (Visao Consolidada)
    if (output_json) {
        std::cout << epg_mgr.to_json_string() << "\n";
        return 0;
    }

    std::map<int, std::string> channel_names;
    for (const auto& ch : saved_channels) {
        channel_names[ch.channel_number] = ch.name;
    }
    for (const auto& [num, ch] : epg_mgr.channels()) {
        if (channel_names.find(num) == channel_names.end()) {
            channel_names[num] = ch.channel_name;
        }
    }

    std::vector<ChannelSummaryItem> items;
    for (const auto& [num, name] : channel_names) {
        ChannelSummaryItem it;
        it.channel_number = num;
        it.name = name;

        EpgEvent now_evt, next_evt;
        if (epg_mgr.has_valid_now(num, now_tp, &now_evt, &next_evt)) {
            it.prog_str = now_evt.title;
            if (!now_evt.start_time_str.empty()) {
                it.time_str = now_evt.start_time_str;
                if (!now_evt.end_time_str.empty()) it.time_str += " - " + now_evt.end_time_str;
            }
            if (!now_evt.rating.empty()) it.rating_str = now_evt.rating;
            if (!now_evt.genre.empty()) it.genre_str = now_evt.genre;
            it.short_desc = now_evt.short_desc;
            it.extended_desc = now_evt.extended_desc;
        } else {
            const auto* ch_ptr = epg_mgr.get_channel(num);
            if (ch_ptr && !ch_ptr->events.empty()) {
                it.prog_str = "(Sem programa no horario atual)";
                it.short_desc = ch_ptr->events[0].short_desc;
                it.extended_desc = ch_ptr->events[0].extended_desc;
            }
        }
        items.push_back(it);
    }

    return render_epg_interactive_multi("Guia Geral de Programacao Salvo (EPG) - Consulta Local", items, now_tp);
}

// ---------------------------------------------------------------------------
// PONTO DE ENTRADA DO SUBCOMANDO EPG
// ---------------------------------------------------------------------------
int cmd_epg(int channel_number, const std::string& name_query, int dev_idx,
            int timeout_s, bool output_json, bool force_refresh, bool view_only) {
    if (!output_json) {
        print_cli_header();
    }

    if (view_only) {
        return run_epg_view(channel_number, name_query, output_json);
    }

    ChannelManager mgr;
    const auto& saved_channels = mgr.get_channels();

    // MODO PADRAO: Todos os canais salvos em channels.json
    if (channel_number <= 0 && name_query.empty()) {
        if (saved_channels.empty()) {
            if (output_json) {
                std::cout << "{\n  \"total_channels\": 0,\n  \"channels\": [],\n  \"message\": \"Nenhum canal salvo em channels.json. Execute scan primeiro.\"\n}\n";
                return 0;
            }
            std::cout << "[AVISO] Nenhum canal salvo foi encontrado em './channels.json'.\n\n"
                      << "Para descobrir e salvar as emissoras disponiveis na sua regiao, execute:\n"
                      << "    .\\isdbtb_rx.exe scan\n\n"
                      << "Para consultar diretamente o EPG de uma frequencia sem escanear, use:\n"
                      << "    .\\isdbtb_rx.exe epg -c <numero_do_canal>  (ex: .\\isdbtb_rx.exe epg -c 18)\n\n";
            return 0;
        }

        return run_epg_all(saved_channels, dev_idx, timeout_s, output_json, force_refresh);
    }

    // MODO CANAL INDIVIDUAL FOCADO
    int active_channel_num = channel_number;
    std::string channel_name;
    uint32_t freq_hz = 0;

    if (!name_query.empty()) {
        const auto* ch_ptr = mgr.find_by_name(name_query);
        if (ch_ptr != nullptr) {
            active_channel_num = ch_ptr->channel_number;
            channel_name = ch_ptr->name;
            freq_hz = ch_ptr->frequency_hz;
        } else {
            std::cerr << "[ERRO] Canal com nome '" << name_query << "' nao encontrado em channels.json.\n";
            return 1;
        }
    } else if (active_channel_num > 0) {
        for (const auto& ch : saved_channels) {
            if (ch.channel_number == active_channel_num) {
                channel_name = ch.name;
                freq_hz = ch.frequency_hz;
                break;
            }
        }
    }

    if (freq_hz == 0) {
        if (active_channel_num >= 14 && active_channel_num <= 69) {
            freq_hz = static_cast<uint32_t>(active_channel_num * 6000000ULL + 389142857ULL);
        } else {
            std::cerr << "[ERRO] Canal UHF invalido: " << active_channel_num << "\n";
            return 1;
        }
    }

    return run_epg_single(active_channel_num, channel_name, freq_hz, dev_idx, timeout_s, output_json, force_refresh);
}

} // namespace isdbt
