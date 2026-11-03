#include <iostream>
#include <vector>
#include <complex>
#include <cassert>
#include <cmath>
#include <algorithm>
#include "sync/robust_grisdbt_sync.h"
#include "dsp/constants.hpp"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while(0)

int main() {
    std::cout << "=== Teste Unitario RobustGRISDBTSync (R7, N5, N6) ===\n";

    isdbt::SyncParams params;
    params.mode = isdbt::OfdmMode::MODE_1_2K;
    params.gi = isdbt::GuardInterval::GI_1_16;
    params.sample_rate = isdbt::constants::SAMPLE_RATE_HZ;
    params.fft_size = 2048;
    params.gi_size = 128;

    isdbt::RobustGRISDBTSync sync(params);

    // 1. Estado inicial
    TEST_ASSERT(sync.robust_state() == isdbt::RobustSyncState::Acquisition);
    TEST_ASSERT(sync.state() == isdbt::SyncState::Acquisition);

    // 2. Gera sinal OFDM sintetico continuo
    constexpr size_t N_FFT = 2048;
    constexpr size_t N_GI  = 128;
    constexpr size_t SYM_LEN = N_FFT + N_GI;
    constexpr size_t NUM_SYMS = 30;
    std::vector<std::complex<float>> signal(SYM_LEN * NUM_SYMS);

    uint32_t lfsr = 0xACE1u;
    size_t pos = 0;
    for (size_t s = 0; s < NUM_SYMS; ++s) {
        std::vector<std::complex<float>> useful(N_FFT);
        for (size_t k = 0; k < N_FFT; ++k) {
            lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xD0000001u);
            float re = (lfsr & 2u) ? 0.7071f : -0.7071f;
            float im = (lfsr & 4u) ? 0.7071f : -0.7071f;
            useful[k] = std::complex<float>(re, im);
        }
        for (size_t m = 0; m < SYM_LEN; ++m, ++pos) {
            signal[pos] = (m < N_GI) ? useful[N_FFT - N_GI + m] : useful[m - N_GI];
        }
    }

    std::vector<std::complex<float>> offset_signal(32768);
    constexpr size_t offset_samples = 2 * SYM_LEN + 2;
    std::copy_n(signal.begin(), std::min(signal.size(), offset_signal.size() - offset_samples),
                offset_signal.begin() + offset_samples);
    isdbt::RobustGRISDBTSync offset_sync(params);
    std::vector<std::complex<float>> offset_output;
    offset_sync.process(offset_signal.data(), offset_signal.size(), offset_output);
    TEST_ASSERT(offset_sync.robust_state() == isdbt::RobustSyncState::Tracking);
    TEST_ASSERT(!offset_output.empty());

    std::vector<std::complex<float>> out_symbols;
    out_symbols.reserve(SYM_LEN * 4);

    // Processa primeiro bloco
    sync.process(signal.data(), 32768, out_symbols);
    TEST_ASSERT(sync.robust_state() == isdbt::RobustSyncState::Tracking);
    TEST_ASSERT(sync.state() == isdbt::SyncState::Tracking);
    TEST_ASSERT(!out_symbols.empty());

    // 3. Processa restante do sinal em modo tracking
    sync.process(signal.data() + 32768, signal.size() - 32768, out_symbols);
    TEST_ASSERT(sync.robust_state() == isdbt::RobustSyncState::Tracking);
    TEST_ASSERT(!out_symbols.empty());

    // 4. Injeta ruido puro para testar transicao Tracking -> Resync -> LostLock
    std::vector<std::complex<float>> noise(32768);
    for (size_t i = 0; i < noise.size(); ++i) {
        lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xD0000001u);
        noise[i] = std::complex<float>((lfsr & 2u) ? 0.5f : -0.5f, (lfsr & 4u) ? 0.5f : -0.5f);
    }

    for (int it = 0; it < 5; ++it) {
        sync.process(noise.data(), noise.size(), out_symbols);
    }
    TEST_ASSERT(sync.robust_state() == isdbt::RobustSyncState::LostLock);
    TEST_ASSERT(sync.state() == isdbt::SyncState::LostLock);

    // 5. Reinicia e valida re-lock
    sync.reset();
    TEST_ASSERT(sync.robust_state() == isdbt::RobustSyncState::Acquisition);
    sync.process(signal.data(), 32768, out_symbols);
    TEST_ASSERT(sync.robust_state() == isdbt::RobustSyncState::Tracking);

    std::cout << "  Transicoes de estado: " << sync.state_transitions() << "\n";
    std::cout << "[PASS] RobustGRISDBTSync testado e aprovado com sucesso!\n";
    return 0;
}
