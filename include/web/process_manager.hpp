#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <deque>
#include <functional>

#ifdef _WIN32
    #include <winsock2.h>
    #include <windows.h>
#endif

namespace isdbt {

struct ProcessStatus {
    bool running = false;
    std::string mode = "idle"; // "idle", "scan", "play"
    int current_channel = 0;
    std::string current_name;
    std::string rssi = "--";
    std::string ofdm = "--";
    std::string tmcc = "--";
    uint64_t ts_packets = 0;
    std::string service_name;
    float peak_corr = 0.0f;
    std::string last_log;
};

class ProcessManager {
public:
    explicit ProcessManager(const std::string& rx_binary_path = "");
    ~ProcessManager();

    // Inicia a varredura profunda (isdbtb_rx scan)
    bool start_scan(int start_ch = 14, int end_ch = 45, int dev_idx = 0);

    // Inicia a reprodução de um canal (isdbtb_rx play -c <ch>)
    bool start_play(int channel_number, const std::string& channel_name = "", uint16_t udp_port = 1234, int dev_idx = 0);

    // Encerra imediatamente o processo ativo
    void stop();

    // Retorna o status em tempo real parseado do stdio
    ProcessStatus get_status() const;

    // Retorna as linhas mais recentes de log capturadas do stdio
    std::vector<std::string> get_recent_logs(size_t max_lines = 100) const;

    // Verifica se há processo em execução
    bool is_running() const;

    // Define o caminho para o executável isdbtb_rx
    void set_rx_path(const std::string& path);
    std::string get_rx_path() const;

    // Retorna o diretorio onde os arquivos HLS (.m3u8 e .ts) estao gravados
    std::string get_hls_dir() const;

    // Escreve pacotes MPEG-TS no pipe stdin do FFmpeg para remux HLS
    void write_ffmpeg_input(const uint8_t* data, size_t len);

private:
    void reader_loop();
    void parse_line(const std::string& line);
    static std::string strip_ansi(const std::string& input);
    std::string resolve_rx_binary() const;
    std::string resolve_ffmpeg_binary() const;
    bool start_ffmpeg_hls();
    void stop_ffmpeg_hls();

    std::string rx_binary_path_;
    std::string hls_dir_;
    std::atomic<bool> is_running_{false};

    mutable std::mutex status_mtx_;
    ProcessStatus current_status_;

    mutable std::mutex logs_mtx_;
    std::deque<std::string> log_buffer_;

    std::thread reader_thread_;

#ifdef _WIN32
    HANDLE h_process_{nullptr};
    HANDLE h_thread_{nullptr};
    HANDLE h_read_pipe_{nullptr};
    HANDLE ffmpeg_process_{nullptr};
    HANDLE ffmpeg_thread_{nullptr};
    HANDLE h_ffmpeg_stdin_write_{nullptr};
    HANDLE h_ffmpeg_log_{nullptr};
#else
    pid_t child_pid_{-1};
    int read_fd_{-1};
    pid_t ffmpeg_pid_{-1};
    int ffmpeg_stdin_fd_{-1};
#endif
};

} // namespace isdbt

