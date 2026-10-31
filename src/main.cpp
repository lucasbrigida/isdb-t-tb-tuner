#include <iostream>
#include <iomanip>
#include <vector>
#include <complex>
#include <atomic>
#include <thread>
#include <chrono>
#include <csignal>
#include <algorithm>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <filesystem>

#ifdef _WIN32
    #include <windows.h>
    #include <shellapi.h>
    #include <io.h>
    #include <fcntl.h>
#endif

#include "CLI11.hpp"
#include "json.hpp"
#include <rtl-sdr.h>
#include <fftw3.h>
#include <volk/volk.h>
#include <volk/volk_cpu.h>

#include "dsp/reed_solomon.hpp"
#include "dsp/descrambler.hpp"
#include "dsp/viterbi_decoder.hpp"
#include "dsp/ofdm_sync.hpp"
#include "dsp/oneseg_demodulator.hpp"
#include "streaming/udp_streamer.hpp"
#include "mpegts/psi_parser.hpp"
#include "mpegts/ts_generator.hpp"
#include "cli/channel_manager.hpp"

// Taxa de amostragem padrao exata de 1/4 da taxa base de chip ISDB-T (512 / (63 * 4) MHz)
constexpr uint32_t ISDBT_SAMPLE_RATE = 2031746;

std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running = false;
}

void setup_platform() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
    SetConsoleCtrlHandler([](DWORD sig) -> BOOL {
        if (sig == CTRL_C_EVENT || sig == CTRL_CLOSE_EVENT) {
            g_running = false;
            return TRUE;
        }
        return FALSE;
    }, TRUE);
#else
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#endif
}

// Frequencia central do canal UHF ISDB-T / ISDB-Tb (Brasil / Internacional / Japao)
// Canal 14 Brasil = 473.142857 MHz (Canal N = 470 + (N-14)*6 + 3 + 1/7 MHz)
uint32_t get_channel_freq(int channel_number) {
    double freq_mhz = 470.0 + (channel_number - 14) * 6.0 + 3.0 + (1.0 / 7.0);
    return static_cast<uint32_t>(freq_mhz * 1e6);
}

// Converte o nivel dBFS em uma descricao simples e intuitiva de forca de sinal para leigos
struct SignalRating {
    std::string text;       // "Alto (Forte)", "Normal (Bom)", "Fraco"
    std::string ansi_color; // Verde, Ciano, Amarelo, Vermelho
    std::string bar;        // "[#####]", "[#### ]", etc.
};

SignalRating get_signal_rating(float rssi_dbfs) {
    if (rssi_dbfs >= -6.0f) {
        return {"Alto (Forte)",   "\033[1;32m", "[#####]"};
    } else if (rssi_dbfs >= -12.0f) {
        return {"Normal (Bom)",   "\033[1;36m", "[#### ]"};
    } else if (rssi_dbfs >= -18.0f) {
        return {"Moderado",       "\033[1;33m", "[###  ]"};
    } else if (rssi_dbfs >= -25.0f) {
        return {"Fraco",          "\033[1;35m", "[##   ]"};
    } else {
        return {"Sem Sinal",      "\033[1;30m", "[     ]"};
    }
}

// Funcao auxiliar para alinhar colunas visuais no terminal ignorando caracteres de escape ANSI
std::string pad_string(const std::string& str, size_t width, bool left_align = true) {
    if (str.length() >= width) return str.substr(0, width);
    if (left_align) {
        return str + std::string(width - str.length(), ' ');
    } else {
        return std::string(width - str.length(), ' ') + str;
    }
}

int cmd_probe() {
    std::cout << "\033[1;36m=== Inspecao de Hardware RTL-SDR e Aceleracao DSP ===\033[0m\n\n";

    uint32_t count = rtlsdr_get_device_count();
    std::cout << "[RTL-SDR] Dispositivos conectados: \033[1;33m" << count << "\033[0m\n";
    if (count == 0) {
        std::cout << "  \033[1;31m[AVISO] Nenhum RTL-SDR detectado. Verifique o cabo USB e o driver WinUSB (Zadig).\033[0m\n";
        return 1;
    }

    for (uint32_t i = 0; i < count; ++i) {
        char mfg[256] = {0}, prod[256] = {0}, sn[256] = {0};
        rtlsdr_get_device_usb_strings(i, mfg, prod, sn);
        std::cout << "  - Dongle #" << i << ": \033[1;32m" << rtlsdr_get_device_name(i) << "\033[0m\n";
        std::cout << "    Fabricante: " << mfg << " | Serial: " << sn << "\n";

        rtlsdr_dev_t* dev = nullptr;
        if (rtlsdr_open(&dev, i) == 0) {
            int gains[100];
            int n_gains = rtlsdr_get_tuner_gains(dev, gains);
            std::cout << "    Ganhos de RF suportados (" << n_gains << "): ";
            for (int g = 0; g < n_gains && g < 8; ++g) {
                std::cout << (gains[g] / 10.0f) << " ";
            }
            if (n_gains > 8) std::cout << "... " << (gains[n_gains - 1] / 10.0f) << " dB";
            std::cout << "\n";
            rtlsdr_close(dev);
        }
    }

    volk_cpu_init();
    std::cout << "\n[SIMD/DSP] VOLK Versao " << VOLK_VERSION_MAJOR << "." << VOLK_VERSION_MINOR << "." << VOLK_VERSION_MAINT << "\n";
    std::cout << "  - Aceleracao AVX2: " << (volk_cpu.has_avx2() ? "\033[1;32mATIVADA\033[0m" : "NAO") << "\n";
    std::cout << "  - Aceleracao FMA: " << (volk_cpu.has_fma() ? "\033[1;32mATIVADA\033[0m" : "NAO") << "\n";
    std::cout << "  - FFTW3: 2048/4096/8192 pontos inicializados\n\n";

    return 0;
}

// Sintoniza com seguranca a frequencia central no RTL-SDR sem colidir endpoints USB (LIBUSB_ERROR_PIPE -9)
// e aguarda a estabilizacao do sintetizador PLL do tuner Rafael Micro R820T/R828D.
static bool safe_set_freq(rtlsdr_dev_t* dev, uint32_t freq_hz) {
    rtlsdr_reset_buffer(dev);
    int r = rtlsdr_set_center_freq(dev, freq_hz);
    std::this_thread::sleep_for(std::chrono::milliseconds(80)); // Estabilizacao do PLL do tuner R820T
    rtlsdr_reset_buffer(dev);
    return (r == 0);
}

struct CandidateChannel {
    int ch = 0;
    uint32_t freq_hz = 0;
    float rssi_dbfs = 0.0f;
    float peak_corr = 0.0f;
    isdbt::GuardInterval detected_gi = isdbt::GuardInterval::GI_1_16;
    std::string name;
    uint16_t service_id = 0;
};

int cmd_scan(int start_ch, int end_ch, int dev_idx) {
    std::cout << "\033[1;36m========================================================\033[0m\n";
    std::cout << "\033[1;36m  VARREDURA PROFUNDA DE FREQUENCIAS ISDB-T / ISDB-Tb    \033[0m\n";
    std::cout << "\033[1;36m========================================================\033[0m\n";
    std::cout << "Faixa: Canais UHF " << start_ch << " a " << end_ch << " | Fs = " << ISDBT_SAMPLE_RATE << " S/s\n\n";

    rtlsdr_dev_t* dev = nullptr;
    if (rtlsdr_open(&dev, static_cast<uint32_t>(dev_idx)) != 0) {
        std::cerr << "\033[1;31m[ERRO] Falha ao abrir dispositivo RTL-SDR #" << dev_idx << "\033[0m\n";
        return 1;
    }

    rtlsdr_set_sample_rate(dev, ISDBT_SAMPLE_RATE);
    // Usa ganho fixo calibrado (28.0 dB) sem AGC para medir com precisao o RF real
    // e evitar falsos positivos onde o AGC amplifica ruido termico a 0 dBFS
    rtlsdr_set_tuner_gain_mode(dev, 1);
    rtlsdr_set_tuner_gain(dev, 280);
    rtlsdr_set_agc_mode(dev, 0);

    isdbt::ChannelManager mgr;
    mgr.clear();
    isdbt::OfdmSync sync(isdbt::OfdmMode::MODE_1_2K, isdbt::GuardInterval::GI_1_16, static_cast<float>(ISDBT_SAMPLE_RATE));

    size_t chunk_samples = 65536;
    std::vector<uint8_t> raw_buf(chunk_samples * 2);
    std::vector<std::complex<float>> iq_buf(chunk_samples);

    std::vector<CandidateChannel> candidates;

    std::cout << "\033[1;36m[Passo 1/2] Triagem rapida de sinal de RF nos canais UHF...\033[0m\n";
    std::cout << " Canal |  Frequencia  |    Forca do Sinal    |  Potencia  | Status OFDM | Status\n";
    std::cout << "-------+--------------+----------------------+------------+-------------+--------------------\n";

    for (int ch = start_ch; ch <= end_ch && g_running; ++ch) {
        uint32_t freq_hz = get_channel_freq(ch);
        safe_set_freq(dev, freq_hz);

        int n_read = 0;
        rtlsdr_read_sync(dev, raw_buf.data(), static_cast<int>(raw_buf.size()), &n_read);

        float pwr_sum = 0.0f;
        for (size_t i = 0; i < chunk_samples; ++i) {
            float iv = (static_cast<float>(raw_buf[2 * i + 0]) - 127.5f) / 127.5f;
            float qv = (static_cast<float>(raw_buf[2 * i + 1]) - 127.5f) / 127.5f;
            iq_buf[i] = std::complex<float>(iv, qv);
            pwr_sum += (iv * iv + qv * qv);
        }
        float rssi_dbfs = 10.0f * std::log10((pwr_sum / chunk_samples) + 1e-12f);

        auto sync_res = sync.find_symbol_boundary(iq_buf.data(), iq_buf.size());
        auto rating = get_signal_rating(rssi_dbfs);

        // Criterio deterministico e imune a ruido ambiente (-17 dBFS):
        // Potencia de RF legitima acima do piso (>= -13 dBFS) E correlacao de prefixo ciclico (>= 0.60)
        bool is_locked = (rssi_dbfs >= -13.0f) && (sync_res.peak_correlation >= 0.60f);

        if (is_locked) {
            CandidateChannel cand;
            cand.ch = ch;
            cand.freq_hz = freq_hz;
            cand.rssi_dbfs = rssi_dbfs;
            cand.peak_corr = sync_res.peak_correlation;
            cand.detected_gi = sync_res.detected_gi;
            cand.name = "Canal " + std::to_string(ch);
            candidates.push_back(cand);
        }

        std::string raw_signal = rating.bar + " " + rating.text;
        std::string colored_signal = rating.ansi_color + pad_string(raw_signal, 20) + "\033[0m";

        std::string raw_status = is_locked ? "SINAL LOCK" : "Ruido";
        std::string colored_status = (is_locked ? "\033[1;32m" : "\033[1;30m") + pad_string(raw_status, 11) + "\033[0m";

        std::string raw_action = is_locked ? "Candidato (SDT pendente)" : "--";
        std::string colored_action = is_locked ? ("\033[1;33m" + raw_action + "\033[0m") : raw_action;

        std::stringstream freq_ss;
        freq_ss << std::fixed << std::setprecision(3) << (freq_hz / 1e6) << " MHz";

        std::stringstream pwr_ss;
        pwr_ss << std::fixed << std::setprecision(1) << rssi_dbfs << " dBFS";

        std::cout << " " << std::setw(5) << ch << " | "
                  << std::setw(12) << freq_ss.str() << " | "
                  << colored_signal << " | "
                  << std::setw(10) << pwr_ss.str() << " | "
                  << colored_status << " | "
                  << colored_action << "\n";
    }

    if (candidates.empty() || !g_running) {
        rtlsdr_close(dev);
        if (candidates.empty()) {
            std::cout << "\n\033[1;33mNenhum sinal digital de TV detectado na faixa de canais UHF " << start_ch << " a " << end_ch << ".\033[0m\n";
            std::cout << "Dica: Verifique se a antena UHF esta conectada e bem posicionada.\n\n";
        }
        return 0;
    }

    // PASSO 2: Resolucao profunda de nome de emissora via SDT/PSI
    std::cout << "\n\033[1;36m[Passo 2/2] Resolvendo nomes de emissoras e servicos via SDT/PSI (PID 0x0011)...\033[0m\n";

    for (auto& cand : candidates) {
        if (!g_running) break;

        std::stringstream freq_ss;
        freq_ss << std::fixed << std::setprecision(3) << (cand.freq_hz / 1e6) << " MHz";
        std::cout << "  -> Canal " << std::setw(2) << cand.ch << " (" << freq_ss.str() << "): sintonizando e lendo SDT (max 3.0s)... " << std::flush;

        safe_set_freq(dev, cand.freq_hz);

        const size_t step_chunk_samples = 32768;
        const size_t step_chunk_bytes = step_chunk_samples * 2;
        std::mutex q_mtx;
        std::condition_variable q_cv;
        std::queue<std::vector<uint8_t>> raw_queue;
        std::atomic<bool> pass2_worker_running{true};

        std::thread reader_thread([&]() {
            while (pass2_worker_running && g_running) {
                std::vector<uint8_t> buf(step_chunk_bytes);
                int n_read = 0;
                int r = rtlsdr_read_sync(dev, buf.data(), static_cast<int>(buf.size()), &n_read);
                if (r < 0 || n_read != static_cast<int>(step_chunk_bytes)) continue;
                {
                    std::lock_guard<std::mutex> lock(q_mtx);
                    if (raw_queue.size() < 64) {
                        raw_queue.push(std::move(buf));
                    }
                }
                q_cv.notify_one();
            }
        });

        constexpr size_t FFT_LEN = 2048;
        size_t GI_LEN = 256;
        if (cand.detected_gi == isdbt::GuardInterval::GI_1_4) GI_LEN = 512;
        else if (cand.detected_gi == isdbt::GuardInterval::GI_1_8) GI_LEN = 256;
        else if (cand.detected_gi == isdbt::GuardInterval::GI_1_16) GI_LEN = 128;
        else if (cand.detected_gi == isdbt::GuardInterval::GI_1_32) GI_LEN = 64;
        size_t SYM_LEN = FFT_LEN + GI_LEN;

        isdbt::OfdmSync scan_sync(isdbt::OfdmMode::MODE_1_2K, cand.detected_gi, static_cast<float>(ISDBT_SAMPLE_RATE));
        isdbt::OneSegDemodulator scan_demod(isdbt::OfdmMode::MODE_1_2K, cand.detected_gi);
        isdbt::PsiParser scan_psi;

        std::vector<std::complex<float>> cand_iq_buf(step_chunk_samples);
        std::vector<std::complex<float>> stream_iq_buf;
        stream_iq_buf.reserve(step_chunk_samples * 2);

        float frac_pos = 0.0f;
        float tracked_ffo_hz = 0.0f;
        bool initial_symbol_found = false;
        std::string detected_name;
        uint16_t detected_service_id = 0;

        auto start_time = std::chrono::steady_clock::now();
        while (g_running) {
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            if (elapsed_ms >= 3000) break;

            std::vector<uint8_t> raw_chunk;
            {
                std::unique_lock<std::mutex> lock(q_mtx);
                q_cv.wait_for(lock, std::chrono::milliseconds(50), [&]() { return !raw_queue.empty() || !g_running; });
                if (!g_running) break;
                if (raw_queue.empty()) continue;
                raw_chunk = std::move(raw_queue.front());
                raw_queue.pop();
            }

            for (size_t i = 0; i < step_chunk_samples; ++i) {
                float iv = (static_cast<float>(raw_chunk[2 * i + 0]) - 127.5f) / 127.5f;
                float qv = (static_cast<float>(raw_chunk[2 * i + 1]) - 127.5f) / 127.5f;
                cand_iq_buf[i] = std::complex<float>(iv, qv);
            }

            auto sync_res = scan_sync.find_symbol_boundary(cand_iq_buf.data(), step_chunk_samples);
            if (!initial_symbol_found) {
                if (sync_res.locked) {
                    initial_symbol_found = true;
                    GI_LEN = static_cast<size_t>(scan_sync.gi_size());
                    SYM_LEN = FFT_LEN + GI_LEN;
                    frac_pos = static_cast<float>(sync_res.symbol_offset);
                    tracked_ffo_hz = sync_res.ffo_hz;
                }
            } else {
                if (sync_res.peak_correlation >= 0.40f) {
                    const float spacing = static_cast<float>(ISDBT_SAMPLE_RATE) / static_cast<float>(FFT_LEN);
                    float residual = sync_res.ffo_hz - tracked_ffo_hz;
                    residual -= spacing * std::round(residual / spacing);
                    tracked_ffo_hz += 0.05f * residual;
                }
            }

            scan_sync.correct_frequency_offset(cand_iq_buf.data(), step_chunk_samples, tracked_ffo_hz);
            stream_iq_buf.insert(stream_iq_buf.end(), cand_iq_buf.begin(), cand_iq_buf.end());

            if (initial_symbol_found) {
                std::vector<std::vector<uint8_t>> scan_pkts;
                while (static_cast<size_t>(std::lround(frac_pos)) + SYM_LEN + 8 <= stream_iq_buf.size()) {
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
                    if (static_cast<size_t>(sym_d) + SYM_LEN > stream_iq_buf.size()) break;

                    scan_demod.process_symbol(&stream_iq_buf[sym_d + GI_LEN], scan_pkts);
                    frac_pos += static_cast<float>(SYM_LEN);
                }

                int consume_i = static_cast<int>(std::floor(frac_pos)) - 8;
                if (consume_i > 0 && static_cast<size_t>(consume_i) <= stream_iq_buf.size()) {
                    stream_iq_buf.erase(stream_iq_buf.begin(), stream_iq_buf.begin() + consume_i);
                    frac_pos -= static_cast<float>(consume_i);
                }

                for (const auto& pkt : scan_pkts) {
                    scan_psi.parse_packet(pkt.data());
                }

                std::string s_name = scan_psi.get_primary_service_name();
                if (!s_name.empty()) {
                    detected_name = s_name;
                    if (!scan_psi.get_services().empty()) {
                        detected_service_id = scan_psi.get_services().begin()->first;
                    }
                    break;
                }
            }
        }

        pass2_worker_running = false;
        q_cv.notify_all();
        if (reader_thread.joinable()) {
            reader_thread.join();
        }
        rtlsdr_reset_buffer(dev);

        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();

        if (!detected_name.empty()) {
            cand.name = detected_name;
            cand.service_id = detected_service_id;
            std::cout << "\033[1;32m[OK] " << cand.name << "\033[0m (" << std::fixed << std::setprecision(1) << (elapsed_ms / 1000.0f) << "s)\n";
        } else {
            std::cout << "\033[1;33m[TIMEOUT] " << cand.name << "\033[0m (" << std::fixed << std::setprecision(1) << (elapsed_ms / 1000.0f) << "s)\n";
        }

        isdbt::ChannelInfo info;
        info.channel_number = cand.ch;
        info.frequency_hz = cand.freq_hz;
        info.rssi_dbfs = cand.rssi_dbfs;
        info.name = cand.name;
        info.service_id = cand.service_id;
        mgr.add_or_update(info);
    }

    rtlsdr_close(dev);
    mgr.save();

    std::cout << "\n\033[1;32mVarredura concluida: " << candidates.size() << " canais detectados e salvos!\033[0m\n";
    std::cout << "Arquivo de configuracao: " << isdbt::ChannelManager::get_default_config_path() << "\n\n";

    std::cout << " Canal |  Frequencia  |    Forca do Sinal    |  Potencia  | Emissora / Servico\n";
    std::cout << "-------+--------------+----------------------+------------+--------------------\n";
    for (const auto& cand : candidates) {
        auto rating = get_signal_rating(cand.rssi_dbfs);
        std::string raw_signal = rating.bar + " " + rating.text;
        std::string colored_signal = rating.ansi_color + pad_string(raw_signal, 20) + "\033[0m";

        std::stringstream freq_ss;
        freq_ss << std::fixed << std::setprecision(3) << (cand.freq_hz / 1e6) << " MHz";

        std::stringstream pwr_ss;
        pwr_ss << std::fixed << std::setprecision(1) << cand.rssi_dbfs << " dBFS";

        std::cout << " " << std::setw(5) << cand.ch << " | "
                  << std::setw(12) << freq_ss.str() << " | "
                  << colored_signal << " | "
                  << std::setw(10) << pwr_ss.str() << " | \033[1;32m"
                  << cand.name << "\033[0m\n";
    }
    std::cout << "\n";

    return 0;
}

int cmd_list() {
    isdbt::ChannelManager mgr;
    const auto& list = mgr.get_channels();

    std::cout << "\033[1;36m=== Lista de Canais ISDB-T Armazenados (" << list.size() << ") ===\033[0m\n\n";
    if (list.empty()) {
        std::cout << "Nenhum canal salvo ainda. Execute 'isdbtb_rx scan' para sintonizar.\n\n";
        return 0;
    }

    std::cout << " Canal |  Frequencia  |    Forca do Sinal    |  Potencia  | Codecs (V/A) | Nome / Emissora\n";
    std::cout << "-------+--------------+----------------------+------------+--------------+----------------\n";
    for (const auto& ch : list) {
        auto rating = get_signal_rating(ch.rssi_dbfs);
        std::string raw_signal = rating.bar + " " + rating.text;
        std::string colored_signal = rating.ansi_color + pad_string(raw_signal, 20) + "\033[0m";

        std::stringstream freq_ss;
        freq_ss << std::fixed << std::setprecision(3) << (ch.frequency_hz / 1e6) << " MHz";

        std::stringstream pwr_ss;
        pwr_ss << std::fixed << std::setprecision(1) << ch.rssi_dbfs << " dBFS";

        std::string codecs = ch.video_codec + "/" + ch.audio_codec;

        std::cout << " " << std::setw(5) << ch.channel_number << " | "
                  << std::setw(12) << freq_ss.str() << " | "
                  << colored_signal << " | "
                  << std::setw(10) << pwr_ss.str() << " | "
                  << std::setw(12) << codecs << " | \033[1;32m"
                  << ch.name << "\033[0m\n";
    }
    std::cout << "\n\033[1;30mLegenda de Forca do Sinal: [#####] Alto (Forte) >= -6 dBFS | [#### ] Normal (Bom) >= -12 dBFS | [###  ] Moderado | [#    ] Fraco\033[0m\n\n";
    return 0;
}

int cmd_export(const std::string& out_path, uint16_t port) {
    isdbt::ChannelManager mgr;
    if (mgr.export_m3u(out_path, port)) {
        std::cout << "\033[1;32m[SUCESSO] Playlist M3U exportada para: " << out_path << "\033[0m\n";
        std::cout << "Para assistir no VLC, execute: vlc " << out_path << "\n\n";
        return 0;
    } else {
        std::cerr << "\033[1;31m[ERRO] Falha ao exportar playlist M3U.\033[0m\n";
        return 1;
    }
}

int cmd_play(int channel_number, const std::string& name_query, uint16_t udp_port, int dev_idx, bool test_pattern = false, double gain_db = 28.0) {
    isdbt::ChannelManager mgr;
    const isdbt::ChannelInfo* target = nullptr;

    if (channel_number > 0) {
        target = mgr.find_by_number(channel_number);
    } else if (!name_query.empty()) {
        target = mgr.find_by_name(name_query);
    }

    uint32_t freq_hz = 0;
    std::string ch_name = "Canal Desconhecido";

    if (target) {
        freq_hz = target->frequency_hz;
        ch_name = target->name;
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

    isdbt::UdpStreamer streamer("127.0.0.1", udp_port);
    if (!streamer.start()) {
        return 1;
    }

    // Envia bloco de teste apenas se explicitamente solicitado (--test-pattern)
    if (test_pattern) {
        isdbt::TsGenerator ts_gen;
        std::vector<std::vector<uint8_t>> standby_packets;
        ts_gen.get_standby_block(standby_packets);
        for (auto &pkt : standby_packets) {
            streamer.send_ts_packet(pkt.data());
        }
        streamer.flush();
    }

    std::cout << "[UDP] Streaming ativo em " << streamer.url() << "\n";
    std::cout << "Abra seu reprodutor de video (ex: VLC) na URL: \033[1;32m" << streamer.url() << "\033[0m\n";
    std::cout << "Pressione \033[1;33mCtrl+C\033[0m para encerrar a transmissao.\n\n";

    rtlsdr_dev_t* dev = nullptr;
    if (rtlsdr_open(&dev, static_cast<uint32_t>(dev_idx)) != 0) {
        std::cerr << "\033[1;31m[ERRO] Falha ao abrir dongle RTL-SDR #" << dev_idx << "\033[0m\n";
        return 1;
    }

    rtlsdr_set_sample_rate(dev, ISDBT_SAMPLE_RATE);
    safe_set_freq(dev, freq_hz);
    rtlsdr_set_tuner_gain_mode(dev, 1); // Ganho manual otimizado
    int gain_tenths = static_cast<int>(std::round(gain_db * 10.0));
    rtlsdr_set_tuner_gain(dev, gain_tenths);
    rtlsdr_set_agc_mode(dev, 0);
    rtlsdr_reset_buffer(dev);

    constexpr size_t FFT_LEN = 2048;
    // O intervalo de guarda (1/4, 1/8, 1/16, 1/32) e detectado automaticamente pelo OfdmSync.
    // Ex: a BAND (Canal 23) usa GI 1/8 -> 2304 amostras por simbolo.
    size_t GI_LEN  = 128;
    size_t SYM_LEN = FFT_LEN + GI_LEN;
    float frac_pos = 0.0f;           // posicao fracionaria do proximo simbolo (rastreio de deriva de clock)
    bool air_stream_started = false; // apos o 1o pacote real, o standby sintetico NUNCA mais e enviado
    auto last_pat_time = std::chrono::steady_clock::now();

    isdbt::OfdmSync sync(isdbt::OfdmMode::MODE_1_2K, isdbt::GuardInterval::GI_1_16, static_cast<float>(ISDBT_SAMPLE_RATE));
    isdbt::OneSegDemodulator demod(isdbt::OfdmMode::MODE_1_2K, isdbt::GuardInterval::GI_1_16);
    isdbt::PsiParser psi;
    isdbt::TsGenerator channel_ts_gen;

    const size_t chunk_samples = 32768;
    const size_t chunk_bytes = chunk_samples * 2;
    std::mutex q_mtx;
    std::condition_variable q_cv;
    std::queue<std::vector<uint8_t>> raw_queue;

    std::thread reader_thread([&]() {
        while (g_running) {
            std::vector<uint8_t> buf(chunk_bytes);
            int n_read = 0;
            int r = rtlsdr_read_sync(dev, buf.data(), static_cast<int>(buf.size()), &n_read);
            if (r < 0 || n_read != static_cast<int>(chunk_bytes)) continue;
            {
                std::lock_guard<std::mutex> lock(q_mtx);
                if (raw_queue.size() < 64) {
                    raw_queue.push(std::move(buf));
                }
            }
            q_cv.notify_one();
        }
    });

    std::vector<std::complex<float>> iq_buf(chunk_samples);
    std::vector<std::complex<float>> stream_iq_buf;
    stream_iq_buf.reserve(chunk_samples * 2);

    bool ofdm_locked = false;
    float tracked_ffo_hz = 0.0f;
    float last_peak_corr = 0.0f;
    int unlock_counter = 0;

    uint64_t total_air_packets = 0;
    uint64_t total_standby_packets = 0;
    auto last_time = std::chrono::steady_clock::now();

    while (g_running) {
        std::vector<uint8_t> raw_buf;
        {
            std::unique_lock<std::mutex> lock(q_mtx);
            q_cv.wait_for(lock, std::chrono::milliseconds(100), [&]() { return !raw_queue.empty() || !g_running; });
            if (!g_running) break;
            if (raw_queue.empty()) continue;
            raw_buf = std::move(raw_queue.front());
            raw_queue.pop();
        }

        float pwr_sum = 0.0f;
        for (size_t i = 0; i < chunk_samples; ++i) {
            float iv = (static_cast<float>(raw_buf[2 * i + 0]) - 127.5f) / 127.5f;
            float qv = (static_cast<float>(raw_buf[2 * i + 1]) - 127.5f) / 127.5f;
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

                // symbol_offset aponta para o inicio da parte util; recuamos GI_LEN para alinhar
                // o inicio de stream_iq_buf exatamente com o prefixo ciclico do primeiro simbolo
                size_t gi_start = (sync_res.symbol_offset >= static_cast<int>(GI_LEN))
                                  ? static_cast<size_t>(sync_res.symbol_offset - static_cast<int>(GI_LEN))
                                  : 0;
                stream_iq_buf.assign(iq_buf.begin() + gi_start, iq_buf.end());
            }
        } else {
            if (!sync_res.locked) {
                unlock_counter++;
                if (unlock_counter > 200) { // ~3.2 segundos de perda real de sinal
                    ofdm_locked = false;
                    stream_iq_buf.clear();
                    demod.reset();
                }
            } else {
                unlock_counter = 0;
                // Rastreamento suave do FFO com DESEMBRULHO de fase: a estimativa do CP so e valida
                // em +/- meia subportadora (~496 Hz). Se o desvio real estiver perto de 0.5 bin,
                // a estimativa alterna entre +490 e -490 Hz; media direta daria ~0 Hz (errado!).
                const float spacing = static_cast<float>(ISDBT_SAMPLE_RATE) / static_cast<float>(FFT_LEN);
                float residual = sync_res.ffo_hz - tracked_ffo_hz;
                residual -= spacing * std::round(residual / spacing);
                tracked_ffo_hz += 0.1f * residual;
            }

            if (ofdm_locked) {
                // Desrotaciona o novo bloco mantendo continuidade de fase NCO
                sync.correct_frequency_offset(iq_buf.data(), iq_buf.size(), tracked_ffo_hz);
                stream_iq_buf.insert(stream_iq_buf.end(), iq_buf.begin(), iq_buf.end());
            }
        }

        if (ofdm_locked) {
            // Processa todos os simbolos completos acumulados, rastreando suavemente o pico do
            // prefixo ciclico (+/-4 amostras, ganho 0.25) para compensar a deriva de clock do dongle
            while (static_cast<size_t>(std::lround(frac_pos)) + SYM_LEN + 8 <= stream_iq_buf.size()) {
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
                if (static_cast<size_t>(sym_d) + SYM_LEN > stream_iq_buf.size()) break;

                demod.process_symbol(&stream_iq_buf[sym_d + GI_LEN], decoded_pkts);
                frac_pos += static_cast<float>(SYM_LEN);
            }

            int consume_i = static_cast<int>(std::floor(frac_pos)) - 8;
            if (consume_i > 0 && static_cast<size_t>(consume_i) <= stream_iq_buf.size()) {
                stream_iq_buf.erase(stream_iq_buf.begin(), stream_iq_buf.begin() + consume_i);
                frac_pos -= static_cast<float>(consume_i);
            }

            for (const auto& pkt : decoded_pkts) {
                psi.parse_packet(pkt.data());
            }

            if (!decoded_pkts.empty()) {
                air_stream_started = true;
                // Em recepcao parcial 1-Seg o PMT vem no PID 0x1FC8 sem PAT correspondente:
                // injeta uma PAT sintetica (~10x/s) apontando para ele, para o VLC achar o programa
                auto now_pat = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now_pat - last_pat_time).count() >= 100) {
                    uint8_t pat_pkt[188];
                    if (psi.build_pat_packet(pat_pkt)) {
                        streamer.send_ts_packet(pat_pkt);
                    }
                    last_pat_time = now_pat;
                }
                // Envia todos os pacotes MPEG-TS reais decodificados do ar para o VLC (em blocos de 7x188 = 1316B)
                for (const auto& pkt : decoded_pkts) {
                    streamer.send_ts_packet(pkt.data());
                    total_air_packets++;
                }
                streamer.flush();
            } else if (test_pattern) {
                // Apenas envia pacotes de teste sinteticos se solicitado com --test-pattern
                std::vector<std::vector<uint8_t>> standby_pkts;
                channel_ts_gen.get_standby_block(standby_pkts);
                for (const auto& pkt : standby_pkts) {
                    streamer.send_ts_packet(pkt.data());
                    total_standby_packets++;
                }
                streamer.flush();
            }
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
            std::stringstream status_ss;
            status_ss << "[Status] RSSI: " << std::fixed << std::setprecision(1) << rssi_dbfs << " dBFS | "
                      << "OFDM: " << (ofdm_locked ? "LOCK" : "SYNC") << " (" << std::setprecision(2) << last_peak_corr << ") | "
                      << "TMCC: " << (demod.is_tmcc_locked() ? "LOCK" : "SYNC") << " | "
                      << "TS: " << total_air_packets << " pkts";
            if (!s_name.empty()) {
                status_ss << " | " << s_name;
            }
            std::cout << "\r" << status_ss.str() << "\033[K" << std::flush;
            last_time = now;
        }
    }

    g_running = false;
    q_cv.notify_all();
    if (reader_thread.joinable()) {
        reader_thread.join();
    }

    std::cout << "\n\n[Encerrando] Fechando dongle RTL-SDR e encerrando stream UDP...\n";
    rtlsdr_close(dev);
    streamer.stop();
    return 0;
}

int main(int argc, char** argv) {
    setup_platform();

    CLI::App app{"\033[1;36mISDB-T / ISDB-Tb Universal SDR Receiver & Player\033[0m"};

    // 1. Subcomando PROBE
    auto sc_probe = app.add_subcommand("probe", "Inspeciona os dongles RTL-SDR conectados e o ambiente DSP");

    // 2. Subcomando SCAN
    int scan_start = 14;
    int scan_end = 45;
    int dev_idx = 0;
    auto sc_scan = app.add_subcommand("scan", "Executa a varredura profunda dos canais UHF globais");
    sc_scan->add_option("-s,--start", scan_start, "Canal inicial (padrao 14)");
    sc_scan->add_option("-e,--end", scan_end, "Canal final (padrao 45)");
    sc_scan->add_option("-d,--device", dev_idx, "Indice do dongle RTL-SDR (padrao 0)");

    // 3. Subcomando LIST
    auto sc_list = app.add_subcommand("list", "Lista os canais sintonizados armazenados em channels.json");

    // 4. Subcomando EXPORT
    std::string export_path = "channels.m3u";
    uint16_t export_port = 1234;
    auto sc_export = app.add_subcommand("export", "Exporta a lista de canais em formato de playlist M3U para VLC/Kodi");
    sc_export->add_option("-o,--output", export_path, "Caminho do arquivo .m3u de saida (padrao channels.m3u)");
    sc_export->add_option("-p,--port", export_port, "Porta UDP de streaming (padrao 1234)");

    // 5. Subcomando PLAY
    int play_ch = 0;
    std::string play_name;
    uint16_t udp_port = 1234;
    bool test_pattern = false;
    double tuner_gain = 28.0;
    auto sc_play = app.add_subcommand("play", "Sintoniza um canal e transmite via UDP para o player");
    sc_play->add_option("-c,--channel", play_ch, "Numero do canal (ex: 14, 28, 30)");
    sc_play->add_option("-n,--name", play_name, "Nome da emissora (ex: GLOBO, RECORD)");
    sc_play->add_option("-p,--port", udp_port, "Porta UDP para transmissao (padrao 1234)");
    sc_play->add_flag("-t,--test-pattern", test_pattern, "Transmite padrao de teste para diagnostico de rede");
    sc_play->add_option("-g,--gain", tuner_gain, "Ganho do sintonizador em dB (padrao: 28.0 dB, evita saturacao)");
    sc_play->add_option("-d,--device", dev_idx, "Indice do dongle RTL-SDR (padrao 0)");

    app.require_subcommand(1); // Exige ao menos um subcomando

    CLI11_PARSE(app, argc, argv);

    if (sc_probe->parsed()) {
        return cmd_probe();
    } else if (sc_scan->parsed()) {
        return cmd_scan(scan_start, scan_end, dev_idx);
    } else if (sc_list->parsed()) {
        return cmd_list();
    } else if (sc_export->parsed()) {
        return cmd_export(export_path, export_port);
    } else if (sc_play->parsed()) {
        return cmd_play(play_ch, play_name, udp_port, dev_idx, test_pattern, tuner_gain);
    }

    return 0;
}
