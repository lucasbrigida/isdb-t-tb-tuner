#include <iostream>
#include <fstream>
#include <vector>
#include <complex>
#include "dsp/ofdm_sync.hpp"

int main() {
    std::cout << "[TEST] Testando Sincronismo OFDM com Taxa Exata ISDB-T (2031746 S/s)...\n";

    std::ifstream file("tests/data/channel30_exact_isdbt_rate.raw", std::ios::binary);
    if (!file.is_open()) {
        file.open("../tests/data/channel30_exact_isdbt_rate.raw", std::ios::binary);
    }
    if (!file.is_open()) {
        std::cerr << "  [ERRO] Nao foi possivel abrir tests/data/channel30_exact_isdbt_rate.raw\n";
        return 1;
    }

    size_t num_samples = 131072;
    std::vector<uint8_t> raw_bytes(num_samples * 2);
    file.read(reinterpret_cast<char*>(raw_bytes.data()), raw_bytes.size());

    std::vector<std::complex<float>> iq_samples(num_samples);
    for (size_t i = 0; i < num_samples; ++i) {
        float i_val = (static_cast<float>(raw_bytes[2 * i + 0]) - 127.5f) / 127.5f;
        float q_val = (static_cast<float>(raw_bytes[2 * i + 1]) - 127.5f) / 127.5f;
        iq_samples[i] = std::complex<float>(i_val, q_val);
    }

    // No clock decrescido por 4 (2.031746 MHz), Mode 3 corresponde a FFT de 2048 pontos
    std::vector<isdbt::GuardInterval> gis = {
        isdbt::GuardInterval::GI_1_4,
        isdbt::GuardInterval::GI_1_8,
        isdbt::GuardInterval::GI_1_16,
        isdbt::GuardInterval::GI_1_32
    };

    const char* gi_names[] = {"1/4", "1/8", "1/16", "1/32"};

    for (size_t i = 0; i < gis.size(); ++i) {
        isdbt::OfdmSync sync(isdbt::OfdmMode::MODE_1_2K, gis[i], 2031746.0f); // 2048 points
        auto res = sync.find_symbol_boundary(iq_samples.data(), iq_samples.size());
        std::cout << "  - Modo 3 (2048 pts) | GI " << gi_names[i]
                  << " -> Pico: " << res.peak_correlation
                  << " | Offset: " << res.symbol_offset
                  << " | FFO: " << res.ffo_hz << " Hz"
                  << " | Lock: " << (res.locked ? "\033[1;32mSIM!\033[0m" : "NAO") << "\n";
    }

    return 0;
}
