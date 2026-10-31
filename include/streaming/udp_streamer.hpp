#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace isdbt {

/**
 * @brief Emissor de Streaming UDP para VLC (Padrao Broadcast IPTV)
 * 
 * Agrupa exatamente 7 pacotes MPEG-TS de 188 bytes por datagrama UDP (1316 bytes).
 * 1316 bytes cabem com folga no MTU de 1500 bytes da rede Ethernet sem fragmentacao.
 */
class UdpStreamer {
public:
    UdpStreamer(const std::string& host = "127.0.0.1", uint16_t port = 1234);
    ~UdpStreamer();

    bool start();
    void stop();

    /**
     * @brief Enfileira e transmite um pacote MPEG-TS de 188 bytes.
     * Quando atinge 7 pacotes (1316 bytes), o datagrama UDP eh enviado imediatamente.
     */
    bool send_ts_packet(const uint8_t* ts_packet188);

    /**
     * @brief Forca o envio de qualquer pacote pendente no buffer acumulador.
     */
    void flush();

    std::string url() const;

private:
    std::string host_;
    uint16_t port_;
    uintptr_t socket_fd_; // SOCKET no Windows / int no POSIX
    bool is_running_;

    std::vector<uint8_t> buffer_; // Buffer acumulador de 1316 bytes
    static constexpr size_t TS_PACKET_SIZE = 188;
    static constexpr size_t PACKETS_PER_UDP = 7;
    static constexpr size_t UDP_PAYLOAD_SIZE = TS_PACKET_SIZE * PACKETS_PER_UDP; // 1316 bytes
};

} // namespace isdbt

