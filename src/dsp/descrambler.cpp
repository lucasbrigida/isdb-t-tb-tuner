#include "dsp/descrambler.hpp"
#include <cstring>

namespace isdbt {

// Sequencia de inicializacao definida por ARIB STD-B31, ABNT NBR 15601 e gr-isdbt:
// Polinomio: 1 + X^14 + X^15
// Estado inicial de 15 bits: [b1..b15] = [1,0,0,1,0,1,0,1,0,0,0,0,0,0,0] -> 0x4A80 (0x00A9 espelhado)
constexpr uint16_t PRBS_INIT_STATE = 0x4A80;

Descrambler::Descrambler() {
    reset();
}

void Descrambler::reset() {
    lfsr_ = PRBS_INIT_STATE;
}

uint8_t Descrambler::clock_byte() {
    uint8_t prbs_byte = 0;
    for (int b = 7; b >= 0; --b) {
        uint16_t bit14 = (lfsr_ >> 0) & 1;
        uint16_t bit15 = (lfsr_ >> 1) & 1;
        uint16_t feedback = bit14 ^ bit15;
        lfsr_ = (lfsr_ >> 1) | (feedback << 14);
        prbs_byte |= (static_cast<uint8_t>(feedback) << b);
    }
    return prbs_byte;
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
        reset();
        packet[0] = 0x47;
    }

    for (size_t i = 1; i < packet_len; ++i) {
        packet[i] ^= clock_byte();
    }

    return true;
}

void Descrambler::process_isdbt_packet(uint8_t* block204, bool reset_prbs, bool sync_at_end) {
    if (!block204) return;

    if (reset_prbs) {
        reset();
    }

    if (sync_at_end) {
        // Exatamente como em gr-isdbt (energy_descrambler_impl.cc linhas 145-157):
        // O byte 203 eh o sync byte (0x47) e os bytes 0..202 sao os dados/paridade
        uint8_t temp[204];
        temp[0] = block204[203];
        for (int i = 0; i < 203; ++i) {
            temp[i + 1] = block204[i] ^ clock_byte();
        }
        clock_byte(); // Avanca 8 clocks no byte de sincronismo
        std::memcpy(block204, temp, 204);
    } else {
        // Bloco ja alinhado com 0x47 no byte 0 e os 203 bytes seguintes embaralhados pelo PRBS
        for (int i = 1; i < 204; ++i) {
            block204[i] ^= clock_byte();
        }
        clock_byte(); // Avanca 8 clocks durante o byte 0x47 do pacote seguinte
    }
}

} // namespace isdbt
