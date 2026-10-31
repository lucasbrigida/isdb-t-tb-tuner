#include <iostream>
#include <fstream>
#include <vector>
#include <complex>
#include <cmath>
#include <cassert>
#include "dsp/ofdm_sync.hpp"

int main() {
    std::cout << "[TEST] Testando Sincronismo OFDM com Taxa Exata ISDB-T (2031746 S/s)...\n";

    size_t num_samples = 131072;
    std::vector<std::complex<float>> iq_samples(num_samples);

    std::ifstream file("tests/data/channel30_exact_isdbt_rate.raw", std::ios::binary);
    if (!file.is_open()) {
        file.open("../tests/data/channel30_exact_isdbt_rate.raw", std::ios::binary);
    }

    if (file.is_open()) {
        std::vector<uint8_t> raw_bytes(num_samples * 2);
        file.read(reinterpret_cast<char*>(raw_bytes.data()), raw_bytes.size());
        for (size_t i = 0; i < num_samples; ++i) {
            float i_val = (static_cast<float>(raw_bytes[2 * i + 0]) - 127.5f) / 127.5f;
            float q_val = (static_cast<float>(raw_bytes[2 * i + 1]) - 127.5f) / 127.5f;
            iq_samples[i] = std::complex<float>(i_val, q_val);
        }
    } else {
        // Sintetiza sinal OFDM ISDB-T Modo 3 (2048 FFT + 128 GI = 2176 amostras) @ 2031746 S/s
        // com desvio fracionario de frequencia FFO = +314 Hz
        constexpr size_t N_FFT = 2048;
        constexpr size_t N_GI  = 128; // GI 1/16
        constexpr size_t SYM_LEN = N_FFT + N_GI;
        uint32_t lfsr = 0x13579BDFu;
        size_t pos = 0;
        while (pos < num_samples) {
            std::vector<std::complex<float>> useful(N_FFT);
            for (size_t k = 0; k < N_FFT; ++k) {
                lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xD0000001u);
                float re = (lfsr & 2u) ? 0.7071f : -0.7071f;
                float im = (lfsr & 4u) ? 0.7071f : -0.7071f;
                useful[k] = std::complex<float>(re, im);
            }
            for (size_t m = 0; m < SYM_LEN && pos < num_samples; ++m, ++pos) {
                std::complex<float> s = (m < N_GI) ? useful[N_FFT - N_GI + m] : useful[m - N_GI];
                float phase = 2.0f * 3.1415926535f * (314.0f / 2031746.0f) * static_cast<float>(pos);
                iq_samples[pos] = s * std::complex<float>(std::cos(phase), std::sin(phase));
            }
        }
    }

    // No clock decrescido por 4 (2.031746 MHz), Mode 3 corresponde a FFT de 2048 pontos
    std::vector<isdbt::GuardInterval> gis = {
        isdbt::GuardInterval::GI_1_4,
        isdbt::GuardInterval::GI_1_8,
        isdbt::GuardInterval::GI_1_16,
        isdbt::GuardInterval::GI_1_32
    };

    const char* gi_names[] = {"1/4", "1/8", "1/16", "1/32"};
    bool locked_any = false;

    for (size_t i = 0; i < gis.size(); ++i) {
        isdbt::OfdmSync sync(isdbt::OfdmMode::MODE_1_2K, gis[i], 2031746.0f); // 2048 points
        auto res = sync.find_symbol_boundary(iq_samples.data(), iq_samples.size());
        if (res.locked) locked_any = true;
        std::cout << "  - Modo 3 (2048 pts) | GI " << gi_names[i]
                  << " -> Pico: " << res.peak_correlation
                  << " | Offset: " << res.symbol_offset
                  << " | FFO: " << res.ffo_hz << " Hz"
                  << " | Lock: " << (res.locked ? "\033[1;32mSIM!\033[0m" : "NAO") << "\n";
    }

    assert(locked_any);
    return 0;
}
