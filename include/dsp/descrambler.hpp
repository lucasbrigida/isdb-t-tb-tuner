#pragma once

#include <cstdint>
#include <cstddef>

namespace isdbt {

/**
 * @brief Descrambler de Dispersao de Energia ISDB-T / DVB (PRBS 1 + X^14 + X^15)
 * 
 * Suporta:
 * - Modo ISDB-T nativo (conforme ABNT NBR 15601 / ARIB STD-B31 e gr-isdbt):
 *   Reseta o LFSR no inicio de cada quadro OFDM (frame_begin, a cada 204 simbolos),
 *   aplica XOR nos 203 bytes pos-sync e avanca 8 clocks durante o byte de sync 0x47.
 * - Modo superframe de 8 pacotes (0xB8 / 0x47) para compatibilidade com os testes unitarios.
 */
class Descrambler {
public:
    Descrambler();

    /**
     * @brief Reseta o registrador linear de deslocamento (LFSR) para o estado inicial 0x4A80 (0xA9 invertido).
     */
    void reset();

    /**
     * @brief Avanca o PRBS em 8 bits (1 byte) e retorna o byte pseudoaleatorio gerado.
     */
    uint8_t clock_byte();

    /**
     * @brief Processa um pacote de 188 ou 204 bytes (modo compativel com sync 0xB8/0x47).
     * @param packet Ponteiro para o inicio do pacote (in-place).
     * @param packet_len Comprimento do pacote (188 ou 204 bytes).
     * @return true se o sync byte era valido (0x47 ou 0xB8), false caso contrario.
     */
    bool process_packet(uint8_t* packet, size_t packet_len = 188);

    /**
     * @brief Processa um bloco de 204 bytes no padrao ISDB-T / gr-isdbt (energy_descrambler_impl.cc).
     * @param block204 Bloco de 204 bytes alinhado com 0x47 no byte 0 (ou byte 203 se sync_at_end=true).
     * @param reset_prbs Se true, reinicia o LFSR no inicio deste pacote (inicio de quadro OFDM).
     * @param sync_at_end Se true, move block204[203] (0x47) para block204[0] e descrambla bytes 0..202 em 1..203.
     */
    void process_isdbt_packet(uint8_t* block204, bool reset_prbs = false, bool sync_at_end = false);

private:
    uint16_t lfsr_; // Registrador de 15 bits
};

} // namespace isdbt
