#include <iostream>
#include <fstream>
#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <filesystem>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    std::string out_path = "tests/fixtures/synthetic_iq_10s.cu8";
    if (argc > 1) {
        out_path = argv[1];
    }

    fs::path p(out_path);
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path());
    }

    constexpr uint32_t sample_rate = 2031746;
    constexpr double duration_s = 10.0;
    size_t total_samples = static_cast<size_t>(sample_rate * duration_s);

    constexpr size_t N_FFT = 2048;
    constexpr size_t N_GI  = 128; // GI 1/16
    constexpr size_t SYM_LEN = N_FFT + N_GI;

    std::cout << "[gen_synthetic_iq] Gerando " << total_samples << " amostras CU8 ("
              << duration_s << "s @ " << sample_rate << " S/s)...\n";

    std::ofstream out(out_path, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "[ERRO] Falha ao abrir " << out_path << " para escrita.\n";
        return 1;
    }

    uint32_t lfsr = 0x13579BDFu;
    size_t written = 0;
    std::vector<uint8_t> block_bytes(SYM_LEN * 2);

    while (written < total_samples) {
        std::vector<std::complex<float>> useful(N_FFT);
        for (size_t k = 0; k < N_FFT; ++k) {
            lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xD0000001u);
            float re = (lfsr & 2u) ? 0.7071f : -0.7071f;
            float im = (lfsr & 4u) ? 0.7071f : -0.7071f;
            useful[k] = std::complex<float>(re, im);
        }

        size_t samples_in_sym = std::min(SYM_LEN, total_samples - written);
        for (size_t m = 0; m < samples_in_sym; ++m) {
            std::complex<float> s = (m < N_GI) ? useful[N_FFT - N_GI + m] : useful[m - N_GI];
            // Pequeno FFO de +314 Hz para simular sinal do ar
            float phase = 2.0f * 3.1415926535f * (314.0f / 2031746.0f) * static_cast<float>(written + m);
            std::complex<float> rot = s * std::complex<float>(std::cos(phase), std::sin(phase));

            // Converte float [-1.0 .. 1.0] para uint8 [0 .. 255] com centro em 127.5
            float iv = std::round(rot.real() * 40.0f + 127.5f);
            float qv = std::round(rot.imag() * 40.0f + 127.5f);
            block_bytes[2 * m + 0] = static_cast<uint8_t>(std::clamp(iv, 0.0f, 255.0f));
            block_bytes[2 * m + 1] = static_cast<uint8_t>(std::clamp(qv, 0.0f, 255.0f));
        }

        out.write(reinterpret_cast<const char*>(block_bytes.data()), samples_in_sym * 2);
        written += samples_in_sym;
    }

    out.close();
    std::cout << "[gen_synthetic_iq] Salvo com sucesso em " << out_path << " (" << written * 2 << " bytes).\n";
    return 0;
}
