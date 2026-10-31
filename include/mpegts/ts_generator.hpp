#pragma once

#include <cstdint>
#include <vector>

namespace isdbt {

/**
 * @brief Gerador utilitario de tabelas MPEG-TS (PAT e PMT)
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

private:
    uint32_t crc32(const uint8_t* data, size_t len);
};

} // namespace isdbt
