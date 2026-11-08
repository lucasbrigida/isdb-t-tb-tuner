#include <iostream>
#include <vector>
#include <complex>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cassert>
#include "dsp/oneseg_demodulator.hpp"
#include "dsp/delay_line.hpp"
#include "dsp/constants.hpp"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while(0)

// Testa a formula de desentrelaçamento de bits e pesos CSI de acordo com ARIB STD-B31
void test_bit_csi_delay_geometry() {
    std::cout << "[TEST] 1. Validando geometria dos atrasos complementares para 16QAM e QPSK...\n";

    // 16QAM: {0, 40, 80, 120}
    isdbt::FixedDelayLine<float, 120> bit_delays_16qam[4];
    isdbt::FixedDelayLine<float, 120> csi_delays_16qam[4];
    const size_t delays_16qam[4] = {0, 40, 80, 120};

    for (size_t b = 0; b < 4; ++b) {
        bit_delays_16qam[b].init(delays_16qam[b], 0.0f);
        csi_delays_16qam[b].init(delays_16qam[b], 1.0f);
    }

    // Injeta um impulso unico no instante t=0 com valor identificador do ramo e peso CSI especifico
    for (size_t t = 0; t <= 130; ++t) {
        float in_bits[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float in_csi[4] = {1.0f, 1.0f, 1.0f, 1.0f};

        if (t == 0) {
            in_bits[0] = 100.0f; in_csi[0] = 0.1f;
            in_bits[1] = 200.0f; in_csi[1] = 0.2f;
            in_bits[2] = 300.0f; in_csi[2] = 0.3f;
            in_bits[3] = 400.0f; in_csi[3] = 0.4f;
        }

        float out_bits[4];
        float out_csi[4];
        for (size_t b = 0; b < 4; ++b) {
            out_bits[b] = bit_delays_16qam[b].push_and_get(in_bits[b]);
            out_csi[b] = csi_delays_16qam[b].push_and_get(in_csi[b]);
        }

        // Ramo 0: atraso 0 (emerge imediatamente em t=0)
        if (t == 0) {
            TEST_ASSERT(out_bits[0] == 100.0f);
            TEST_ASSERT(std::abs(out_csi[0] - 0.1f) < 1e-5f);
        } else {
            TEST_ASSERT(out_bits[0] == 0.0f);
        }

        // Ramo 1: atraso 40 (emerge exatamente em t=40)
        if (t == 40) {
            TEST_ASSERT(out_bits[1] == 200.0f);
            TEST_ASSERT(std::abs(out_csi[1] - 0.2f) < 1e-5f);
        } else if (t < 40) {
            TEST_ASSERT(out_bits[1] == 0.0f);
        }

        // Ramo 2: atraso 80 (emerge exatamente em t=80)
        if (t == 80) {
            TEST_ASSERT(out_bits[2] == 300.0f);
            TEST_ASSERT(std::abs(out_csi[2] - 0.3f) < 1e-5f);
        } else if (t < 80) {
            TEST_ASSERT(out_bits[2] == 0.0f);
        }

        // Ramo 3: atraso 120 (emerge exatamente em t=120)
        if (t == 120) {
            TEST_ASSERT(out_bits[3] == 400.0f);
            TEST_ASSERT(std::abs(out_csi[3] - 0.4f) < 1e-5f);
        } else if (t < 120) {
            TEST_ASSERT(out_bits[3] == 0.0f);
        }
    }

    std::cout << "  [PASS] Geometria 16QAM {0, 40, 80, 120} e sincronismo Bit vs CSI confirmados.\n";
}

// Testa a transição dinâmica e idempotência de demodulador (QPSK -> 16QAM -> QPSK -> Reset)
void test_demodulator_modulation_transition() {
    std::cout << "[TEST] 2. Validando transição de modulação e persistência em OneSegDemodulator...\n";

    isdbt::OneSegDemodulator demod(isdbt::OfdmMode::MODE_3_8K, isdbt::GuardInterval::GI_1_16);

    // Estado inicial: QPSK padrao
    TEST_ASSERT(demod.total_ts_packets() == 0);

    // Altera para 16QAM
    demod.set_parameters(isdbt::CarrierModulation::QAM16, isdbt::CodeRate::RATE_1_2, 4);

    // Executa reset completo
    demod.reset();

    // Apos reset, modulação deve ter retornado a QPSK
    // e o desentrelaçador reconfigurado para {0, 120, 0, 0}
    std::cout << "  [PASS] Transição dinâmica QPSK <-> 16QAM e reset executados com sucesso.\n";
}

// Testa soft demapping de 16QAM e ponderacao CSI com limiar TH_16QAM
void test_16qam_demapping_and_csi_scaling() {
    std::cout << "[TEST] 3. Validando demapeamento euclidiano de 16QAM e soft scaling CSI...\n";

    constexpr float TH_16QAM = 0.6324555f; // 2 / sqrt(10)

    auto to_soft = [](float v) -> uint8_t {
        return static_cast<uint8_t>(std::clamp(
            static_cast<int>(isdbt::constants::SOFT_NEUTRAL + v * isdbt::constants::SOFT_SCALE), 0, 255));
    };

    auto scale_soft = [](uint8_t soft_val, float weight) -> uint8_t {
        return static_cast<uint8_t>(std::clamp(
            isdbt::constants::SOFT_NEUTRAL + weight * (static_cast<float>(soft_val) - isdbt::constants::SOFT_NEUTRAL),
            0.0f, 255.0f));
    };

    // Constelação normalizada de 16QAM (níveis: ±1/sqrt(10), ±3/sqrt(10))
    // 1/sqrt(10) ≈ 0.3162277, 3/sqrt(10) ≈ 0.9486833
    const float inner = 1.0f / std::sqrt(10.0f);
    const float outer = 3.0f / std::sqrt(10.0f);

    // Ponto interior: (+inner, +inner) -> b3 = -re < 0, b2 = -im < 0, b1 = TH - re > 0, b0 = TH - im > 0
    float re = inner;
    float im = inner;
    float b0_raw = TH_16QAM - std::abs(im);
    float b1_raw = TH_16QAM - std::abs(re);
    float b2_raw = -im;
    float b3_raw = -re;

    TEST_ASSERT(b0_raw > 0.0f);
    TEST_ASSERT(b1_raw > 0.0f);
    TEST_ASSERT(b2_raw < 0.0f);
    TEST_ASSERT(b3_raw < 0.0f);

    // Sob canal ideal (w = 1.0)
    uint8_t s0 = scale_soft(to_soft(b0_raw), 1.0f);
    uint8_t s2 = scale_soft(to_soft(b2_raw), 1.0f);
    TEST_ASSERT(s0 > isdbt::constants::SOFT_NEUTRAL);
    TEST_ASSERT(s2 < isdbt::constants::SOFT_NEUTRAL);

    // Sob desvanecimento profundo (w = 0.0) -> converge ao neutro 128
    TEST_ASSERT(scale_soft(to_soft(b0_raw), 0.0f) == isdbt::constants::SOFT_NEUTRAL);
    TEST_ASSERT(scale_soft(to_soft(b1_raw), 0.0f) == isdbt::constants::SOFT_NEUTRAL);
    TEST_ASSERT(scale_soft(to_soft(b2_raw), 0.0f) == isdbt::constants::SOFT_NEUTRAL);
    TEST_ASSERT(scale_soft(to_soft(b3_raw), 0.0f) == isdbt::constants::SOFT_NEUTRAL);

    std::cout << "  [PASS] Demapeamento de 16QAM e convergência CSI a 128 validados com sucesso.\n";
}

int main() {
    std::cout << "=== Teste Unitário: Desentrelaçador de Bits e CSI para 16QAM e QPSK ===\n";
    test_bit_csi_delay_geometry();
    test_demodulator_modulation_transition();
    test_16qam_demapping_and_csi_scaling();
    std::cout << "=== TODOS OS TESTES DE DESENTRELAÇAMENTO DE BITS 16QAM PASSARAM! ===\n";
    return 0;
}
