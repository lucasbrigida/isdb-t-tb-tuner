#include <iostream>
#include <cassert>
#include <cstring>
#include <vector>
#include <random>
#include "dsp/viterbi_decoder.hpp"

int main() {
    std::cout << "[TEST] Iniciando Testes Unitarios do Viterbi Decoder (Inner FEC K=7)...\n";

    // Criar mensagem de teste (204 bytes = 1632 bits)
    std::vector<uint8_t> original_bytes(204);
    original_bytes[0] = 0x47;
    for (size_t i = 1; i < 204; ++i) {
        original_bytes[i] = static_cast<uint8_t>((i * 13 + 7) & 0xFF);
    }

    // Converter bytes para bits
    std::vector<uint8_t> in_bits;
    in_bits.reserve(original_bytes.size() * 8 + 6);
    for (uint8_t b : original_bytes) {
        for (int i = 7; i >= 0; --i) {
            in_bits.push_back((b >> i) & 1);
        }
    }
    // Flush bits (6 zeros para retornar o encoder ao estado 0)
    for (int i = 0; i < 6; ++i) in_bits.push_back(0);

    // 1. Teste Taxa 1/2 sem ruido
    {
        isdbt::ViterbiDecoder viterbi(isdbt::CodeRate::RATE_1_2);
        std::vector<uint8_t> symbols;
        viterbi.encode(in_bits, symbols);

        // Mapear simbolos discretos {0, 1} para soft metrics {0, 255}
        std::vector<uint8_t> soft_in(symbols.size());
        for (size_t i = 0; i < symbols.size(); ++i) {
            soft_in[i] = symbols[i] ? 255 : 0;
        }

        std::vector<uint8_t> decoded_bytes;
        viterbi.decode(soft_in, decoded_bytes);

        assert(decoded_bytes.size() >= original_bytes.size());
        assert(std::memcmp(decoded_bytes.data(), original_bytes.data(), original_bytes.size()) == 0);
        std::cout << "  [PASS] Taxa 1/2 sem ruido: 204 bytes decodificados perfeitamente\n";
    }

    // 2. Teste Taxa 1/2 com ruido e erros de canal (5% de bits corrompidos)
    {
        isdbt::ViterbiDecoder viterbi(isdbt::CodeRate::RATE_1_2);
        std::vector<uint8_t> symbols;
        viterbi.encode(in_bits, symbols);

        std::vector<uint8_t> soft_in(symbols.size());
        for (size_t i = 0; i < symbols.size(); ++i) {
            soft_in[i] = symbols[i] ? 255 : 0;
        }

        // Injetar erros de canal invertendo bits de soft metrics a cada 20 simbolos
        for (size_t i = 10; i < soft_in.size(); i += 20) {
            soft_in[i] = 255 - soft_in[i];
        }

        std::vector<uint8_t> decoded_bytes;
        viterbi.decode(soft_in, decoded_bytes);

        assert(decoded_bytes.size() >= original_bytes.size());
        assert(std::memcmp(decoded_bytes.data(), original_bytes.data(), original_bytes.size()) == 0);
        std::cout << "  [PASS] Taxa 1/2 com canal ruidoso: Viterbi corrigiu todos os erros de canal com sucesso\n";
    }

    // 3. Teste Taxa 2/3 (Puncionada)
    {
        isdbt::ViterbiDecoder viterbi(isdbt::CodeRate::RATE_2_3);
        std::vector<uint8_t> symbols;
        viterbi.encode(in_bits, symbols);

        std::vector<uint8_t> soft_in(symbols.size());
        for (size_t i = 0; i < symbols.size(); ++i) {
            soft_in[i] = symbols[i] ? 255 : 0;
        }

        std::vector<uint8_t> decoded_bytes;
        viterbi.decode(soft_in, decoded_bytes);

        assert(decoded_bytes.size() >= original_bytes.size());
        assert(std::memcmp(decoded_bytes.data(), original_bytes.data(), original_bytes.size()) == 0);
        std::cout << "  [PASS] Taxa 2/3 puncionada: decodificacao correta\n";
    }

    std::cout << "\033[1;32m[SUCESSO] Todos os testes do Viterbi Decoder passaram com 100% de precisao!\033[0m\n";
    return 0;
}

