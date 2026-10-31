#include <iostream>
#include <cassert>
#include <cstring>
#include <random>
#include "dsp/reed_solomon.hpp"

int main() {
    std::cout << "[TEST] Iniciando Testes Unitarios do Reed-Solomon (204, 188)...\n";
    isdbt::ReedSolomon rs;

    // Criar pacote de teste 188 bytes com sync byte MPEG-TS 0x47
    uint8_t original_ts[188];
    original_ts[0] = 0x47;
    for (int i = 1; i < 188; ++i) {
        original_ts[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);
    }

    uint8_t block[204];
    std::memcpy(block, original_ts, 188);
    rs.encode(original_ts, block + 188);

    // 1. Teste sem erros
    uint8_t test_block[204];
    std::memcpy(test_block, block, 204);
    int corr = rs.decode(test_block);
    assert(corr == 0);
    assert(std::memcmp(test_block, original_ts, 188) == 0);
    std::cout << "  [PASS] Bloco sem erros decodificado com sucesso (corr = 0)\n";

    // 2. Teste com 1 erro
    std::memcpy(test_block, block, 204);
    test_block[10] ^= 0x55;
    corr = rs.decode(test_block);
    assert(corr == 1);
    assert(std::memcmp(test_block, original_ts, 188) == 0);
    std::cout << "  [PASS] Bloco com 1 erro corrigido perfeitamente (corr = 1)\n";

    // 3. Teste com 5 erros
    std::memcpy(test_block, block, 204);
    test_block[0] ^= 0x12;
    test_block[50] ^= 0x34;
    test_block[100] ^= 0x56;
    test_block[150] ^= 0x78;
    test_block[200] ^= 0x9A;
    corr = rs.decode(test_block);
    assert(corr == 5);
    assert(std::memcmp(test_block, original_ts, 188) == 0);
    std::cout << "  [PASS] Bloco com 5 erros corrigido perfeitamente (corr = 5)\n";

    // 4. Teste no limite maximo de correcao: t=8 erros
    std::memcpy(test_block, block, 204);
    for (int e = 0; e < 8; ++e) {
        test_block[e * 25] ^= static_cast<uint8_t>(0xA5 + e);
    }
    corr = rs.decode(test_block);
    assert(corr == 8);
    assert(std::memcmp(test_block, original_ts, 188) == 0);
    std::cout << "  [PASS] Bloco no limite maximo t=8 erros corrigido perfeitamente (corr = 8)\n";

    // 5. Teste com 10 erros (acima da capacidade t=8)
    std::memcpy(test_block, block, 204);
    for (int e = 0; e < 10; ++e) {
        test_block[e * 20] ^= 0xFF;
    }
    corr = rs.decode(test_block);
    assert(corr == -1);
    std::cout << "  [PASS] Bloco com 10 erros corretamente rejeitado como incorrigivel (corr = -1)\n";

    std::cout << "\033[1;32m[SUCESSO] Todos os testes do Reed-Solomon passaram com 100% de precisao!\033[0m\n";
    return 0;
}

