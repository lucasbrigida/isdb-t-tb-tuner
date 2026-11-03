#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <chrono>
#include "dsp/constants.hpp"

namespace isdbt {

/**
 * @brief Emissor de Streaming UDP para VLC (Padrao Broadcast IPTV - RFC 2250)
 * 
 * Agrupa exatamente 7 pacotes MPEG-TS de 188 bytes por datagrama UDP:
 *   7 pacotes * 188 bytes = 1316 bytes por datagrama
 * 
 * Justificativa do MTU:
 *   Ethernet MTU = 1500 bytes
 *   Cabecalho IPv4 (20 bytes) + Cabecalho UDP (8 bytes) = 28 bytes de overhead
 *   Espaco util maximo sem fragmentacao no kernel IP: 1500 - 28 = 1472 bytes
 *   Maior multiplo inteiro de 188 bytes: floor(1472 / 188) * 188 = 1316 bytes.
 * 
 * Este formato garante transmissao em tempo real com latencia quase zero (< 300ms)
 * e sem fragmentacao de datagramas na interface de rede local ou Wi-Fi.
 */
class UdpStreamer {
public:
    /**
     * @brief Construtor do emissor UDP.
     * @param host Endereco IP de destino (padrao "127.0.0.1" para loopback local).
     * @param port Porta UDP de escuta do player (padrao 1234).
     */
    UdpStreamer(const std::string& host = "127.0.0.1", uint16_t port = 1234);
    ~UdpStreamer();

    /**
     * @brief Inicializa o socket UDP (com WSAStartup no Windows e criacao de socket DGRAM).
     * @return true se o socket foi aberto com sucesso, false caso contrario.
     */
    bool start();

    /**
     * @brief Descarrega o buffer pendente e encerra o socket UDP.
     */
    void stop();

    /**
     * @brief Enfileira um pacote MPEG-TS de 188 bytes para transmissao.
     * Quando o acumulador interno atinge 7 pacotes (1316 bytes), o datagrama
     * eh enviado atomicamente atraves de sendto().
     * @param ts_packet188 Ponteiro para os 188 bytes do pacote TS (sync byte 0x47).
     * @return true se o pacote foi enfileirado com sucesso.
     */
    bool send_ts_packet(const uint8_t* ts_packet188);

    /**
     * @brief Forca a transmissao imediata de quaisquer pacotes restantes no buffer.
     */
    void flush();

    /**
     * @brief Descarrega o buffer apenas se houver pacotes residuais (1 a 6)
     * e o tempo decorrido desde o ultimo pacote recebido exceder max_age_ms.
     * @param max_age_ms Tempo maximo em milissegundos (padrao 80ms).
     * @return true se um datagrama foi transmitido.
     */
    bool flush_if_stale(uint32_t max_age_ms = 80);

    /**
     * @brief Retorna o numero de pacotes TS atualmente pendentes no buffer.
     */
    size_t pending_packets() const;

    /**
     * @brief Envia um datagrama UDP de 1316 bytes (7 pacotes TS) com a flag de
     *        descontinuidade (discontinuity_indicator = 1) no campo de adaptacao.
     *        Informa o demuxer do MPV e VLC que o relogio de PCR e os timestamps
     *        (PTS/DTS) foram reiniciados devido a troca de canal.
     */
    void send_discontinuity_burst();

    /**
     * @brief Envia pacotes nulos MPEG-TS (PID 0x1FFF) para manter o socket UDP
     *        alimentado (keep-alive) durante a transicao de sintonia.
     * @param packet_count Quantidade de pacotes nulos a enviar (padrao 7 = 1 datagrama).
     */
    void send_null_burst(size_t packet_count = 7);

    /**
     * @brief Retorna a URL completa formatada para abertura direta no VLC Media Player.
     * Exemplo: "udp://@127.0.0.1:1234"
     */
    std::string url() const;

private:
    std::string host_;
    uint16_t port_;
    uintptr_t socket_fd_; // SOCKET no Windows / int no POSIX
    bool is_running_;
    std::chrono::steady_clock::time_point last_packet_time_;

    std::vector<uint8_t> buffer_; // Buffer acumulador de 1316 bytes
    static constexpr size_t TS_PACKET_SIZE = constants::TS_PACKET_SIZE;
    static constexpr size_t PACKETS_PER_UDP = 7;
    static constexpr size_t UDP_PAYLOAD_SIZE = constants::UDP_PACKET_SIZE; // 1316 bytes
};

} // namespace isdbt
