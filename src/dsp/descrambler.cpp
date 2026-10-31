#include "dsp/descrambler.hpp"

namespace isdbt {

// Sequencia de inicializacao definida por ARIB STD-B31 e ABNT NBR 15601:
// Polinomio: 1 + X^14 + X^15
// Estado inicial de 15 bits: [b1..b15] = [1,0,0,1,0,1,0,1,0,0,0,0,0,0,0] -> 0x4A80
constexpr uint16_t PRBS_INIT_STATE = 0x4A80;

Descrambler::Descrambler() {
    reset();
}

void Descrambler::reset() {
    lfsr_ = PRBS_INIT_STATE;
}

bool Descrambler::process_packet(uint8_t* packet, size_t packet_len) {
    if (!packet || packet_len < 4) return false;

    uint8_t sync = packet[0];
    bool is_inverted = (sync == 0xB8);
    bool is_regular  = (sync == 0x47);

    if (!is_inverted && !is_regular) {
        return false;
    }

    if (is_inverted) {
        // Inicio de superframing de 8 pacotes: reseta PRBS e restaura sync byte para 0x47
        reset();
        packet[0] = 0x47;
    }

    // O sync byte (byte 0) nao eh modificado pelo PRBS
    // O PRBS atua a partir do byte 1 ate packet_len - 1
    for (size_t i = 1; i < packet_len; ++i) {
        uint8_t prbs_byte = 0;
        // Gera 8 bits do PRBS, MSB first
        for (int b = 7; b >= 0; --b) {
            // Feedback = bit 14 ^ bit 15 (bits 0-indexed: bit 0 e bit 1 no LSB)
            // No LFSR de 15 bits: b14 = (lfsr_ >> 0) & 1, b15 = (lfsr_ >> 1) & 1
            uint16_t bit14 = (lfsr_ >> 0) & 1;
            uint16_t bit15 = (lfsr_ >> 1) & 1;
            uint16_t feedback = bit14 ^ bit15;

            // Desloca para a direita e insere o feedback no MSB (bit 14)
            lfsr_ = (lfsr_ >> 1) | (feedback << 14);

            prbs_byte |= (static_cast<uint8_t>(feedback) << b);
        }
        packet[i] ^= prbs_byte;
    }

    return true;
}

} // namespace isdbt

