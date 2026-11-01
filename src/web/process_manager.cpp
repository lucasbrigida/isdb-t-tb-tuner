#include "web/process_manager.hpp"
#include <iostream>
#include <sstream>
#include <regex>
#include <chrono>
#include <filesystem>

#ifndef _WIN32
    #include <unistd.h>
    #include <signal.h>
    #include <sys/wait.h>
#endif

namespace isdbt {

ProcessManager::ProcessManager(const std::string& rx_binary_path)
    : rx_binary_path_(rx_binary_path) {
}

ProcessManager::~ProcessManager() {
    stop();
}

std::string ProcessManager::strip_ansi(const std::string& input) {
    static const std::regex ansi_regex("\033\\[[0-9;?]*[a-zA-Z]");
    return std::regex_replace(input, ansi_regex, "");
}

std::string ProcessManager::resolve_rx_binary() const {
    if (!rx_binary_path_.empty() && std::filesystem::exists(rx_binary_path_)) {
        return rx_binary_path_;
    }

#ifdef _WIN32
    char exe_buf[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe_buf, MAX_PATH);
    std::filesystem::path current_dir = std::filesystem::path(exe_buf).parent_path();

    std::vector<std::filesystem::path> candidates = {
        current_dir / "isdbtb_rx.exe",
        std::filesystem::current_path() / "build" / "isdbtb_rx.exe",
        std::filesystem::current_path() / "isdbtb_rx.exe",
        current_dir.parent_path() / "build" / "isdbtb_rx.exe"
    };

    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) {
            return p.string();
        }
    }
    return "isdbtb_rx.exe";
#else
    return "isdbtb_rx";
#endif
}

void ProcessManager::set_rx_path(const std::string& path) {
    rx_binary_path_ = path;
}

std::string ProcessManager::get_rx_path() const {
    return resolve_rx_binary();
}

std::string ProcessManager::get_hls_dir() const {
    if (!hls_dir_.empty()) return hls_dir_;
    return (std::filesystem::current_path() / "build" / "hls").string();
}

void ProcessManager::write_ffmpeg_input(const uint8_t* data, size_t len) {
#ifdef _WIN32
    if (!h_ffmpeg_stdin_write_ || !data || len == 0) return;
    DWORD written = 0;
    WriteFile(h_ffmpeg_stdin_write_, data, static_cast<DWORD>(len), &written, NULL);
#endif
}

std::string ProcessManager::resolve_ffmpeg_binary() const {
#ifdef _WIN32
    char exe_buf[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe_buf, MAX_PATH);
    std::filesystem::path current_dir = std::filesystem::path(exe_buf).parent_path();

    std::vector<std::filesystem::path> candidates = {
        current_dir / "deps" / "ffmpeg" / "bin" / "ffmpeg.exe",
        current_dir.parent_path() / "deps" / "ffmpeg" / "bin" / "ffmpeg.exe",
        std::filesystem::current_path() / "deps" / "ffmpeg" / "bin" / "ffmpeg.exe",
        current_dir / "ffmpeg.exe",
        "ffmpeg.exe"
    };

    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) {
            return p.string();
        }
    }
    return "ffmpeg.exe";
#else
    return "ffmpeg";
#endif
}

bool ProcessManager::start_ffmpeg_hls() {
    stop_ffmpeg_hls();

    std::string ffmpeg_bin = resolve_ffmpeg_binary();
    std::filesystem::path hls_path = get_hls_dir();

    try {
        if (std::filesystem::exists(hls_path)) {
            std::filesystem::remove_all(hls_path);
        }
        std::filesystem::create_directories(hls_path);
    } catch (...) {}

    std::filesystem::path m3u8_file = hls_path / "live.m3u8";
    std::filesystem::path log_file = hls_path / "ffmpeg.log";

    std::stringstream cmd;
    cmd << "\"" << ffmpeg_bin << "\" -y -loglevel info"
        << " -f mpegts -i pipe:0"
        << " -map 0:v:0 -map 0:a:0? -sn"
        << " -c:v copy -c:a aac -b:a 128k -ar 48000"
        << " -f hls -hls_time 1 -hls_list_size 3 -hls_flags delete_segments+split_by_time"
        << " \"" << m3u8_file.generic_string() << "\"";

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    HANDLE h_stdin_read = nullptr;
    if (!CreatePipe(&h_stdin_read, &h_ffmpeg_stdin_write_, &sa, 0)) {
        return false;
    }
    SetHandleInformation(h_ffmpeg_stdin_write_, HANDLE_FLAG_INHERIT, 0);

    h_ffmpeg_log_ = CreateFileA(log_file.string().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput = h_stdin_read;
    si.hStdOutput = h_ffmpeg_log_ ? h_ffmpeg_log_ : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = h_ffmpeg_log_ ? h_ffmpeg_log_ : GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi = {0};
    std::string cmd_str = cmd.str();

    BOOL ok = CreateProcessA(NULL, cmd_str.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(h_stdin_read);

    if (!ok) {
        std::cerr << "[ProcessManager] Falha ao iniciar FFmpeg para HLS: " << ffmpeg_bin << "\n";
        CloseHandle(h_ffmpeg_stdin_write_);
        h_ffmpeg_stdin_write_ = nullptr;
        if (h_ffmpeg_log_) {
            CloseHandle(h_ffmpeg_log_);
            h_ffmpeg_log_ = nullptr;
        }
        return false;
    }
    ffmpeg_process_ = pi.hProcess;
    ffmpeg_thread_ = pi.hThread;
    std::cout << "[ProcessManager] FFmpeg HLS iniciado (via pipe) gerando em: " << m3u8_file.string() << "\n";
    return true;
#else
    return false;
#endif
}

void ProcessManager::stop_ffmpeg_hls() {
#ifdef _WIN32
    if (h_ffmpeg_stdin_write_) {
        CloseHandle(h_ffmpeg_stdin_write_);
        h_ffmpeg_stdin_write_ = nullptr;
    }
    if (ffmpeg_process_) {
        TerminateProcess(ffmpeg_process_, 0);
        WaitForSingleObject(ffmpeg_process_, 1000);
        CloseHandle(ffmpeg_process_);
        ffmpeg_process_ = nullptr;
    }
    if (ffmpeg_thread_) {
        CloseHandle(ffmpeg_thread_);
        ffmpeg_thread_ = nullptr;
    }
    if (h_ffmpeg_log_) {
        CloseHandle(h_ffmpeg_log_);
        h_ffmpeg_log_ = nullptr;
    }
#endif
}

bool ProcessManager::is_running() const {
    return is_running_.load();
}

ProcessStatus ProcessManager::get_status() const {
    std::lock_guard<std::mutex> lock(status_mtx_);
    return current_status_;
}

std::vector<std::string> ProcessManager::get_recent_logs(size_t max_lines) const {
    std::lock_guard<std::mutex> lock(logs_mtx_);
    size_t count = std::min(max_lines, log_buffer_.size());
    std::vector<std::string> res;
    res.reserve(count);
    auto it = log_buffer_.end() - count;
    while (it != log_buffer_.end()) {
        res.push_back(*it++);
    }
    return res;
}

void ProcessManager::parse_line(const std::string& raw_line) {
    std::string line = strip_ansi(raw_line);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
    }
    if (line.empty()) return;

    {
        std::lock_guard<std::mutex> lock(logs_mtx_);
        if (log_buffer_.size() >= 300) {
            log_buffer_.pop_front();
        }
        log_buffer_.push_back(line);
    }

    std::lock_guard<std::mutex> lock(status_mtx_);
    current_status_.last_log = line;

    // Linha de status em tempo real do play:
    // [Status] RSSI: -14.1 dBFS | OFDM: LOCK (0.95) | TMCC: LOCK | TS: 9393 pkts | Xsports HD
    if (line.rfind("[Status]", 0) == 0) {
        // RSSI
        size_t p_rssi = line.find("RSSI: ");
        if (p_rssi != std::string::npos) {
            size_t end_rssi = line.find(" |", p_rssi);
            if (end_rssi != std::string::npos) {
                current_status_.rssi = line.substr(p_rssi + 6, end_rssi - (p_rssi + 6));
            }
        }

        // OFDM
        size_t p_ofdm = line.find("OFDM: ");
        if (p_ofdm != std::string::npos) {
            size_t end_ofdm = line.find(" |", p_ofdm);
            if (end_ofdm != std::string::npos) {
                current_status_.ofdm = line.substr(p_ofdm + 6, end_ofdm - (p_ofdm + 6));
                // Peak corr
                size_t p_paren = current_status_.ofdm.find('(');
                if (p_paren != std::string::npos) {
                    try {
                        current_status_.peak_corr = std::stof(current_status_.ofdm.substr(p_paren + 1));
                    } catch (...) {}
                }
            }
        }

        // TMCC
        size_t p_tmcc = line.find("TMCC: ");
        if (p_tmcc != std::string::npos) {
            size_t end_tmcc = line.find(" |", p_tmcc);
            if (end_tmcc != std::string::npos) {
                current_status_.tmcc = line.substr(p_tmcc + 6, end_tmcc - (p_tmcc + 6));
            }
        }

        // TS pkts
        size_t p_ts = line.find("TS: ");
        if (p_ts != std::string::npos) {
            size_t end_ts = line.find(" pkts", p_ts);
            if (end_ts != std::string::npos) {
                try {
                    current_status_.ts_packets = std::stoull(line.substr(p_ts + 4, end_ts - (p_ts + 4)));
                } catch (...) {}
            }
        }

        // Service Name (opcional, último campo)
        size_t p_last_pipe = line.rfind(" | ");
        if (p_last_pipe != std::string::npos) {
            std::string possible_service = line.substr(p_last_pipe + 3);
            if (possible_service.find("TS:") == std::string::npos &&
                possible_service.find("TMCC:") == std::string::npos) {
                current_status_.service_name = possible_service;
            }
        }
    }
}

void ProcessManager::reader_loop() {
#ifdef _WIN32
    if (!h_read_pipe_) return;

    char buffer[1024];
    std::string accumulator;

    while (is_running_.load()) {
        DWORD bytes_read = 0;
        BOOL success = ReadFile(h_read_pipe_, buffer, sizeof(buffer) - 1, &bytes_read, NULL);
        if (!success || bytes_read == 0) {
            break;
        }
        buffer[bytes_read] = '\0';

        for (DWORD i = 0; i < bytes_read; ++i) {
            char c = buffer[i];
            if (c == '\r' || c == '\n') {
                if (!accumulator.empty()) {
                    parse_line(accumulator);
                    accumulator.clear();
                }
            } else {
                accumulator += c;
            }
        }
    }

    if (!accumulator.empty()) {
        parse_line(accumulator);
    }
#endif

    is_running_ = false;
    {
        std::lock_guard<std::mutex> lock(status_mtx_);
        current_status_.running = false;
        current_status_.mode = "idle";
    }
}

void ProcessManager::stop() {
    stop_ffmpeg_hls();

    if (!is_running_.exchange(false)) {
        return;
    }

#ifdef _WIN32
    if (h_process_) {
        // Encerra o processo filho
        TerminateProcess(h_process_, 0);
        WaitForSingleObject(h_process_, 1000);
        CloseHandle(h_process_);
        h_process_ = nullptr;
    }
    if (h_thread_) {
        CloseHandle(h_thread_);
        h_thread_ = nullptr;
    }
    if (h_read_pipe_) {
        CloseHandle(h_read_pipe_);
        h_read_pipe_ = nullptr;
    }
#endif

    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }

    // Intervalo de segurança para liberação completa do driver RTL-SDR USB pelo SO
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    {
        std::lock_guard<std::mutex> lock(status_mtx_);
        current_status_.running = false;
        current_status_.mode = "idle";
        current_status_.current_channel = 0;
        current_status_.current_name.clear();
        current_status_.rssi = "--";
        current_status_.ofdm = "--";
        current_status_.tmcc = "--";
        current_status_.ts_packets = 0;
    }
}

bool ProcessManager::start_scan(int start_ch, int end_ch, int dev_idx) {
    stop();

    std::string bin = resolve_rx_binary();
    std::stringstream cmd;
    cmd << "\"" << bin << "\" scan -s " << start_ch << " -e " << end_ch << " -d " << dev_idx;

    {
        std::lock_guard<std::mutex> lock(status_mtx_);
        current_status_ = ProcessStatus();
        current_status_.running = true;
        current_status_.mode = "scan";
    }

#ifdef _WIN32
    HANDLE h_write_pipe = nullptr;
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    if (!CreatePipe(&h_read_pipe_, &h_write_pipe, &sa, 0)) {
        return false;
    }
    SetHandleInformation(h_read_pipe_, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = h_write_pipe;
    si.hStdError = h_write_pipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {0};
    std::string cmd_str = cmd.str();

    BOOL ok = CreateProcessA(NULL, cmd_str.data(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    CloseHandle(h_write_pipe);

    if (!ok) {
        CloseHandle(h_read_pipe_);
        h_read_pipe_ = nullptr;
        std::lock_guard<std::mutex> lock(status_mtx_);
        current_status_.running = false;
        current_status_.mode = "idle";
        return false;
    }

    h_process_ = pi.hProcess;
    h_thread_ = pi.hThread;
    is_running_ = true;
    reader_thread_ = std::thread(&ProcessManager::reader_loop, this);
    return true;
#else
    return false;
#endif
}

bool ProcessManager::start_play(int channel_number, const std::string& channel_name, uint16_t udp_port, int dev_idx) {
    if (channel_number <= 0 && channel_name.empty()) {
        std::cerr << "[ProcessManager] Erro: Canal ou nome de emissora deve ser fornecido!\n";
        return false;
    }
    stop();

    std::string bin = resolve_rx_binary();
    std::stringstream cmd;
    cmd << "\"" << bin << "\" play";
    if (channel_number > 0) {
        cmd << " -c " << channel_number;
    } else if (!channel_name.empty()) {
        cmd << " -n \"" << channel_name << "\"";
    }
    cmd << " -p " << udp_port << " -d " << dev_idx;

    {
        std::lock_guard<std::mutex> lock(status_mtx_);
        current_status_ = ProcessStatus();
        current_status_.running = true;
        current_status_.mode = "play";
        current_status_.current_channel = channel_number;
        current_status_.current_name = channel_name;
    }

#ifdef _WIN32
    HANDLE h_write_pipe = nullptr;
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    if (!CreatePipe(&h_read_pipe_, &h_write_pipe, &sa, 0)) {
        return false;
    }
    SetHandleInformation(h_read_pipe_, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = h_write_pipe;
    si.hStdError = h_write_pipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {0};
    std::string cmd_str = cmd.str();

    BOOL ok = CreateProcessA(NULL, cmd_str.data(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    CloseHandle(h_write_pipe);

    if (!ok) {
        CloseHandle(h_read_pipe_);
        h_read_pipe_ = nullptr;
        std::lock_guard<std::mutex> lock(status_mtx_);
        current_status_.running = false;
        current_status_.mode = "idle";
        return false;
    }

    h_process_ = pi.hProcess;
    h_thread_ = pi.hThread;
    is_running_ = true;
    reader_thread_ = std::thread(&ProcessManager::reader_loop, this);

    // Inicia o FFmpeg em segundo plano para remuxar e transcodificar o stream (via pipe) para HLS
    start_ffmpeg_hls();

    return true;
#else
    return false;
#endif
}

} // namespace isdbt

