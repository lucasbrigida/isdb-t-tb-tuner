#pragma once

#include <complex>
#include <vector>
#include <cstdint>
#include <fftw3.h>
#include <volk/volk.h>

namespace isdbt {

enum class OfdmMode {
    MODE_1_2K = 2048,
    MODE_2_4K = 4096,
    MODE_3_8K = 8192
};

enum class GuardInterval {
    GI_1_4 = 4,   // N_fft / 4
    GI_1_8 = 8,   // N_fft / 8
    GI_1_16 = 16, // N_fft / 16
    GI_1_32 = 32  // N_fft / 32
};

struct SyncResult {
    bool locked = false;
    int symbol_offset = 0;       // Indice do inicio do simbolo util
    float ffo_hz = 0.0f;         // Fractional Frequency Offset estimado em Hz
    float peak_correlation = 0.0f;// Razao de correlacao de pico (qualidade do sinal)
    float snr_est_db = 0.0f;     // Estimativa aproximada de SNR em dB
};

class OfdmSync {
public:
    OfdmSync(OfdmMode mode = OfdmMode::MODE_3_8K, GuardInterval gi = GuardInterval::GI_1_16, float sample_rate = 2048000.0f);
    ~OfdmSync();

    void set_mode(OfdmMode mode, GuardInterval gi);
    SyncResult find_symbol_boundary(const std::complex<float>* samples, size_t num_samples);

    /**
     * @brief Corrige o desvio de frequencia FFO no bloco de amostras utilizando VOLK.
     */
    void correct_frequency_offset(std::complex<float>* samples, size_t num_samples, float ffo_hz);

    /**
     * @brief Executa FFT no simbolo alinhado e extrai as portadoras do 1-Seg.
     */
    void process_symbol_fft(const std::complex<float>* symbol_in, std::complex<float>* carriers_out);

private:
    OfdmMode mode_;
    GuardInterval gi_;
    float sample_rate_;
    int fft_size_;
    int gi_size_;

    fftwf_complex* fft_in_;
    fftwf_complex* fft_out_;
    fftwf_plan fft_plan_;
};

} // namespace isdbt

