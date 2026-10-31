#include <iostream>
#include <cassert>
#include <cstring>
#include <vector>
#include "dsp/descrambler.hpp"

int main() {
    std::cout << "[TEST] Iniciando Testes Unitarios do Descrambler (Dispersao de Energia)...\n";
    isdbt::Descrambler scrambler;
    isdbt::Descrambler descrambler;

    // Criar superframe com 8 pacotes de 188 bytes
    std::vector<std::vector<uint8_t>> original_packets(8, std::vector<uint8_t>(188));
    for (int p = 0; p < 8; ++p) {
        original_packets[p][0] = (p == 0) ? 0xB8 : 0x47;
        for (int i = 1; i < 188; ++i) {
            original_packets[p][i] = static_cast<uint8_t>((p * 37 + i * 19) & 0xFF);
        }
    }

    // Copiar e scramblar
    auto scrambled_packets = original_packets;
    for (int p = 0; p < 8; ++p) {
        bool ok = scrambler.process_packet(scrambled_packets[p].data(), 188);
        assert(ok);
        // O sync byte deve ter virado 0x47 para o pacote 0
        assert(scrambled_packets[p][0] == 0x47);
        // Os dados devem ser diferentes dos originais devido ao PRBS
        assert(std::memcmp(scrambled_packets[p].data() + 1, original_packets[p].data() + 1, 187) != 0);
    }
    std::cout << "  [PASS] 8 pacotes scramblados com PRBS e sync byte 0xB8 tratado com sucesso\n";

    // Simular o transmissor reinserindo 0xB8 no inicio da superframe transmitida
    scrambled_packets[0][0] = 0xB8;

    // Descramblar no receptor
    for (int p = 0; p < 8; ++p) {
        bool ok = descrambler.process_packet(scrambled_packets[p].data(), 188);
        assert(ok);
        assert(scrambled_packets[p][0] == 0x47);
        // Dados recuperados devem bater byte a byte com o original
        assert(std::memcmp(scrambled_packets[p].data() + 1, original_packets[p].data() + 1, 187) == 0);
    }
    std::cout << "  [PASS] Receptor descramblou perfeitamente os 8 pacotes, restaurando carga util original\n";

    std::cout << "\033[1;32m[SUCESSO] Todos os testes do Descrambler passaram com 100% de precisao!\033[0m\n";
    return 0;
}

