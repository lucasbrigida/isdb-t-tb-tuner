#include <iostream>
#include <cassert>
#include <cstring>
#include <vector>
#include "dsp/reed_solomon.hpp"
#include "dsp/descrambler.hpp"
#include "dsp/viterbi_decoder.hpp"

int main() {
    std::cout << "[TEST] Iniciando Teste da Cadeia Completa de FEC (Viterbi + Descrambler + Reed-Solomon)...\n";

    isdbt::ReedSolomon rs;
    isdbt::Descrambler scrambler;
    isdbt::Descrambler descrambler;
    isdbt::ViterbiDecoder viterbi(isdbt::CodeRate::RATE_1_2);

    // 1. Criar pacote MPEG-TS original (188 bytes com sync byte 0x47)
    uint8_t ts_packet[188];
    ts_packet[0] = 0x47;
    for (int i = 1; i < 188; ++i) {
        ts_packet[i] = static_cast<uint8_t>((i * 11 + 3) & 0xFF);
    }

    // 2. Codificacao Reed-Solomon: 188 -> 204 bytes
    uint8_t block204[204];
    std::memcpy(block204, ts_packet, 188);
    rs.encode(ts_packet, block204 + 188);

    // 3. Dispersao de Energia (Scrambler)
    // Para teste de primeiro pacote de superframe, usamos sync byte 0xB8
    block204[0] = 0xB8;
    bool ok = scrambler.process_packet(block204, 204);
    assert(ok);
    block204[0] = 0xB8; // Transmissor envia com 0xB8 no ar

    // 4. Codificacao Convolucional (Inner Code K=7, 1/2)
    std::vector<uint8_t> in_bits;
    for (int i = 0; i < 204; ++i) {
        for (int b = 7; b >= 0; --b) {
            in_bits.push_back((block204[i] >> b) & 1);
        }
    }
    for (int f = 0; f < 6; ++f) in_bits.push_back(0); // flush bits

    std::vector<uint8_t> symbols;
    viterbi.encode(in_bits, symbols);

    // 5. Canal Ruidoso (inversao de 2% dos simbolos transmitidos)
    std::vector<uint8_t> rx_soft(symbols.size());
    for (size_t i = 0; i < symbols.size(); ++i) {
        rx_soft[i] = symbols[i] ? 255 : 0;
        if (i % 45 == 0) {
            rx_soft[i] = 255 - rx_soft[i]; // Ruido de canal
        }
    }

    // 6. Decodificador Viterbi (Receptor)
    std::vector<uint8_t> rx_bytes;
    viterbi.decode(rx_soft, rx_bytes);
    assert(rx_bytes.size() >= 204);

    // 7. Descrambler (Dispersao de Energia no Receptor)
    ok = descrambler.process_packet(rx_bytes.data(), 204);
    assert(ok);
    assert(rx_bytes[0] == 0x47);

    // 8. Decodificador Reed-Solomon (Outer FEC no Receptor)
    int corrected = rs.decode(rx_bytes.data());
    std::cout << "  - Reed-Solomon corrigiu " << corrected << " erros residuais pos-Viterbi\n";
    assert(corrected >= 0);

    // Validacao do pacote MPEG-TS de 188 bytes final
    assert(std::memcmp(rx_bytes.data(), ts_packet, 188) == 0);
    std::cout << "\033[1;32m[SUCESSO] Pacote MPEG-TS de 188 bytes recuperado intacto atraves da cadeia FEC completa!\033[0m\n";

    return 0;
}

