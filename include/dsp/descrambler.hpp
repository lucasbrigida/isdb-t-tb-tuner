#pragma once

#include <cstdint>
#include <cstddef>

namespace isdbt {

/**
 * @brief Descrambler de Dispersao de Energia ISDB-T / DVB (PRBS 1 + X^14 + X^15)
 * 
 * Sincronizado a cada 8 pacotes MPEG-TS pelo sync byte invertido 0xB8.
 * Restaura o sync byte para 0x47 e descrambla os 187 bytes de carga util de cada pacote.
 */
class Descrambler {
public:
    Descrambler();

    /**
     * @brief Reseta o registrador linear de deslocamento (LFSR) para o estado inicial 0x4A80.
     */
    void reset();

    /**
     * @brief Processa um pacote de 188 ou 204 bytes.
     * @param packet Ponteiro para o inicio do pacote (in-place).
     * @param packet_len Comprimento do pacote (188 ou 204 bytes).
     * @return true se o sync byte era valido (0x47 ou 0xB8), false caso contrario.
     */
    bool process_packet(uint8_t* packet, size_t packet_len = 188);

private:
    uint16_t lfsr_; // Registrador de 15 bits
};

} // namespace isdbt

