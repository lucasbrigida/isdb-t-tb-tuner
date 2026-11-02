#include "CLI11.hpp"
#include "platform/platform_utils.hpp"
#include "cli/commands.hpp"

int main(int argc, char** argv) {
    isdbt::setup_platform();

    CLI::App app{"ISDB-T / ISDB-Tb Universal SDR Receiver & Player"};

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

    // 4. Subcomando PLAY
    int play_ch = 0;
    std::string play_name;
    uint16_t udp_port = 1234;
    bool test_pattern = false;
    double tuner_gain = 28.0;
    bool auto_player = false;
    auto sc_play = app.add_subcommand("play", "Sintoniza um canal e transmite via UDP para o player");
    sc_play->add_option("-c,--channel", play_ch, "Numero do canal (ex: 14, 28, 30)");
    sc_play->add_option("-n,--name", play_name, "Nome da emissora (ex: GLOBO, RECORD)");
    sc_play->add_option("-p,--port", udp_port, "Porta UDP para transmissao (padrao 1234)");
    sc_play->add_flag("-t,--test-pattern", test_pattern, "Transmite padrao de teste para diagnostico de rede");
    sc_play->add_option("-g,--gain", tuner_gain, "Ganho do sintonizador em dB (padrao: 28.0 dB, evita saturacao)");
    sc_play->add_flag("--player", auto_player, "Abre automaticamente o player MPV integrado com interpolacao em tempo real");
    sc_play->add_option("-d,--device", dev_idx, "Indice do dongle RTL-SDR (padrao 0)");

    app.require_subcommand(1); // Exige ao menos um subcomando

    CLI11_PARSE(app, argc, argv);

    if (sc_probe->parsed()) {
        return isdbt::cmd_probe();
    } else if (sc_scan->parsed()) {
        return isdbt::cmd_scan(scan_start, scan_end, dev_idx);
    } else if (sc_list->parsed()) {
        return isdbt::cmd_list();
    } else if (sc_play->parsed()) {
        return isdbt::cmd_play(play_ch, play_name, udp_port, dev_idx, test_pattern, tuner_gain, auto_player);
    }

    return 0;
}
