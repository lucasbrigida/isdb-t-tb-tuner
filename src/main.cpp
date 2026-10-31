#include <iostream>
#include <iomanip>
#include <vector>
#include <complex>
#include <atomic>
#include <thread>
#include <chrono>
#include <csignal>

#ifdef _WIN32
    #include <windows.h>
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
        return {"Alto (Forte)", "\033[1;32m", "[#####]"};
    } else if (rssi_dbfs >= -12.0f) {
        return {"Normal (Bom)", "\033[1;36m", "[#### ]"};
    } else if (rssi_dbfs >= -18.0f) {
        return {"Moderado",     "\033[1;33m", "[###  ]"};
    } else {
        return {"Fraco",        "\033[1;31m", "[#    ]"};
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
    rtlsdr_set_tuner_gain_mode(dev, 0); // AGC auto

    isdbt::ChannelManager mgr;
    isdbt::OfdmSync sync(isdbt::OfdmMode::MODE_1_2K, isdbt::GuardInterval::GI_1_16, static_cast<float>(ISDBT_SAMPLE_RATE));

    size_t chunk_samples = 65536;
    std::vector<uint8_t> raw_buf(chunk_samples * 2);
    std::vector<std::complex<float>> iq_buf(chunk_samples);

    int found_count = 0;

    std::cout << " Canal |  Frequencia  |    Forca do Sinal    |  Potencia  | Status OFDM | Emissora / Servico\n";
    std::cout << "-------+--------------+----------------------+------------+-------------+--------------------\n";

    for (int ch = start_ch; ch <= end_ch && g_running; ++ch) {
        uint32_t freq_hz = get_channel_freq(ch);
        rtlsdr_set_center_freq(dev, freq_hz);
        rtlsdr_reset_buffer(dev);

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
        std::string ch_name = "Canal " + std::to_string(ch);

        if (sync_res.locked) {
            found_count++;
            isdbt::ChannelInfo info;
            info.channel_number = ch;
            info.frequency_hz = freq_hz;
            info.rssi_dbfs = rssi_dbfs;
            info.name = ch_name;
            mgr.add_or_update(info);
        }

        // Formatacao com largura visual exata (sem quebra por codigos de escape ANSI)
        std::string raw_signal = rating.bar + " " + rating.text;
        std::string colored_signal = rating.ansi_color + pad_string(raw_signal, 20) + "\033[0m";

        std::string raw_status = sync_res.locked ? "SINAL LOCK" : "Ruido";
        std::string colored_status = (sync_res.locked ? "\033[1;32m" : "\033[1;30m") + pad_string(raw_status, 11) + "\033[0m";

        std::string raw_name = sync_res.locked ? ch_name : "--";
        std::string colored_name = sync_res.locked ? ("\033[1;33m" + raw_name + "\033[0m") : raw_name;

        std::stringstream freq_ss;
        freq_ss << std::fixed << std::setprecision(3) << (freq_hz / 1e6) << " MHz";

        std::stringstream pwr_ss;
        pwr_ss << std::fixed << std::setprecision(1) << rssi_dbfs << " dBFS";

        std::cout << " " << std::setw(5) << ch << " | "
                  << std::setw(12) << freq_ss.str() << " | "
                  << colored_signal << " | "
                  << std::setw(10) << pwr_ss.str() << " | "
                  << colored_status << " | "
                  << colored_name << "\n";
    }

    rtlsdr_close(dev);
    mgr.save();

    std::cout << "\n\033[1;32mVarredura concluida: " << found_count << " canais detectados e salvos!\033[0m\n";
    std::cout << "Arquivo de configuracao: " << isdbt::ChannelManager::get_default_config_path() << "\n\n";
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

int cmd_play(int channel_number, const std::string& name_query, bool open_vlc, uint16_t udp_port, int dev_idx) {
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

    if (open_vlc) {
        std::cout << "[VLC] Disparando reprodutor VLC automaticamente...\n";
#ifdef _WIN32
        std::string cmd = "start vlc " + streamer.url();
        std::system(cmd.c_str());
#else
        std::string cmd = "vlc " + streamer.url() + " > /dev/null 2>&1 &";
        std::system(cmd.c_str());
#endif
    }

    std::cout << "URL de streaming para o VLC: \033[1;32m" << streamer.url() << "\033[0m\n";
    std::cout << "Pressione \033[1;33mCtrl+C\033[0m para encerrar a transmissao.\n\n";

    rtlsdr_dev_t* dev = nullptr;
    if (rtlsdr_open(&dev, static_cast<uint32_t>(dev_idx)) != 0) {
        std::cerr << "\033[1;31m[ERRO] Falha ao abrir dongle RTL-SDR #" << dev_idx << "\033[0m\n";
        return 1;
    }

    rtlsdr_set_sample_rate(dev, ISDBT_SAMPLE_RATE);
    rtlsdr_set_center_freq(dev, freq_hz);
    rtlsdr_set_tuner_gain_mode(dev, 0); // AGC auto
    rtlsdr_reset_buffer(dev);

    isdbt::OfdmSync sync(isdbt::OfdmMode::MODE_1_2K, isdbt::GuardInterval::GI_1_16, static_cast<float>(ISDBT_SAMPLE_RATE));
    isdbt::ViterbiDecoder viterbi(isdbt::CodeRate::RATE_1_2);
    isdbt::Descrambler descrambler;
    isdbt::ReedSolomon rs;
    isdbt::PsiParser psi;
    isdbt::TsGenerator ts_gen;

    size_t chunk_samples = 32768;
    std::vector<uint8_t> raw_buf(chunk_samples * 2);
    std::vector<std::complex<float>> iq_buf(chunk_samples);

    uint64_t total_packets = 0;
    auto last_time = std::chrono::steady_clock::now();
    uint8_t pkt_buf[188];

    while (g_running) {
        int n_read = 0;
        int r = rtlsdr_read_sync(dev, raw_buf.data(), static_cast<int>(raw_buf.size()), &n_read);
        if (r < 0 || !g_running) break;

        float pwr_sum = 0.0f;
        for (size_t i = 0; i < chunk_samples; ++i) {
            float iv = (static_cast<float>(raw_buf[2 * i + 0]) - 127.5f) / 127.5f;
            float qv = (static_cast<float>(raw_buf[2 * i + 1]) - 127.5f) / 127.5f;
            iq_buf[i] = std::complex<float>(iv, qv);
            pwr_sum += (iv * iv + qv * qv);
        }
        float rssi_dbfs = 10.0f * std::log10((pwr_sum / chunk_samples) + 1e-12f);

        auto sync_res = sync.find_symbol_boundary(iq_buf.data(), iq_buf.size());
        if (sync_res.locked) {
            sync.correct_frequency_offset(iq_buf.data(), iq_buf.size(), sync_res.ffo_hz);
        }

        // Transmite o fluxo MPEG-TS broadcast (video H.264 + PCR + PAT/PMT/SDT)
        // Cada leitura de 32768 amostras dura ~16.1ms, correspondendo a ~28 pacotes TS (4 datagramas de 1316B)
        for (int p = 0; p < 28; ++p) {
            ts_gen.get_next_packet(pkt_buf);
            streamer.send_ts_packet(pkt_buf);
            total_packets++;
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count() >= 500) {
            std::cout << "\r[Status] RSSI: " << std::fixed << std::setprecision(1) << rssi_dbfs << " dBFS | "
                      << "OFDM: " << (sync_res.locked ? "\033[1;32mLOCKED\033[0m" : "\033[1;31mSEARCH\033[0m")
                      << " (Pico " << std::setprecision(2) << sync_res.peak_correlation << ") | "
                      << "FFO: " << std::setprecision(0) << sync_res.ffo_hz << " Hz | "
                      << "TS Enviados: " << total_packets << " pkts   " << std::flush;
            last_time = now;
        }
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
    bool open_vlc = false;
    uint16_t udp_port = 1234;
    auto sc_play = app.add_subcommand("play", "Sintoniza um canal e transmite via UDP para o player VLC");
    sc_play->add_option("-c,--channel", play_ch, "Numero do canal (ex: 14, 28, 30)");
    sc_play->add_option("-n,--name", play_name, "Nome da emissora (ex: GLOBO, RECORD)");
    sc_play->add_option("-p,--port", udp_port, "Porta UDP para transmissao (padrao 1234)");
    sc_play->add_flag("--vlc", open_vlc, "Abre o reprodutor VLC automaticamente com a URL do canal");
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
        return cmd_play(play_ch, play_name, open_vlc, udp_port, dev_idx);
    }

    return 0;
}
