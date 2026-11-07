#include "platform/platform_utils.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <csignal>
#include <thread>
#include <chrono>

#ifdef _WIN32
    #include <windows.h>
    #include <shellapi.h>
    #include <io.h>
    #include <fcntl.h>
    #include <conio.h>
#else
    #include <unistd.h>
    #include <signal.h>
    #include <termios.h>
    #include <sys/select.h>
    #include <sys/ioctl.h>
#endif

namespace isdbt {

std::atomic<bool> g_running{true};

#ifdef _WIN32
static HANDLE g_player_process = NULL;
static HANDLE g_player_job = NULL;
#else
static pid_t g_player_pid = 0;
#endif

void print_cli_header() {
    std::cout <<
"▀█▀ ▄▀▀▀ █▀▀▄ █▀▀▄      ▀▀█▀▀    ▄▀ ▀▀█▀▀ █▀▀▄ \n"
" █   ▀▀▄ █  █ █▀▀▄ ▀▀▀▀   █    ▄▀     █   █▀▀▄ \n"
"▀▀▀ ▀▀▀  ▀▀▀  ▀▀▀         ▀   ▀       ▀   ▀▀▀  \n"
"Repositorio: https://github.com/lucasbrigida/isdb_t_isdb_tb_tuner_player\n"
"Licenca:     GNU General Public License v3.0 (GPL-3.0)\n\n";
}

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

#ifdef _WIN32
static BOOL WINAPI console_ctrl_handler(DWORD sig) {
    if (sig == CTRL_C_EVENT || sig == CTRL_CLOSE_EVENT) {
        g_running = false;
        stop_player();
        return TRUE;
    }
    return FALSE;
}
#endif

void setup_platform() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }

    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#else
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#endif
}

bool launch_mpv(uint16_t udp_port) {
#ifdef _WIN32
    std::string mpv_bin = "tools\\mpv\\mpv.exe";
    std::string conf_dir = "tools\\mpv";

    auto probe_candidates = [&](const std::filesystem::path& base) -> bool {
        std::error_code ec;
        auto is_valid_exe = [&](const std::filesystem::path& p) -> bool {
            return std::filesystem::exists(p, ec) &&
                   std::filesystem::is_regular_file(p, ec) &&
                   std::filesystem::file_size(p, ec) > 1024 * 1024;
        };

        if (sizeof(void*) == 8) {
            auto b64 = base / "tools" / "mpv" / "x64" / "mpv.exe";
            if (is_valid_exe(b64)) {
                mpv_bin = b64.string();
                conf_dir = (base / "tools" / "mpv" / "x64").string();
                return true;
            }
        }
        if (sizeof(void*) == 4) {
            auto b86 = base / "tools" / "mpv" / "x86" / "mpv.exe";
            if (is_valid_exe(b86)) {
                mpv_bin = b86.string();
                conf_dir = (base / "tools" / "mpv" / "x86").string();
                return true;
            }
        }
        // Fallback: se estiver em x64 mas o mpv x64 nao existir ou for stub LFS, usa o x86
        auto b86_fb = base / "tools" / "mpv" / "x86" / "mpv.exe";
        if (is_valid_exe(b86_fb)) {
            mpv_bin = b86_fb.string();
            conf_dir = (base / "tools" / "mpv" / "x86").string();
            return true;
        }
        auto bdef = base / "tools" / "mpv" / "mpv.exe";
        if (is_valid_exe(bdef)) {
            mpv_bin = bdef.string();
            conf_dir = (base / "tools" / "mpv").string();
            return true;
        }
        return false;
    };

    // 1. Tenta a partir do diretorio corrente
    bool found = probe_candidates(std::filesystem::current_path());

    // 2. Se nao encontrou, tenta a partir do diretorio onde esta o executavel
    if (!found) {
        char exe_buf[MAX_PATH];
        if (GetModuleFileNameA(NULL, exe_buf, MAX_PATH) > 0) {
            std::filesystem::path exe_dir = std::filesystem::path(exe_buf).parent_path();
            found = probe_candidates(exe_dir) || probe_candidates(exe_dir.parent_path());
        }
    }

    std::string cmdline = "\"" + mpv_bin + "\" --config-dir=\"" + conf_dir + "\" --force-window=immediate --idle=yes --input-ipc-server=\\\\.\\pipe\\isdbtb_mpv";

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;
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
        std::cerr << "[ERRO] Falha ao iniciar MPV (" << mpv_bin << "). Execute manualmente .\\scripts\\play_mpv.bat\n";
        return false;
    }

    g_player_process = pi.hProcess;
    CloseHandle(pi.hThread);

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
    int ret = std::system(cmd.c_str());
    return (ret == 0);
#endif
}

bool reload_mpv_player(uint16_t udp_port) {
#ifdef _WIN32
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 20; ++attempt) {
        hPipe = CreateFileA(
            "\\\\.\\pipe\\isdbtb_mpv",
            GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );
        if (hPipe != INVALID_HANDLE_VALUE) {
            break;
        }
        if (GetLastError() == ERROR_PIPE_BUSY) {
            WaitNamedPipeA("\\\\.\\pipe\\isdbtb_mpv", 100);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    if (hPipe == INVALID_HANDLE_VALUE) {
        return false;
    }
    std::string cmd = "{\"command\": [\"loadfile\", \"udp://127.0.0.1:" + std::to_string(udp_port) + "\", \"replace\"]}\n";
    DWORD written = 0;
    WriteFile(hPipe, cmd.data(), static_cast<DWORD>(cmd.size()), &written, NULL);
    CloseHandle(hPipe);
    return true;
#else
    (void)udp_port;
    return false;
#endif
}

bool console_kbhit() {
#ifdef _WIN32
    return (_kbhit() != 0);
#else
    struct timeval tv = {0, 0};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0);
#endif
}

int console_getch() {
#ifdef _WIN32
    return _getch();
#else
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    int ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
#endif
}

bool is_interactive_terminal() {
#ifdef _WIN32
    return (_isatty(_fileno(stdin)) != 0 && _isatty(_fileno(stdout)) != 0);
#else
    return (isatty(STDIN_FILENO) != 0 && isatty(STDOUT_FILENO) != 0);
#endif
}

TerminalSize get_terminal_size() {
    TerminalSize sz{80, 24};
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(hOut, &csbi)) {
        sz.width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        sz.height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        sz.width = ws.ws_col;
        sz.height = ws.ws_row;
    }
#endif
    if (sz.width < 20) sz.width = 80;
    if (sz.height < 5) sz.height = 24;
    return sz;
}

} // namespace isdbt
