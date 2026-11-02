#include "dsp/ofdm_sync.hpp"
#include <cmath>
#include <iostream>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace isdbt {

OfdmSync::OfdmSync(OfdmMode mode, GuardInterval gi, float sample_rate)
    : mode_(mode), gi_(gi), sample_rate_(sample_rate), current_phase_(0.0f),
      fft_in_(nullptr), fft_out_(nullptr), fft_plan_(nullptr) {
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
    reset();

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
    res.detected_gi = gi_;
    int total_symbol_len = fft_size_ + gi_size_;
    if (num_samples < static_cast<size_t>(total_symbol_len * 2)) {
        return res;
    }

    if (state_ == SyncState::LostLock) {
        state_ = SyncState::Acquisition;
        consecutive_failures_ = 0;
        has_expected_d_ = false;
    }

    bool in_tracking = (state_ == SyncState::Tracking && has_expected_d_);

    // Se o buffer contiver ao menos 6 simbolos (ex: bloco USB de 32768 amostras),
    // avalia os intervalos de guarda padrao ISDB-T (1/16, 1/8, 1/4, 1/32) dobrando (folding)
    // a correlacao do prefixo ciclico ao longo de multiplos simbolos consecutivos.
    // Isso distingue sem ambiguidade GI 1/8 (2304 amostras) de GI 1/16 (2176 amostras) e GI 1/4 (2560 amostras).
    if (num_samples >= static_cast<size_t>(fft_size_ * 6)) {
        float best_folded_rho = -1.0f;
        GuardInterval best_gi = gi_;
        int best_gi_size = gi_size_;
        int best_first_d = 0;
        std::complex<float> best_folded_gamma(0.0f, 0.0f);

        // Pre-calcula o produto ponto a ponto p[n] = r[n] * conj(r[n + N_fft]) e energia e[n]
        size_t corr_len = num_samples - static_cast<size_t>(fft_size_);
        if (prod_.size() < corr_len) {
            prod_.resize(corr_len);
            nrm_.resize(corr_len);
        }
        for (size_t n = 0; n < corr_len; ++n) {
            std::complex<float> r1 = samples[n];
            std::complex<float> r2 = samples[n + fft_size_];
            prod_[n] = r1 * std::conj(r2);
            nrm_[n] = 0.5f * (std::norm(r1) + std::norm(r2));
        }

        if (in_tracking) {
            // Em modo Tracking, o intervalo de guarda ja foi estabelecido com seguranca.
            // Avalia apenas em janela estreita (+/- TRACK_RADIUS amostras) em torno do boundary predito.
            int c_gi_size = gi_size_;
            int c_sym_len = fft_size_ + c_gi_size;
            int num_folds = static_cast<int>((corr_len - c_gi_size) / c_sym_len);
            if (num_folds >= 2) {
                if (num_folds > 8) num_folds = 8;
                size_t win_limit = static_cast<size_t>(num_folds * c_sym_len);
                if (win_gamma_.size() < win_limit) {
                    win_gamma_.resize(win_limit);
                    win_energy_.resize(win_limit);
                }

                std::complex<float> sg(0.0f, 0.0f);
                float se = 0.0f;
                for (int m = 0; m < c_gi_size; ++m) {
                    sg += prod_[m];
                    se += nrm_[m];
                }
                for (size_t d = 0; d < win_limit; ++d) {
                    win_gamma_[d] = sg;
                    win_energy_[d] = se;
                    sg += prod_[d + c_gi_size] - prod_[d];
                    se += nrm_[d + c_gi_size] - nrm_[d];
                }

                for (int sh = -TRACK_RADIUS; sh <= TRACK_RADIUS; ++sh) {
                    int d0 = (expected_first_d_ + sh + c_sym_len) % c_sym_len;
                    std::complex<float> f_gamma(0.0f, 0.0f);
                    float f_energy = 0.0f;
                    for (int k = 0; k < num_folds; ++k) {
                        size_t idx = static_cast<size_t>(d0 + k * c_sym_len);
                        f_gamma += win_gamma_[idx];
                        f_energy += win_energy_[idx];
                    }
                    float rho = (f_energy > 1e-6f) ? (std::abs(f_gamma) / f_energy) : 0.0f;
                    if (rho > best_folded_rho) {
                        best_folded_rho = rho;
                        best_gi = gi_;
                        best_gi_size = c_gi_size;
                        best_first_d = d0;
                        best_folded_gamma = f_gamma;
                    }
                }
            }
        } else {
            // Em modo Acquisition, varre exaustivamente todos os intervalos de guarda possiveis
            const GuardInterval candidates[4] = {
                GuardInterval::GI_1_16,
                GuardInterval::GI_1_8,
                GuardInterval::GI_1_4,
                GuardInterval::GI_1_32
            };

            for (GuardInterval cand_gi : candidates) {
                int c_gi_size = fft_size_ / static_cast<int>(cand_gi);
                int c_sym_len = fft_size_ + c_gi_size;
                int num_folds = static_cast<int>((corr_len - c_gi_size) / c_sym_len);
                if (num_folds < 2) continue;
                if (num_folds > 8) num_folds = 8;

                size_t win_limit = static_cast<size_t>(num_folds * c_sym_len);
                if (win_gamma_.size() < win_limit) {
                    win_gamma_.resize(win_limit);
                    win_energy_.resize(win_limit);
                }

                std::complex<float> sg(0.0f, 0.0f);
                float se = 0.0f;
                for (int m = 0; m < c_gi_size; ++m) {
                    sg += prod_[m];
                    se += nrm_[m];
                }
                for (size_t d = 0; d < win_limit; ++d) {
                    win_gamma_[d] = sg;
                    win_energy_[d] = se;
                    sg += prod_[d + c_gi_size] - prod_[d];
                    se += nrm_[d + c_gi_size] - nrm_[d];
                }

                for (int d0 = 0; d0 < c_sym_len; ++d0) {
                    std::complex<float> f_gamma(0.0f, 0.0f);
                    float f_energy = 0.0f;
                    for (int k = 0; k < num_folds; ++k) {
                        size_t idx = static_cast<size_t>(d0 + k * c_sym_len);
                        f_gamma += win_gamma_[idx];
                        f_energy += win_energy_[idx];
                    }
                    float rho = (f_energy > 1e-6f) ? (std::abs(f_gamma) / f_energy) : 0.0f;
                    if (rho > best_folded_rho) {
                        best_folded_rho = rho;
                        best_gi = cand_gi;
                        best_gi_size = c_gi_size;
                        best_first_d = d0;
                        best_folded_gamma = f_gamma;
                    }
                }
            }
        }

        res.peak_correlation = best_folded_rho;
        if (res.peak_correlation > 0.20f) {
            gi_ = best_gi;
            gi_size_ = best_gi_size;
            res.detected_gi = best_gi;
            res.locked = true;
            res.symbol_offset = best_first_d + best_gi_size;

            float phase = std::arg(best_folded_gamma);
            float subcarrier_spacing = sample_rate_ / static_cast<float>(fft_size_);
            res.ffo_hz = -(phase / (2.0f * static_cast<float>(M_PI))) * subcarrier_spacing;

            float rho = std::min(res.peak_correlation, 0.99f);
            float snr_linear = rho / (1.0f - rho + 1e-6f);
            res.snr_est_db = 10.0f * std::log10(snr_linear + 1e-6f);

            state_ = SyncState::Tracking;
            consecutive_failures_ = 0;
            int cur_total_sym = fft_size_ + gi_size_;
            expected_first_d_ = (best_first_d + cur_total_sym - static_cast<int>(num_samples % cur_total_sym)) % cur_total_sym;
            has_expected_d_ = true;
        } else {
            if (in_tracking) {
                consecutive_failures_++;
                int cur_total_sym = fft_size_ + gi_size_;
                expected_first_d_ = (expected_first_d_ + cur_total_sym - static_cast<int>(num_samples % cur_total_sym)) % cur_total_sym;
                if (consecutive_failures_ >= MAX_CONSECUTIVE_FAILURES) {
                    state_ = SyncState::LostLock;
                    has_expected_d_ = false;
                }
            }
        }
        return res;
    }

    // Autocorrelacao de Prefixo Ciclico deslizante (Van de Beek ML timing & frequency estimator)
    float max_metric = 0.0f;
    int best_d = 0;
    std::complex<float> best_gamma(0.0f, 0.0f);
    size_t search_len = num_samples - total_symbol_len;

    if (in_tracking) {
        for (int sh = -TRACK_RADIUS; sh <= TRACK_RADIUS; ++sh) {
            int d0 = (expected_first_d_ + sh + total_symbol_len) % total_symbol_len;
            if (static_cast<size_t>(d0) >= search_len) continue;
            std::complex<float> gamma(0.0f, 0.0f);
            float energy = 0.0f;
            for (int m = 0; m < gi_size_; ++m) {
                std::complex<float> r1 = samples[d0 + m];
                std::complex<float> r2 = samples[d0 + m + fft_size_];
                gamma += r1 * std::conj(r2);
                energy += 0.5f * (std::norm(r1) + std::norm(r2));
            }
            float metric = (energy > 1e-6f) ? (std::norm(gamma) / (energy * energy)) : 0.0f;
            if (metric > max_metric) {
                max_metric = metric;
                best_d = d0;
                best_gamma = gamma;
            }
        }
    } else {
        std::complex<float> sum_gamma(0.0f, 0.0f);
        float sum_energy = 0.0f;

        for (int m = 0; m < gi_size_; ++m) {
            std::complex<float> r1 = samples[m];
            std::complex<float> r2 = samples[m + fft_size_];
            sum_gamma += r1 * std::conj(r2);
            sum_energy += 0.5f * (std::norm(r1) + std::norm(r2));
        }

        for (size_t d = 0; d < search_len; ++d) {
            float metric = (sum_energy > 1e-6f) ? (std::norm(sum_gamma) / (sum_energy * sum_energy)) : 0.0f;
            if (metric > max_metric) {
                max_metric = metric;
                best_d = static_cast<int>(d);
                best_gamma = sum_gamma;
            }

            std::complex<float> r1_old = samples[d];
            std::complex<float> r2_old = samples[d + fft_size_];
            std::complex<float> r1_new = samples[d + gi_size_];
            std::complex<float> r2_new = samples[d + gi_size_ + fft_size_];

            sum_gamma -= r1_old * std::conj(r2_old);
            sum_gamma += r1_new * std::conj(r2_new);

            sum_energy -= 0.5f * (std::norm(r1_old) + std::norm(r2_old));
            sum_energy += 0.5f * (std::norm(r1_new) + std::norm(r2_new));
        }
    }

    res.peak_correlation = std::sqrt(max_metric);
    if (res.peak_correlation > 0.20f) {
        res.locked = true;
        int first_d = best_d % total_symbol_len;
        res.symbol_offset = first_d + gi_size_;

        float phase = std::arg(best_gamma);
        float subcarrier_spacing = sample_rate_ / static_cast<float>(fft_size_);
        res.ffo_hz = -(phase / (2.0f * static_cast<float>(M_PI))) * subcarrier_spacing;

        float rho = std::min(res.peak_correlation, 0.99f);
        float snr_linear = rho / (1.0f - rho + 1e-6f);
        res.snr_est_db = 10.0f * std::log10(snr_linear + 1e-6f);

        state_ = SyncState::Tracking;
        consecutive_failures_ = 0;
        expected_first_d_ = (first_d + total_symbol_len - static_cast<int>(num_samples % total_symbol_len)) % total_symbol_len;
        has_expected_d_ = true;
    } else {
        if (in_tracking) {
            consecutive_failures_++;
            expected_first_d_ = (expected_first_d_ + total_symbol_len - static_cast<int>(num_samples % total_symbol_len)) % total_symbol_len;
            if (consecutive_failures_ >= MAX_CONSECUTIVE_FAILURES) {
                state_ = SyncState::LostLock;
                has_expected_d_ = false;
            }
        }
    }

    return res;
}

int OfdmSync::fine_symbol_sync(const std::complex<float>* samples, int max_shift, float* phase_error_out) const {
    int best_shift = 0;
    float best_mag = -1.0f;
    std::complex<float> best_gamma(0.0f, 0.0f);

    for (int shift = -max_shift; shift <= max_shift; ++shift) {
        const std::complex<float>* s = samples + shift;
        std::complex<float> gamma(0.0f, 0.0f);
        for (int m = 0; m < gi_size_; ++m) {
            gamma += s[m] * std::conj(s[m + fft_size_]);
        }
        float mag = std::norm(gamma);
        if (mag > best_mag) {
            best_mag = mag;
            best_shift = shift;
            best_gamma = gamma;
        }
    }

    if (phase_error_out) {
        float subcarrier_spacing = sample_rate_ / static_cast<float>(fft_size_);
        *phase_error_out = -(std::arg(best_gamma) / (2.0f * static_cast<float>(M_PI))) * subcarrier_spacing;
    }
    return best_shift;
}

void OfdmSync::correct_frequency_offset(std::complex<float>* samples, size_t num_samples, float ffo_hz) {
    if (std::abs(ffo_hz) < 0.05f || num_samples == 0) return;

    double phase_step = -2.0 * M_PI * (static_cast<double>(ffo_hz) / static_cast<double>(sample_rate_));
    std::complex<double> rotator_step(std::cos(phase_step), std::sin(phase_step));
    std::complex<double> current_rotator(std::cos(static_cast<double>(current_phase_)),
                                         std::sin(static_cast<double>(current_phase_)));

    if (rot_vec_.size() < num_samples) {
        rot_vec_.resize(num_samples);
    }
    for (size_t i = 0; i < num_samples; ++i) {
        rot_vec_[i] = std::complex<float>(static_cast<float>(current_rotator.real()),
                                          static_cast<float>(current_rotator.imag()));
        current_rotator *= rotator_step;
        if ((i & 511) == 511) {
            double inv_mag = 1.0 / std::abs(current_rotator);
            current_rotator *= inv_mag;
        }
    }

    double end_phase = std::fmod(static_cast<double>(current_phase_) + phase_step * static_cast<double>(num_samples),
                                 2.0 * M_PI);
    if (end_phase > M_PI) end_phase -= 2.0 * M_PI;
    if (end_phase < -M_PI) end_phase += 2.0 * M_PI;
    current_phase_ = static_cast<float>(end_phase);

    // Acelera a multiplicacao complexa vetorial atraves do VOLK
    volk_32fc_x2_multiply_32fc(
        reinterpret_cast<lv_32fc_t*>(samples),
        reinterpret_cast<const lv_32fc_t*>(samples),
        reinterpret_cast<const lv_32fc_t*>(rot_vec_.data()),
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
