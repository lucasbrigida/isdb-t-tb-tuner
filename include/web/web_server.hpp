#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <vector>
#include <set>
#include <mutex>
#include <memory>

#include "web/process_manager.hpp"
#include "cli/channel_manager.hpp"
#include "mpegts/psi_parser.hpp"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET socket_t;
    #define INVALID_SOCK INVALID_SOCKET
    #define CLOSE_SOCK closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    typedef int socket_t;
    #define INVALID_SOCK (-1)
    #define CLOSE_SOCK close
#endif

namespace isdbt {

class WebServer {
public:
    WebServer(ProcessManager& proc_mgr, uint16_t port = 8080, const std::string& bind_ip = "0.0.0.0", uint16_t udp_stream_port = 1234);
    ~WebServer();

    bool start();
    void stop();
    bool is_running() const { return is_running_.load(); }
    uint16_t port() const { return port_; }

private:
    void accept_loop();
    void udp_relay_loop();
    void handle_client(socket_t client_sock);

    ProcessManager& proc_mgr_;
    ChannelManager channel_mgr_;
    PsiParser psi_parser_;
    uint16_t port_;
    std::string bind_ip_;
    uint16_t udp_stream_port_;

    std::atomic<bool> is_running_{false};
    socket_t listen_sock_{INVALID_SOCK};
    socket_t udp_sock_{INVALID_SOCK};

    std::thread server_thread_;
    std::thread udp_thread_;

    mutable std::mutex stream_clients_mtx_;
    std::set<socket_t> stream_clients_;
};

} // namespace isdbt

