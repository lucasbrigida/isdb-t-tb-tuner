#include <iostream>
#include <atomic>
#include <csignal>
#include <chrono>
#include <thread>

#ifdef _WIN32
    #include <winsock2.h>
    #include <windows.h>
#endif

#include "CLI11.hpp"
#include "web/process_manager.hpp"
#include "web/web_server.hpp"

static std::atomic<bool> g_server_running{true};

#ifdef _WIN32
static BOOL WINAPI console_handler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT || signal == CTRL_BREAK_EVENT) {
        g_server_running = false;
        return TRUE;
    }
    return FALSE;
}
#else
static void signal_handler(int) {
    g_server_running = false;
}
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
    SetConsoleCtrlHandler(console_handler, TRUE);
#else
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#endif

    CLI::App app{"\033[1;36mISDB-T / ISDB-Tb Web Dashboard & Tuner Server\033[0m"};

    uint16_t port = 8080;
    std::string bind_ip = "0.0.0.0";
    std::string rx_path;
    uint16_t udp_port = 1234;

    app.add_option("-p,--port", port, "Porta TCP do servidor Web (padrao 8080)");
    app.add_option("-b,--bind", bind_ip, "Endereco de IP para bind (padrao 0.0.0.0)");
    app.add_option("--rx-path", rx_path, "Caminho customizado para o executavel isdbtb_rx");
    app.add_option("--udp-port", udp_port, "Porta UDP de streaming local (padrao 1234)");

    CLI11_PARSE(app, argc, argv);

    std::cout << "\033[1;36m==============================================================\033[0m\n";
    std::cout << "\033[1;36m  ISDB-T / ISDB-Tb Web Dashboard & Tuner Server               \033[0m\n";
    std::cout << "\033[1;36m==============================================================\033[0m\n";

    isdbt::ProcessManager proc_mgr(rx_path);
    std::cout << "[SDR Engine] Executavel: \033[1;32m" << proc_mgr.get_rx_path() << "\033[0m\n";

    isdbt::WebServer server(proc_mgr, port, bind_ip, udp_port);
    if (!server.start()) {
        std::cerr << "\033[1;31m[ERRO] Falha ao iniciar o servidor Web na porta " << port << "\033[0m\n";
        return 1;
    }

    std::cout << "\n\033[1;32m>> Painel Web pronto!\033[0m Abra em seu navegador:\n";
    std::cout << "   \033[1;36mhttp://localhost:" << port << "/\033[0m ou \033[1;36mhttp://127.0.0.1:" << port << "/\033[0m\n\n";
    std::cout << "Pressione \033[1;33mCtrl+C\033[0m para encerrar o servidor.\n\n";

    while (g_server_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "\n[Encerrando] Parando processos e desligando servidor Web...\n";
    proc_mgr.stop();
    server.stop();
    std::cout << "[OK] Servidor finalizado com sucesso.\n";

    return 0;
}

