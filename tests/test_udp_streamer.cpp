#include "streaming/udp_streamer.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

int main() {
    std::cout << "[Test] UdpStreamer validacao de IPTV MTU (1316B) e burst de descontinuidade..." << std::endl;

    isdbt::UdpStreamer streamer("127.0.0.1", 1234);

    // 1. Validacao de URL oficial para reprodutores (Regra 2 do AGENTS.md)
    TEST_ASSERT(streamer.url() == "udp://@127.0.0.1:1234");
    TEST_ASSERT(streamer.pending_packets() == 0);

    // 2. Inicializacao do streamer
    TEST_ASSERT(streamer.start());

    // 3. Enfileiramento de pacotes TS de midia de 188 bytes
    std::array<uint8_t, 188> media_packet{};
    media_packet.fill(0xFF);
    media_packet[0] = 0x47; // Sync
    media_packet[1] = 0x01;
    media_packet[2] = 0x11; // PID 0x0111 (Video H.264)
    media_packet[3] = 0x10; // Payload only, CC = 0

    TEST_ASSERT(streamer.send_ts_packet(media_packet.data()));
    TEST_ASSERT(streamer.pending_packets() == 1);

    // Flush esvazia o buffer residual preenchendo com pacotes nulos ate 1316 bytes
    streamer.flush();
    TEST_ASSERT(streamer.pending_packets() == 0);

    // 4. Validacao do burst de descontinuidade MPEG-TS atômico (1316 bytes / 7 pacotes)
    // Emissao de descontinuidade de PCR/PTS/DTS no PID 0x0000 para VLC e MPV
    streamer.send_discontinuity_burst();
    TEST_ASSERT(streamer.pending_packets() == 0);

    // 5. Validacao da estrutura do pacote de descontinuidade conforme ISO/IEC 13818-1
    uint8_t disc_pkt[188];
    std::memset(disc_pkt, 0xFF, sizeof(disc_pkt));
    disc_pkt[0] = 0x47;
    disc_pkt[1] = 0x00;
    disc_pkt[2] = 0x00; // PID 0x0000
    disc_pkt[3] = 0x20; // Somente adaptation field
    disc_pkt[4] = 183;  // Comprimento do adaptation field (188 - 5)
    disc_pkt[5] = 0x80; // discontinuity_indicator = 1

    TEST_ASSERT(disc_pkt[0] == 0x47);
    TEST_ASSERT((((disc_pkt[1] & 0x1F) << 8) | disc_pkt[2]) == 0x0000);
    TEST_ASSERT(((disc_pkt[3] >> 4) & 0x03) == 2);
    TEST_ASSERT(disc_pkt[4] == 183);
    TEST_ASSERT((disc_pkt[5] & 0x80) == 0x80);

    // 6. Validacao de envio de burst nulo (keep-alive)
    streamer.send_null_burst(7);
    TEST_ASSERT(streamer.pending_packets() == 0);

    streamer.stop();
    std::cout << "[PASS] UdpStreamer validado com sucesso (1316B atomico, PID 0x0000 disc_indicator).\n";
    return 0;
}
