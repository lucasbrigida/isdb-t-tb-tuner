#pragma once

#include <cstdint>
#include <vector>
#include <array>

namespace isdbt {

/**
 * @brief Gerador de Pacotes MPEG-TS padrao broadcast (PAT, PMT, H.264 PES e AAC PES)
 * 
 * Permite que o VLC abra imediatamente a janela de video e reproduza o fluxo UDP
 * com tabelas de sinalizacao validas (PID 0 PAT, PID 0x1000 PMT, PID 0x0100 Video).
 */
class TsGenerator {
public:
    TsGenerator();

    /**
     * @brief Obtem o proximo pacote MPEG-TS valido do fluxo broadcast,
     * atualizando dinamicamente os contadores de continuidade (CC) e carimbos de relogio PCR.
     */
    void get_next_packet(uint8_t* out188);

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
     * @brief Gera um frame de video H.264 (com SPS, PPS e slice IDR) encapsulado em TS.
     */
    std::vector<std::vector<uint8_t>> generate_h264_frame_packets(uint32_t frame_num, uint8_t& cc);

    /**
     * @brief Gera um bloco de audio AAC (ADTS) encapsulado em TS.
     */
    std::vector<std::vector<uint8_t>> generate_aac_audio_packets(uint8_t& cc);

private:
    uint32_t crc32(const uint8_t* data, size_t len);

    size_t current_packet_idx_{0};
    uint64_t pcr_val_{0};
    std::array<uint8_t, 8192> cc_table_{};
};

} // namespace isdbt
