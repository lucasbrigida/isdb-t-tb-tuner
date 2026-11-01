#include "streaming/udp_streamer.hpp"
#include <iostream>
#include <array>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET socket_t;
    #define INVALID_SOCK INVALID_SOCKET
    #define CLOSE_SOCK closesocket
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    typedef int socket_t;
    #define INVALID_SOCK (-1)
    #define CLOSE_SOCK close
#endif

namespace isdbt {

UdpStreamer::UdpStreamer(const std::string& host, uint16_t port)
    : host_(host), port_(port), socket_fd_(reinterpret_cast<uintptr_t>(INVALID_SOCK)),
      is_running_(false), last_packet_time_(std::chrono::steady_clock::now()) {
    buffer_.reserve(UDP_PAYLOAD_SIZE);
}

UdpStreamer::~UdpStreamer() {
    stop();
}

bool UdpStreamer::start() {
    if (is_running_) return true;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "[UDP] Falha ao inicializar WinSock2\n";
        return false;
    }
#endif

    socket_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCK) {
        std::cerr << "[UDP] Falha ao criar socket UDP\n";
        return false;
    }

    // configurar buffer de envio para 1 MiB
    int send_buf_size = 1024 * 1024;
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&send_buf_size), sizeof(send_buf_size));

    socket_fd_ = static_cast<uintptr_t>(s);
    is_running_ = true;
    buffer_.clear();
    last_packet_time_ = std::chrono::steady_clock::now();

    return true;
}

void UdpStreamer::stop() {
    if (!is_running_) return;
    flush();
    socket_t s = static_cast<socket_t>(socket_fd_);
    if (s != INVALID_SOCK) {
        CLOSE_SOCK(s);
        socket_fd_ = reinterpret_cast<uintptr_t>(INVALID_SOCK);
    }
#ifdef _WIN32
    WSACleanup();
#endif
    is_running_ = false;
}

bool UdpStreamer::send_ts_packet(const uint8_t* ts_packet188) {
    if (!is_running_ || !ts_packet188) return false;
    buffer_.insert(buffer_.end(), ts_packet188, ts_packet188 + TS_PACKET_SIZE);
    last_packet_time_ = std::chrono::steady_clock::now();
    if (buffer_.size() >= UDP_PAYLOAD_SIZE) {
        flush();
    }
    return true;
}

bool UdpStreamer::flush_if_stale(uint32_t max_age_ms) {
    if (!is_running_ || buffer_.empty()) return false;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_packet_time_).count();
    if (elapsed >= max_age_ms) {
        flush();
        return true;
    }
    return false;
}

size_t UdpStreamer::pending_packets() const {
    return buffer_.size() / TS_PACKET_SIZE;
}

void UdpStreamer::flush() {
    if (!is_running_ || buffer_.empty()) return;
    // preencher até 7 pacotes (1316 B) com pacotes nulos (PID 0x1FFF)
    while (buffer_.size() < UDP_PAYLOAD_SIZE) {
        static const uint8_t null_header[4] = { 0x47, 0x1F, 0xFF, 0x10 };
        std::array<uint8_t, TS_PACKET_SIZE - 4> zeros{}; // zero-filled payload
        buffer_.insert(buffer_.end(), std::begin(null_header), std::end(null_header));
        buffer_.insert(buffer_.end(), zeros.begin(), zeros.end());
    }
    socket_t s = static_cast<socket_t>(socket_fd_);
    sockaddr_in dest_addr{};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port   = htons(port_);
    inet_pton(AF_INET, host_.c_str(), &dest_addr.sin_addr);
    sendto(s, reinterpret_cast<const char*>(buffer_.data()), static_cast<int>(buffer_.size()), 0,
           reinterpret_cast<sockaddr*>(&dest_addr), sizeof(dest_addr));
    buffer_.clear();
}

std::string UdpStreamer::url() const {
    return "udp://@" + host_ + ":" + std::to_string(port_);
}

} // namespace isdbt
