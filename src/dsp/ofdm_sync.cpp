#include "dsp/ofdm_sync.hpp"
#include <cmath>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace isdbt {

OfdmSync::OfdmSync(OfdmMode mode, GuardInterval gi, float sample_rate)
    : mode_(mode), gi_(gi), sample_rate_(sample_rate), fft_in_(nullptr), fft_out_(nullptr), fft_plan_(nullptr) {
    set_mode(mode, gi);
}

OfdmSync::~OfdmSync() {
    if (fft_plan_) fftwf_destroy_plan(fft_plan_);
    if (fft_in_) fftwf_free(fft_in_);
    if (fft_out_) fftwf_free(fft_out_);
}

void OfdmSync::set_mode(OfdmMode mode, GuardInterval gi) {
    mode_ = mode;
    gi_ = gi;
    fft_size_ = static_cast<int>(mode_);
    gi_size_ = fft_size_ / static_cast<int>(gi_);

    if (fft_plan_) {
        fftwf_destroy_plan(fft_plan_);
        fftwf_free(fft_in_);
        fftwf_free(fft_out_);
    }

    fft_in_ = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fft_size_);
    fft_out_ = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fft_size_);
    fft_plan_ = fftwf_plan_dft_1d(fft_size_, fft_in_, fft_out_, FFTW_FORWARD, FFTW_ESTIMATE);
}

SyncResult OfdmSync::find_symbol_boundary(const std::complex<float>* samples, size_t num_samples) {
    SyncResult res;
    int total_symbol_len = fft_size_ + gi_size_;
    if (num_samples < static_cast<size_t>(total_symbol_len * 2)) {
        return res;
    }

    // Autocorrelacao de Prefixo Ciclico deslizante (Van de Beek ML timing & frequency estimator)
    // Gamma(d) = sum_{m=0}^{N_gi - 1} r(d + m) * conj(r(d + m + N_fft))
    // Phi(d)   = 0.5 * sum_{m=0}^{N_gi - 1} (|r(d + m)|^2 + |r(d + m + N_fft)|^2)
    std::complex<float> sum_gamma(0.0f, 0.0f);
    float sum_energy = 0.0f;

    // Inicializa a primeira janela de tamanho gi_size_
    for (int m = 0; m < gi_size_; ++m) {
        std::complex<float> r1 = samples[m];
        std::complex<float> r2 = samples[m + fft_size_];
        sum_gamma += r1 * std::conj(r2);
        sum_energy += 0.5f * (std::norm(r1) + std::norm(r2));
    }

    float max_metric = 0.0f;
    int best_d = 0;
    std::complex<float> best_gamma = sum_gamma;

    size_t search_len = num_samples - total_symbol_len;
    for (size_t d = 0; d < search_len; ++d) {
        float metric = (sum_energy > 1e-6f) ? (std::norm(sum_gamma) / (sum_energy * sum_energy)) : 0.0f;
        if (metric > max_metric) {
            max_metric = metric;
            best_d = static_cast<int>(d);
            best_gamma = sum_gamma;
        }

        // Atualizacao deslizante: remove amostra mais antiga e adiciona a nova
        std::complex<float> r1_old = samples[d];
        std::complex<float> r2_old = samples[d + fft_size_];
        std::complex<float> r1_new = samples[d + gi_size_];
        std::complex<float> r2_new = samples[d + gi_size_ + fft_size_];

        sum_gamma -= r1_old * std::conj(r2_old);
        sum_gamma += r1_new * std::conj(r2_new);

        sum_energy -= 0.5f * (std::norm(r1_old) + std::norm(r2_old));
        sum_energy += 0.5f * (std::norm(r1_new) + std::norm(r2_new));
    }

    res.peak_correlation = std::sqrt(max_metric);
    // Se a correlacao normalizada for superior a 0.25, temos um sincronismo valido
    if (res.peak_correlation > 0.20f) {
        res.locked = true;
        res.symbol_offset = best_d + gi_size_; // Inicio do periodo util (pos-prefixo ciclico)

        // Estimativa de desvio fracionario de frequencia (FFO)
        // angle(Gamma) = 2 * pi * delta_f * T_u
        float phase = std::arg(best_gamma); // [-pi, +pi]
        float subcarrier_spacing = sample_rate_ / static_cast<float>(fft_size_);
        res.ffo_hz = -(phase / (2.0f * static_cast<float>(M_PI))) * subcarrier_spacing;

        // Estimativa aproximada de SNR baseada na correlacao de pico rho = SNR / (SNR + 1)
        float rho = std::min(res.peak_correlation, 0.99f);
        float snr_linear = rho / (1.0f - rho + 1e-6f);
        res.snr_est_db = 10.0f * std::log10(snr_linear + 1e-6f);
    }

    return res;
}

void OfdmSync::correct_frequency_offset(std::complex<float>* samples, size_t num_samples, float ffo_hz) {
    if (std::abs(ffo_hz) < 1.0f || num_samples == 0) return;

    float phase_step = -2.0f * static_cast<float>(M_PI) * (ffo_hz / sample_rate_);
    std::complex<float> rotator_step(std::cos(phase_step), std::sin(phase_step));
    std::complex<float> current_rotator(1.0f, 0.0f);

    std::vector<std::complex<float>> rot_vec(num_samples);
    for (size_t i = 0; i < num_samples; ++i) {
        rot_vec[i] = current_rotator;
        current_rotator *= rotator_step;
    }

    // Acelera a multiplicacao complexa vetorial atraves do VOLK
    volk_32fc_x2_multiply_32fc(
        reinterpret_cast<lv_32fc_t*>(samples),
        reinterpret_cast<const lv_32fc_t*>(samples),
        reinterpret_cast<const lv_32fc_t*>(rot_vec.data()),
        static_cast<unsigned int>(num_samples)
    );
}

void OfdmSync::process_symbol_fft(const std::complex<float>* symbol_in, std::complex<float>* carriers_out) {
    if (!symbol_in || !carriers_out || !fft_plan_) return;

    for (int i = 0; i < fft_size_; ++i) {
        fft_in_[i][0] = symbol_in[i].real();
        fft_in_[i][1] = symbol_in[i].imag();
    }

    fftwf_execute(fft_plan_);

    // Normalizacao 1/sqrt(N) e FFT shift para centralizar as portadoras
    float norm = 1.0f / std::sqrt(static_cast<float>(fft_size_));
    int half = fft_size_ / 2;

    for (int i = 0; i < half; ++i) {
        carriers_out[i] = std::complex<float>(fft_out_[i + half][0] * norm, fft_out_[i + half][1] * norm);
        carriers_out[i + half] = std::complex<float>(fft_out_[i][0] * norm, fft_out_[i][1] * norm);
    }
}

} // namespace isdbt

