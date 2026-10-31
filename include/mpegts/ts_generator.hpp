#pragma once

#include <cstdint>
#include <vector>

namespace isdbt {

/**
 * @brief Gerador utilitario de tabelas MPEG-TS e gerador de fluxo de standby / padrao de teste
 * com PCR dinamico de 27 MHz e quadros de video H.264 (SPS/PPS/IDR) validos para VLC.
 */
class TsGenerator {
public:
    TsGenerator();

    /**
     * @brief Gera o pacote PAT (PID 0x0000) apontando o Programa 1 para o PMT no PID 0x1000.
     */
    void get_pat_packet(uint8_t* out188, uint8_t continuity_counter);

    /**
     * @brief Gera o pacote PMT (PID 0x1000) definindo:
     * - Stream Type 0x1B (H.264 AVC Video) no PID 0x0100
     * - Stream Type 0x0F (AAC Audio) no PID 0x0101
     */
    void get_pmt_packet(uint8_t* out188, uint8_t continuity_counter);

    /**
     * @brief Gera um bloco completo de pacotes TS de standby/teste (exatamente 7 pacotes = 1316 bytes)
     * contendo PAT, PMT, pacotes de video H.264 com PCR dinamico (27 MHz) e PTS (90 kHz),
     * audio AAC com PTS e pacotes nulos para alinhamento estrito com MTU Ethernet (1316B).
     */
    void get_standby_block(std::vector<std::vector<uint8_t>>& out_packets);

    /**
     * @brief Preenche diretamente um buffer de 1316 bytes (7 * 188) com um bloco de standby.
     */
    void get_standby_datagram(uint8_t* out1316);

    /**
     * @brief Reinicia os contadores de continuidade e o relogio de PCR/PTS.
     */
    void reset();

private:
    uint32_t crc32(const uint8_t* data, size_t len);
    void encode_pcr(uint8_t* out6, uint64_t pcr_ticks_27mhz);
    void encode_pts(uint8_t* out5, uint64_t pts_ticks_90khz);

    uint8_t pat_cc_{0};
    uint8_t pmt_cc_{0};
    uint8_t vid_cc_{0};
    uint8_t aud_cc_{0};
    uint8_t null_cc_{0};
    uint64_t frame_index_{0};
};

} // namespace isdbt
