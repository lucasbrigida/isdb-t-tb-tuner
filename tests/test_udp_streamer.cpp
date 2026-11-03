#include "streaming/udp_streamer.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
using test_socket_t = SOCKET;
static constexpr test_socket_t INVALID_TEST_SOCKET = INVALID_SOCKET;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <unistd.h>
using test_socket_t = int;
static constexpr test_socket_t INVALID_TEST_SOCKET = -1;
#endif

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

static int receive_datagram(test_socket_t socket_fd, uint8_t* data, size_t size) {
#ifdef _WIN32
    return recv(socket_fd, reinterpret_cast<char*>(data), static_cast<int>(size), 0);
#else
    return static_cast<int>(recv(socket_fd, data, size, 0));
#endif
}

int main() {
#ifdef _WIN32
    WSADATA wsa{};
    TEST_ASSERT(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
#endif

    test_socket_t receiver = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    TEST_ASSERT(receiver != INVALID_TEST_SOCKET);

#ifdef _WIN32
    DWORD timeout_ms = 2000;
    TEST_ASSERT(setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO,
                           reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms)) == 0);
#else
    timeval timeout{2, 0};
    TEST_ASSERT(setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
#endif

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = 0;
    TEST_ASSERT(bind(receiver, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0);

#ifdef _WIN32
    int local_len = sizeof(local);
#else
    socklen_t local_len = sizeof(local);
#endif
    TEST_ASSERT(getsockname(receiver, reinterpret_cast<sockaddr*>(&local), &local_len) == 0);

    isdbt::UdpStreamer streamer("127.0.0.1", ntohs(local.sin_port));
    TEST_ASSERT(streamer.start());

    std::array<uint8_t, 188> media_packet{};
    media_packet.fill(0xFF);
    media_packet[0] = 0x47;
    media_packet[1] = 0x01;
    media_packet[2] = 0x00; // PID 0x0100
    media_packet[3] = 0x15; // payload only, continuity counter 5
    TEST_ASSERT(streamer.send_ts_packet(media_packet.data()));
    streamer.flush();

    std::array<uint8_t, isdbt::constants::UDP_PACKET_SIZE> datagram{};
    TEST_ASSERT(receive_datagram(receiver, datagram.data(), datagram.size()) ==
                static_cast<int>(datagram.size()));

    streamer.send_discontinuity_burst({0x0100});
    TEST_ASSERT(receive_datagram(receiver, datagram.data(), datagram.size()) ==
                static_cast<int>(datagram.size()));

    const uint8_t* pid0_marker = datagram.data();
    TEST_ASSERT(pid0_marker[0] == 0x47);
    TEST_ASSERT((((pid0_marker[1] & 0x1F) << 8) | pid0_marker[2]) == 0x0000);
    TEST_ASSERT(((pid0_marker[3] >> 4) & 0x03) == 2);
    TEST_ASSERT(pid0_marker[4] == 183 && (pid0_marker[5] & 0x80) != 0);

    const uint8_t* media_marker = datagram.data() + 188;
    TEST_ASSERT(media_marker[0] == 0x47);
    TEST_ASSERT((((media_marker[1] & 0x1F) << 8) | media_marker[2]) == 0x0100);
    TEST_ASSERT(((media_marker[3] >> 4) & 0x03) == 2);
    TEST_ASSERT((media_marker[3] & 0x0F) == 5);
    TEST_ASSERT(media_marker[4] == 183 && (media_marker[5] & 0x80) != 0);

    streamer.stop();
#ifdef _WIN32
    closesocket(receiver);
    WSACleanup();
#else
    close(receiver);
#endif
    std::cout << "[PASS] UDP streamer signals media-PID discontinuities.\n";
    return 0;
}
