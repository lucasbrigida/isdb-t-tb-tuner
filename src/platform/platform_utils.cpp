#include "platform/platform_utils.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <csignal>

#ifdef _WIN32
    #include <windows.h>
    #include <shellapi.h>
    #include <io.h>
    #include <fcntl.h>
#else
    #include <unistd.h>
    #include <signal.h>
#endif

namespace isdbt {

std::atomic<bool> g_running{true};

#ifdef _WIN32
static HANDLE g_player_process = NULL;
static HANDLE g_player_job = NULL;
#else
static pid_t g_player_pid = 0;
#endif

void stop_player() {
#ifdef _WIN32
    if (g_player_process != NULL) {
        TerminateProcess(g_player_process, 0);
        CloseHandle(g_player_process);
        g_player_process = NULL;
    }
    if (g_player_job != NULL) {
        CloseHandle(g_player_job);
        g_player_job = NULL;
    }
#else
    if (g_player_pid > 0) {
        kill(g_player_pid, SIGTERM);
        g_player_pid = 0;
    }
#endif
}

static void signal_handler(int) {
    g_running = false;
    stop_player();
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
            stop_player();
            return TRUE;
        }
        return FALSE;
    }, TRUE);
#else
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#endif
}

bool launch_mpv(uint16_t udp_port) {
#ifdef _WIN32
    std::string mpv_bin = "tools\\mpv\\mpv.exe";
    std::string conf_dir = "tools\\mpv";
    if (!std::filesystem::exists(mpv_bin)) {
        auto root = std::filesystem::current_path();
        if (std::filesystem::exists(root / "tools" / "mpv" / "mpv.exe")) {
            mpv_bin = (root / "tools" / "mpv" / "mpv.exe").string();
            conf_dir = (root / "tools" / "mpv").string();
        }
    }
    std::string cmdline = "\"" + mpv_bin + "\" --config-dir=\"" + conf_dir + "\" udp://127.0.0.1:" + std::to_string(udp_port);
    std::cout << "\033[1;35m[MPV] Disparando player com interpolacao em tempo real: " << cmdline << "\033[0m\n";

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> cmd_buf(cmdline.begin(), cmdline.end());
    cmd_buf.push_back('\0');

    BOOL ok = CreateProcessA(
        NULL,
        cmd_buf.data(),
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        NULL,
        &si,
        &pi
    );

    if (!ok) {
        std::cerr << "\033[1;31m[ERRO] Falha ao iniciar MPV (" << mpv_bin << "). Execute manualmente .\\scripts\\play_mpv.bat\033[0m\n";
        return false;
    }

    g_player_process = pi.hProcess;
    CloseHandle(pi.hThread);

    // Vincula o processo do MPV a um Job Object do Windows configurado com JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE.
    // Isso garante pelo proprio kernel do Windows que o MPV sera fechado instantaneamente caso a CLI
    // seja encerrada por Ctrl+C, fechamento de janela do console ou encerramento do processo.
    g_player_job = CreateJobObjectA(NULL, NULL);
    if (g_player_job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_player_job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
        AssignProcessToJobObject(g_player_job, g_player_process);
    }
    return true;
#else
    std::string cmd = "mpv --config-dir=tools/mpv udp://127.0.0.1:" + std::to_string(udp_port) + " &";
    std::cout << "\033[1;35m[MPV] Disparando: " << cmd << "\033[0m\n";
    int ret = std::system(cmd.c_str());
    return (ret == 0);
#endif
}

} // namespace isdbt

