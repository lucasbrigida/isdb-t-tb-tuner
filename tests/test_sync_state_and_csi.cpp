#include <iostream>
#include <vector>
#include <complex>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cassert>
#include "dsp/ofdm_sync.hpp"
#include "dsp/constants.hpp"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while(0)

// Testa a formula de soft scaling CSI centrada em 128
void test_csi_soft_scaling() {
    auto scale_soft = [](uint8_t soft_val, float weight) -> uint8_t {
        return static_cast<uint8_t>(std::clamp(
            isdbt::constants::SOFT_NEUTRAL + weight * (static_cast<float>(soft_val) - isdbt::constants::SOFT_NEUTRAL),
            0.0f, 255.0f));
    };

    // Caso 1: Soft neutro (128) permanece neutro com qualquer peso
    TEST_ASSERT(scale_soft(128, 0.0f) == 128);
    TEST_ASSERT(scale_soft(128, 0.5f) == 128);
    TEST_ASSERT(scale_soft(128, 1.0f) == 128);
    TEST_ASSERT(scale_soft(128, 2.0f) == 128);

    // Caso 2: Peso 0.0 colapsa qualquer decisao para apagamento (128)
    TEST_ASSERT(scale_soft(0, 0.0f) == 128);
    TEST_ASSERT(scale_soft(255, 0.0f) == 128);
    TEST_ASSERT(scale_soft(64, 0.0f) == 128);
    TEST_ASSERT(scale_soft(192, 0.0f) == 128);

    // Caso 3: Peso 1.0 preserva a decisao original
    TEST_ASSERT(scale_soft(0, 1.0f) == 0);
    TEST_ASSERT(scale_soft(255, 1.0f) == 255);
    TEST_ASSERT(scale_soft(80, 1.0f) == 80);

    // Caso 4: Peso 0.5 atenua a conviccao em direcao ao neutro (128)
    // 128 + 0.5 * (0 - 128) = 128 - 64 = 64
    TEST_ASSERT(scale_soft(0, 0.5f) == 64);
    // 128 + 0.5 * (255 - 128) = 128 + 63.5 = 191
    TEST_ASSERT(scale_soft(255, 0.5f) == 191);

    // Caso 5: Peso 2.0 amplifica confianca com clamp em [0, 255]
    TEST_ASSERT(scale_soft(0, 2.0f) == 0);
    TEST_ASSERT(scale_soft(255, 2.0f) == 255);

    std::cout << "  [PASS] test_csi_soft_scaling validado com sucesso.\n";
}

// Testa a maquina de estados de sincronismo OFDM (Acquisition -> Tracking -> LostLock)
void test_ofdm_sync_state_machine() {
    isdbt::OfdmSync sync(isdbt::OfdmMode::MODE_1_2K, isdbt::GuardInterval::GI_1_16);
    TEST_ASSERT(sync.sync_state() == isdbt::SyncState::Acquisition);

    constexpr size_t N_FFT = 2048;
    constexpr size_t N_GI = 128;
    constexpr size_t SYM_LEN = N_FFT + N_GI;
    constexpr size_t BLOCK_SAMPLES = 32768;
    constexpr size_t TOTAL_SAMPLES = BLOCK_SAMPLES * 2;

    std::vector<std::complex<float>> signal(TOTAL_SAMPLES);
    uint32_t lfsr = 0xACE1u;

    // Gera sinal OFDM sintetico continuo de 2 blocos consecutivos
    size_t pos = 0;
    while (pos < TOTAL_SAMPLES) {
        std::vector<std::complex<float>> useful(N_FFT);
        for (size_t k = 0; k < N_FFT; ++k) {
            lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xD0000001u);
            float re = (lfsr & 2u) ? 0.7071f : -0.7071f;
            float im = (lfsr & 4u) ? 0.7071f : -0.7071f;
            useful[k] = std::complex<float>(re, im);
        }
        for (size_t m = 0; m < SYM_LEN && pos < TOTAL_SAMPLES; ++m, ++pos) {
            signal[pos] = (m < N_GI) ? useful[N_FFT - N_GI + m] : useful[m - N_GI];
        }
    }

    // 1. Bloco 1 (0..32767): estado Acquisition -> transiciona para Tracking
    auto res1 = sync.find_symbol_boundary(&signal[0], BLOCK_SAMPLES);
    TEST_ASSERT(res1.locked);
    TEST_ASSERT(sync.sync_state() == isdbt::SyncState::Tracking);

    // 2. Bloco 2 subsequente no tempo (32768..65535): rastreia em tracking window estreita (+/- 4)
    auto res2 = sync.find_symbol_boundary(&signal[BLOCK_SAMPLES], BLOCK_SAMPLES);
    TEST_ASSERT(res2.locked);
    TEST_ASSERT(sync.sync_state() == isdbt::SyncState::Tracking);

    // 3. Injeta ruído puro (sem correlação CP) para forçar falhas consecutivas
    std::vector<std::complex<float>> noise(TOTAL_SAMPLES);
    for (size_t i = 0; i < TOTAL_SAMPLES; ++i) {
        lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xD0000001u);
        float re = (lfsr & 2u) ? 0.5f : -0.5f;
        float im = (lfsr & 4u) ? 0.5f : -0.5f;
        noise[i] = std::complex<float>(re, im);
    }

    // Falhas 1, 2, 3
    sync.find_symbol_boundary(noise.data(), noise.size());
    sync.find_symbol_boundary(noise.data(), noise.size());
    sync.find_symbol_boundary(noise.data(), noise.size());

    // Apos MAX_CONSECUTIVE_FAILURES falhas, o estado deve ser LostLock
    TEST_ASSERT(sync.sync_state() == isdbt::SyncState::LostLock);

    // 4. Proxima chamada com sinal valido: LostLock deve reverter automaticamente para Acquisition e travar novamente
    auto res3 = sync.find_symbol_boundary(signal.data(), signal.size());
    TEST_ASSERT(res3.locked);
    TEST_ASSERT(sync.sync_state() == isdbt::SyncState::Tracking);

    // 6. Teste de rastreamento multi-bloco prolongado (10 blocos consecutivos em Tracking)
    constexpr size_t MULTI_BLOCKS = 10;
    std::vector<std::complex<float>> long_signal(BLOCK_SAMPLES * MULTI_BLOCKS);
    pos = 0;
    while (pos < long_signal.size()) {
        std::vector<std::complex<float>> useful(N_FFT);
        for (size_t k = 0; k < N_FFT; ++k) {
            lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xD0000001u);
            float re = (lfsr & 2u) ? 0.7071f : -0.7071f;
            float im = (lfsr & 4u) ? 0.7071f : -0.7071f;
            useful[k] = std::complex<float>(re, im);
        }
        for (size_t m = 0; m < SYM_LEN && pos < long_signal.size(); ++m, ++pos) {
            long_signal[pos] = (m < N_GI) ? useful[N_FFT - N_GI + m] : useful[m - N_GI];
        }
    }

    sync.reset();
    TEST_ASSERT(sync.sync_state() == isdbt::SyncState::Acquisition);
    for (size_t b = 0; b < MULTI_BLOCKS; ++b) {
        auto res = sync.find_symbol_boundary(&long_signal[b * BLOCK_SAMPLES], BLOCK_SAMPLES);
        TEST_ASSERT(res.locked);
        TEST_ASSERT(sync.sync_state() == isdbt::SyncState::Tracking);
    }

    std::cout << "  [PASS] test_ofdm_sync_state_machine validado com sucesso (incluindo 10 blocos continuos).\n";
}

int main() {
    std::cout << "[TEST] Executando testes de CSI Scaling e OFDM Sync State Machine...\n";
    test_csi_soft_scaling();
    test_ofdm_sync_state_machine();
    std::cout << "[SUCESSO] Todos os testes de CSI e Sync State Machine passaram!\n";
    return 0;
}
