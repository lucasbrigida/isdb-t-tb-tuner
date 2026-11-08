/*
 * sp_cir_sync.cpp - Sincronizador OFDM de dois estagios baseado em Resposta ao Impulso
 * de Canal por Pilotos Dispersos (SP-CIR), fundamentado em Speth & Meyr (IEEE Trans.
 * Comm., 1999/2001) e gr-isdbt (Larroca et al., 2014).
 *
 * Copyright (C) 2014-2026 Universidad de la Republica (Montevideo, Uruguay),
 * gr-isdbt contributors, and Lucas Brigida.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "sync/sp_cir_sync.h"
#include <volk/volk.h>
#include <cstring>
#include <iostream>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace isdbt {

SpCirSynchronizer::SpCirSynchronizer(const SyncParams& params)
    : params_(params),
      fft_size_(params.fft_size),
      gi_size_(params.gi_size),
      symbol_len_(params.fft_size + params.gi_size),
      stream_buf_(65536 * 4) {

    // Mensagem de inicializacao para o usuario final
    if (!announced_) {
        std::cerr << "[info] Modo reforcado ativado (SP-CIR Synchronizer).\n";
        announced_ = true;
    }

    derot_buf_.reserve(65536 * 2);
    init_sp_tables();

    // Planos FFTW3 pre-alocados para estimativa SP e CIR (Zero alocacoes no loop quente)
    fft_plan_ = fftwf_plan_dft_1d(
        2048,
        reinterpret_cast<fftwf_complex*>(fft_in_.data()),
        reinterpret_cast<fftwf_complex*>(fft_out_.data()),
        FFTW_FORWARD,
        FFTW_ESTIMATE
    );
    ifft_cir_plan_ = fftwf_plan_dft_1d(
        64,
        reinterpret_cast<fftwf_complex*>(cir_in_.data()),
        reinterpret_cast<fftwf_complex*>(cir_out_.data()),
        FFTW_BACKWARD,
        FFTW_ESTIMATE
    );

    window_gamma_.resize(8 * (fft_size_ + fft_size_ / 4));
    window_energy_.resize(8 * (fft_size_ + fft_size_ / 4));
    reset();
}

SpCirSynchronizer::~SpCirSynchronizer() {
    if (fft_plan_) {
        fftwf_destroy_plan(fft_plan_);
        fft_plan_ = nullptr;
    }
    if (ifft_cir_plan_) {
        fftwf_destroy_plan(ifft_cir_plan_);
        ifft_cir_plan_ = nullptr;
    }
}

void SpCirSynchronizer::init_sp_tables() {
    // Gerador PRBS de 11 bits (1 + X^9 + X^11) conforme ARIB STD-B31 e gr-isdbt:
    // O registrador de 11 bits desloca para a direita, tap X^9 no bit 2 (11 - 9 = 2)
    uint32_t reg_prbs = (1 << 11) - 1;
    std::vector<float> full_prbs(5617);
    for (size_t k = 0; k < 5617; ++k) {
        uint32_t aux = reg_prbs & 0x01;
        uint32_t new_bit = ((reg_prbs >> 2) ^ (reg_prbs >> 0)) & 0x01;
        reg_prbs = (reg_prbs >> 1) | (new_bit << 10);
        full_prbs[k] = (4.0f * 2.0f * (0.5f - static_cast<float>(aux))) / 3.0f;
    }

    // No ISDB-T Modo 3 amostrado a Fs = 2031746 S/s (FFT de 2048 pontos),
    // o Segmento 0 contem 432 subportadoras ativas centradas no bin 808 ( (2048 - 432)/2 ).
    // O Segmento 0 inicia na portadora global 2592 (6 * 432).
    constexpr int BASE_SEG_OFFSET = (2048 - 432) / 2; // 808
    for (int s = 0; s < 4; ++s) {
        sp_indices_[s].clear();
        sp_prbs_[s].clear();
        for (int l = 3 * s; l < 432; l += 12) {
            int centered_idx = BASE_SEG_OFFSET + l;
            float prbs_val = full_prbs[2592 + l];
            float inv_prbs = (prbs_val > 0.0f) ? 0.75f : -0.75f;
            sp_indices_[s].push_back(centered_idx);
            sp_prbs_[s].push_back(inv_prbs);
        }
    }
}

void SpCirSynchronizer::reset() noexcept {
    stream_buf_.clear();
    nco_phase_ = 0.0;
    current_cfo_hz_ = 0.0f;
    peak_correlation_ = 0.0f;
    sco_ppm_ = 0.0f;
    frac_timing_offset_ = 0.0;
    sco_integrator_ = 0.0;
    consecutive_good_symbols_ = 0;
    consecutive_bad_symbols_ = 0;
    symbol_counter_ = 0;
    sp_phase_locked_ = false;
    sp_tmcc_anchored_ = false;
    is_sp_active_ = false;
    sp_phase_offset_ = 0;
    sp_consecutive_drops_ = 0;
    ifo_bins_ = 0;
    pdp_avg_.fill(0.0f);
    pdp_initialized_ = false;
    block_cp_gamma_ = std::complex<float>(0.0f, 0.0f);
    block_cp_power_ = 0.0f;
    set_sp_cir_state(SpCirSyncState::Acquisition);
}

const char* SpCirSynchronizer::detailed_state_str() const noexcept {
    switch (sp_cir_state_) {
        case SpCirSyncState::Acquisition: return "Acquisition";
        case SpCirSyncState::Tracking:
            if (is_sp_active_) {
                return sp_tmcc_anchored_ ? "Tracking [TMCC+SP]" : "Tracking [SP]";
            }
            return "Tracking [CP]";
        case SpCirSyncState::LostLock:    return "LostLock";
        case SpCirSyncState::Resync:      return "Resync";
        default:                          return "Unknown";
    }
}

void SpCirSynchronizer::notify_frame_sync(int symbol_idx_in_frame, int ifo_bins) noexcept {
    // No ISDB-T (ARIB STD-B31 / ABNT NBR 15601), o simbolo 0 do quadro TMCC corresponde
    // rigorosamente a fase 0 dos pilotos dispersos (l = 0). O simbolo s tem fase de SP = s mod 4.
    int target_phase = symbol_idx_in_frame & 3;
    sp_phase_offset_ = (target_phase - (symbol_counter_ & 3)) & 3;
    ifo_bins_ = ifo_bins;
    sp_phase_locked_ = true;
    sp_tmcc_anchored_ = true;
    sp_consecutive_drops_ = 0;
}

void SpCirSynchronizer::set_sp_cir_state(SpCirSyncState new_state) {
    if (sp_cir_state_ != new_state) {
        state_transitions_++;
        sp_cir_state_ = new_state;
        switch (new_state) {
            case SpCirSyncState::Tracking:
                exposed_state_ = SyncState::Tracking;
                break;
            case SpCirSyncState::LostLock:
                exposed_state_ = SyncState::LostLock;
                break;
            case SpCirSyncState::Resync:
            case SpCirSyncState::Acquisition:
            default:
                exposed_state_ = SyncState::Acquisition;
                break;
        }
    }
}

const SyncState& SpCirSynchronizer::state() const noexcept {
    return exposed_state_;
}

float SpCirSynchronizer::compute_cp_correlation(const std::complex<float>* ptr, size_t gi, size_t fft) const {
    std::complex<float> sum_cross(0.0f, 0.0f);
    float sum_pwr = 0.0f;
    for (size_t m = 0; m < gi; ++m) {
        std::complex<float> r1 = ptr[m];
        std::complex<float> r2 = ptr[m + fft];
        sum_cross += r1 * std::conj(r2);
        sum_pwr += 0.5f * (std::norm(r1) + std::norm(r2));
    }
    if (sum_pwr <= 1e-12f) return 0.0f;
    return std::abs(sum_cross) / sum_pwr;
}

int SpCirSynchronizer::detect_first_path(const std::complex<float>* cir_data, size_t len, float* out_metric) {
    if (!cir_data || len == 0) {
        if (out_metric) *out_metric = 0.0f;
        return 0;
    }

    std::array<float, 64> pwr_cir{};
    float max_pwr = 0.0f;
    float total_pwr = 0.0f;
    int max_idx = 0;

    for (size_t t = 0; t < len && t < 64; ++t) {
        float p = std::norm(cir_data[t]);
        pwr_cir[t] = p;
        total_pwr += p;
        if (p > max_pwr) {
            max_pwr = p;
            max_idx = static_cast<int>(t);
        }
    }

    if (total_pwr <= 1e-12f || max_pwr <= 1e-12f) {
        if (out_metric) *out_metric = 0.0f;
        return 0;
    }

    float mean_pwr = total_pwr / static_cast<float>(len);
    float papr = max_pwr / mean_pwr;

    // Se o PAPR for menor que 12.0f, a CIR e indistinguivel de ruido gaussiano ou dados aleatorios.
    // Rejeita categoricamente como nao-piloto.
    if (papr < 12.0f) {
        if (out_metric) *out_metric = 0.0f;
        return 0;
    }

    // Piloto coerente confirmado: metrica normalizada entre 0.40f e 1.0f
    float metric = std::min(1.0f, papr / 25.0f);
    if (out_metric) *out_metric = metric;

    // Converte indice para tau centrado [-32 .. +31]
    int tau_peak = (max_idx >= 32) ? (max_idx - 64) : max_idx;

    // Deteccao do Primeiro Caminho (First Arrival Path) em multipercurso urbano (Speth & Meyr):
    // Busca no intervalo [-16 .. +16] o primeiro pico que atinja pelo menos 25% de max_pwr
    int first_path_tau = tau_peak;
    float thresh = 0.25f * max_pwr;
    for (int tau = -16; tau <= 16; ++tau) {
        int idx = (tau < 0) ? (tau + 64) : tau;
        if (pwr_cir[idx] >= thresh) {
            first_path_tau = tau;
            break;
        }
    }

    return first_path_tau;
}

int SpCirSynchronizer::refine_timing_sp(const std::complex<float>* symbol_time, float* out_metric, int max_neg_shift) {
    // 1. Se ancorado ao TMCC pelo demodulador, executa rastreamento fino ativo por Pilotos Dispersos (SP-CIR)
    if (sp_tmcc_anchored_) {
        // Acumula fase coerente do CP na janela nominal (sh=0) para rastreamento continuo de CFO em bloco
        std::complex<float> nom_g(0.0f, 0.0f);
        float nom_pwr = 0.0f;
        for (size_t m = 0; m < gi_size_; ++m) {
            std::complex<float> r1 = symbol_time[m];
            std::complex<float> r2 = symbol_time[m + fft_size_];
            nom_g += r1 * std::conj(r2);
            nom_pwr += 0.5f * (std::norm(r1) + std::norm(r2));
        }
        block_cp_gamma_ += nom_g;
        block_cp_power_ += nom_pwr;

        // Atualiza a estimativa de canal CIR por SP a cada 2 simbolos (~466 Hz),
        // taxa mais que suficiente para rastrear multipercurso urbano com orcamento de CPU controlado
        if ((symbol_counter_ & 1) == 0 && fft_plan_ && ifft_cir_plan_) {
            const std::complex<float>* useful = symbol_time + gi_size_;
            std::memcpy(fft_in_.data(), useful, 2048 * sizeof(std::complex<float>));
            fftwf_execute(fft_plan_);

            float norm = 1.0f / std::sqrt(2048.0f);
            auto get_centered_carrier = [&](int centered_idx) -> std::complex<float> {
                int raw_idx = (centered_idx < 1024) ? (centered_idx + 1024) : (centered_idx - 1024);
                return std::complex<float>(fft_out_[raw_idx].real() * norm, fft_out_[raw_idx].imag() * norm);
            };

            int current_phase = (symbol_counter_ + sp_phase_offset_) & 3;
            const auto& indices = sp_indices_[current_phase];
            const auto& inv_prbs = sp_prbs_[current_phase];
            std::fill(cir_in_.begin(), cir_in_.end(), std::complex<float>(0.0f, 0.0f));
            for (size_t p = 0; p < indices.size() && p < 36; ++p) {
                int bin = indices[p] + ifo_bins_;
                if (bin < 0 || bin >= 2048) continue;
                std::complex<float> y = get_centered_carrier(bin);
                cir_in_[p] = y * inv_prbs[p];
            }
            fftwf_execute(ifft_cir_plan_);

            if (!pdp_initialized_) {
                for (size_t t = 0; t < 64; ++t) {
                    pdp_avg_[t] = std::norm(cir_out_[t]);
                }
                pdp_initialized_ = true;
            } else {
                for (size_t t = 0; t < 64; ++t) {
                    pdp_avg_[t] = 0.85f * pdp_avg_[t] + 0.15f * std::norm(cir_out_[t]);
                    cir_out_[t] = std::complex<float>(std::sqrt(pdp_avg_[t]), 0.0f);
                }
            }

            float cir_metric = 0.0f;
            int cir_shift = detect_first_path(cir_out_.data(), 64, &cir_metric);

            if (cir_metric >= 0.40f) {
                sp_consecutive_drops_ = 0;
                is_sp_active_ = true;
                if (out_metric) *out_metric = cir_metric;
                return std::clamp(cir_shift, -4, 4);
            }

            sp_consecutive_drops_++;
        } else if (is_sp_active_) {
            // No simbolo intercalado, mantem o tracking de SP por inercia nominal (shift 0)
            if (out_metric) *out_metric = std::max(peak_correlation_, params_.min_correlation);
            return 0;
        }
    }

    // 2. Fallback transparente para o estimador por Prefixo Ciclico (CP) para timing de simbolo:
    // Conforme a literatura e Invariante 12 do AGENTS.md, garante que o sincronizador
    // nao perca o tracking de simbolo antes da ancoragem ao TMCC ou durante desvanecimentos severos.
    // NOTA ARQUITETURAL (ADR 028): A correlacao de CP aqui avalia ESTRITAMENTE a borda de timing,
    // NUNCA integrando fase residual de CFO, prevenindo deriva de NCO sob multipercurso urbano.
    is_sp_active_ = false;
    int max_neg = std::clamp(max_neg_shift, 0, 4);
    int best_shift = 0;
    float best_mag = -1.0f;
    std::complex<float> best_gamma(0.0f, 0.0f);
    float best_power = 1.0f;
    for (int sh = -max_neg; sh <= 4; ++sh) {
        std::complex<float> sum_g(0.0f, 0.0f);
        float sum_pwr = 0.0f;
        for (size_t m = 0; m < gi_size_; ++m) {
            std::complex<float> r1 = symbol_time[sh + m];
            std::complex<float> r2 = symbol_time[sh + m + fft_size_];
            sum_g += r1 * std::conj(r2);
            sum_pwr += 0.5f * (std::norm(r1) + std::norm(r2));
        }
        float mag = std::norm(sum_g);
        if (mag > best_mag) {
            best_mag = mag;
            best_shift = sh;
            best_gamma = sum_g;
            best_power = sum_pwr;
        }
    }
    block_cp_gamma_ += best_gamma;
    block_cp_power_ += best_power;
    float rho = (best_power > 1e-6f) ? (std::abs(best_gamma) / best_power) : 0.0f;
    if (out_metric) *out_metric = rho;

    return best_shift;
}

void SpCirSynchronizer::process(const std::complex<float>* in, size_t n,
                               std::vector<std::complex<float>>& out) {
    out.clear();
    if (in == nullptr || n == 0) return;
    peak_correlation_ = 0.0f;

    // Zero-allocation: redimensiona derot_buf_ se necessario (somente na inicializacao)
    if (derot_buf_.size() < n) {
        derot_buf_.resize(n);
    }

    // 1. Correcao de CFO e desrotacao rapida via fasor incremental (NCO)
    double phase_inc = -2.0 * M_PI * (static_cast<double>(current_cfo_hz_) / static_cast<double>(params_.sample_rate));
    std::complex<float> phase_step(static_cast<float>(std::cos(phase_inc)), static_cast<float>(std::sin(phase_inc)));
    std::complex<float> cur_rot(static_cast<float>(std::cos(nco_phase_)), static_cast<float>(std::sin(nco_phase_)));

    for (size_t i = 0; i < n; ++i) {
        derot_buf_[i] = in[i] * cur_rot;
        cur_rot *= phase_step;
    }
    nco_phase_ = std::fmod(nco_phase_ + phase_inc * static_cast<double>(n), 2.0 * M_PI);

    stream_buf_.append(derot_buf_.data(), n);

    // 2. Maquina de estados: Aquisicao inicial (Estagio 1 de Speth & Meyr via Prefixo Ciclico)
    if (sp_cir_state_ == SpCirSyncState::Acquisition || sp_cir_state_ == SpCirSyncState::LostLock) {
        const size_t max_gi_size = fft_size_ / 4;
        const size_t max_symbol_len = fft_size_ + max_gi_size;
        if (stream_buf_.available() < max_symbol_len * 2) {
            return;
        }

        // Adquire o intervalo de guarda por correlacao dobrada em simbolos consecutivos.
        const GuardInterval candidates[] = {
            GuardInterval::GI_1_16,
            GuardInterval::GI_1_8,
            GuardInterval::GI_1_4,
            GuardInterval::GI_1_32
        };
        const size_t corr_len = stream_buf_.available() - fft_size_;
        float max_corr = -1.0f;
        size_t best_d = 0;
        size_t best_gi_size = gi_size_;
        std::complex<float> best_cross(0.0f, 0.0f);

        for (GuardInterval candidate : candidates) {
            const size_t candidate_gi = fft_size_ / static_cast<size_t>(candidate);
            const size_t candidate_symbol_len = fft_size_ + candidate_gi;
            size_t num_folds = (corr_len - candidate_gi) / candidate_symbol_len;
            if (num_folds < 2) continue;
            num_folds = std::min<size_t>(num_folds, 8);

            const size_t window_len = num_folds * candidate_symbol_len;
            std::complex<float> cross(0.0f, 0.0f);
            float power = 0.0f;
            for (size_t m = 0; m < candidate_gi; ++m) {
                const auto r1 = stream_buf_[m];
                const auto r2 = stream_buf_[m + fft_size_];
                cross += r1 * std::conj(r2);
                power += 0.5f * (std::norm(r1) + std::norm(r2));
            }

            for (size_t d = 0; d < window_len; ++d) {
                window_gamma_[d] = cross;
                window_energy_[d] = power;
                const auto old_r1 = stream_buf_[d];
                const auto old_r2 = stream_buf_[d + fft_size_];
                const auto new_r1 = stream_buf_[d + candidate_gi];
                const auto new_r2 = stream_buf_[d + candidate_gi + fft_size_];
                cross += new_r1 * std::conj(new_r2) - old_r1 * std::conj(old_r2);
                power += 0.5f * (std::norm(new_r1) + std::norm(new_r2)
                               - std::norm(old_r1) - std::norm(old_r2));
            }

            for (size_t d0 = 0; d0 < candidate_symbol_len; ++d0) {
                std::complex<float> folded_cross(0.0f, 0.0f);
                float folded_power = 0.0f;
                for (size_t k = 0; k < num_folds; ++k) {
                    const size_t index = d0 + k * candidate_symbol_len;
                    folded_cross += window_gamma_[index];
                    folded_power += window_energy_[index];
                }
                const float corr = (folded_power > 1e-12f)
                    ? std::abs(folded_cross) / folded_power
                    : 0.0f;
                if (corr > max_corr) {
                    max_corr = corr;
                    best_d = d0;
                    best_gi_size = candidate_gi;
                    best_cross = folded_cross;
                }
            }
        }
        peak_correlation_ = std::max(0.0f, max_corr);

        if (max_corr >= 0.20f) {
            // Estima CFO fracionario inicial (Van de Beek ML estimator)
            float angle = std::arg(best_cross);
            float ffo_hz = -angle / (2.0f * static_cast<float>(M_PI) * (static_cast<float>(fft_size_) / params_.sample_rate));
            current_cfo_hz_ = ffo_hz;

            // Retroativamente compensa o CFO no buffer acumulado durante a aquisicao
            double retro_inc = -2.0 * M_PI * (static_cast<double>(current_cfo_hz_) / static_cast<double>(params_.sample_rate));
            std::complex<float> retro_step(static_cast<float>(std::cos(retro_inc)), static_cast<float>(std::sin(retro_inc)));
            std::complex<float> retro_rot(1.0f, 0.0f);
            for (size_t i = 0; i < stream_buf_.available(); ++i) {
                stream_buf_[i] = stream_buf_[i] * retro_rot;
                retro_rot *= retro_step;
            }
            nco_phase_ = std::fmod(retro_inc * static_cast<double>(stream_buf_.available()), 2.0 * M_PI);

            gi_size_ = best_gi_size;
            symbol_len_ = fft_size_ + gi_size_;
            frac_timing_offset_ = static_cast<double>(best_d);
            consecutive_good_symbols_ = 1;
            consecutive_bad_symbols_ = 0;
            set_sp_cir_state(SpCirSyncState::Tracking);
        } else {
            // Continua em Acquisition, descarta excesso
            if (stream_buf_.available() > max_symbol_len * 4) {
                stream_buf_.consume(max_symbol_len);
            }
            return;
        }
    }

    // 3. Regime de Tracking / Resync / LostLock (Estagio 2 de Speth & Meyr via SP-CIR)
    if (sp_cir_state_ == SpCirSyncState::Tracking ||
        sp_cir_state_ == SpCirSyncState::Resync ||
        sp_cir_state_ == SpCirSyncState::LostLock) {
        block_cp_gamma_ = std::complex<float>(0.0f, 0.0f);
        block_cp_power_ = 0.0f;

        while (static_cast<size_t>(std::lround(frac_timing_offset_)) + symbol_len_ + 8 <= stream_buf_.available()) {
            size_t base_idx = static_cast<size_t>(std::lround(frac_timing_offset_));
            int max_neg_shift = static_cast<int>(std::min<size_t>(4, base_idx));
            float metric = 0.0f;
            int fine_sh = refine_timing_sp(stream_buf_.data_at(base_idx), &metric, max_neg_shift);
            peak_correlation_ = metric;

            bool is_good = (metric >= params_.min_correlation);
            if (is_good) {
                consecutive_good_symbols_++;
                consecutive_bad_symbols_ = 0;

                // Rastreamento fino de timing com ganho suave e restrito
                int clamped_sh = std::clamp(fine_sh, -max_neg_shift, 4);
                frac_timing_offset_ += 0.25 * static_cast<double>(clamped_sh);
                size_t sym_start = static_cast<size_t>(std::max<int>(0, static_cast<int>(std::lround(frac_timing_offset_))));
                if (sym_start + symbol_len_ > stream_buf_.available()) break;

                // Malha PI de Sampling Clock Offset (SCO) estavel com anti-windup e leakage:
                // Integra e atualiza SCO exclusivamente quando o piloto SP estiver ativo e com alta confianca
                // para NUNCA acumular ruido do prefixo ciclico!
                if (is_sp_active_ && sp_tmcc_anchored_) {
                    double sco_err = static_cast<double>(clamped_sh);
                    sco_integrator_ = 0.999 * sco_integrator_ + 0.0001 * sco_err;
                    sco_integrator_ = std::clamp(sco_integrator_, -0.05, 0.05); // Limite fisico ~23 ppm
                    double sco_step = sco_integrator_;
                    frac_timing_offset_ += sco_step;
                }
                sco_ppm_ = static_cast<float>(sco_integrator_ * 1e6 / static_cast<double>(symbol_len_));

                if (sp_cir_state_ == SpCirSyncState::LostLock) {
                    set_sp_cir_state(SpCirSyncState::Resync);
                }

                if (sp_cir_state_ == SpCirSyncState::Resync) {
                    // Confirmacao de re-lock apos 3 simbolos consecutivos com metrica valida
                    if (consecutive_good_symbols_ >= 3) {
                        resync_recovered_++;
                        set_sp_cir_state(SpCirSyncState::Tracking);
                    }
                }

                // Extrai as fft_size_ amostras uteis (pos-GI) para saida
                const std::complex<float>* useful = stream_buf_.data_at(sym_start + gi_size_);
                out.insert(out.end(), useful, useful + fft_size_);
            } else {
                consecutive_bad_symbols_++;
                consecutive_good_symbols_ = 0;

                // Em flywheel (desvanecimento transiente): extrai o simbolo no timing nominal
                // se o desvanecimento for recente (< 6 simbolos ~ 6ms), mantendo a entrega ao FEC
                if (consecutive_bad_symbols_ < 6 && (sp_cir_state_ == SpCirSyncState::Tracking)) {
                    size_t sym_start = static_cast<size_t>(std::max<int>(0, static_cast<int>(std::lround(frac_timing_offset_))));
                    if (sym_start + symbol_len_ <= stream_buf_.available()) {
                        const std::complex<float>* useful = stream_buf_.data_at(sym_start + gi_size_);
                        out.insert(out.end(), useful, useful + fft_size_);
                    }
                }

                if (sp_cir_state_ == SpCirSyncState::Tracking) {
                    if (consecutive_bad_symbols_ >= 4) {
                        set_sp_cir_state(SpCirSyncState::Resync);
                    }
                } else if (sp_cir_state_ == SpCirSyncState::Resync) {
                    if (consecutive_bad_symbols_ >= 8) {
                        resync_fallback_++;
                        set_sp_cir_state(SpCirSyncState::LostLock);
                    }
                }
            }

            frac_timing_offset_ += static_cast<double>(symbol_len_);
            symbol_counter_++;
        }

        // Atualiza CFO residual uma vez por bloco usando a soma coerente de CP nos simbolos do bloco
        // com zona morta (deadband de 0.5 Hz) para estabilidade assintotica sem deriva sob multipercurso (Opcao 2)
        if (block_cp_power_ > 1e-6f) {
            float block_rho = std::abs(block_cp_gamma_) / block_cp_power_;
            if (block_rho >= 0.20f) {
                float angle = std::arg(block_cp_gamma_);
                float spacing = static_cast<float>(params_.sample_rate) / static_cast<float>(fft_size_);
                float ffo_res = -(angle / (2.0f * static_cast<float>(M_PI))) * spacing;
                // Deadband quando ancorado ao TMCC: impede que vieses estaticos de multipercurso
                // causem deriva continua do NCO em regime permanente
                if (sp_tmcc_anchored_ && std::abs(ffo_res) < 0.5f) {
                    ffo_res = 0.0f;
                }
                float cfo_gain = sp_tmcc_anchored_ ? 0.08f : 0.20f;
                current_cfo_hz_ += cfo_gain * ffo_res;
            }
        }

        // Descarta amostras processadas do buffer deslizante
        int consume_count = static_cast<int>(std::floor(frac_timing_offset_)) - 8;
        if (consume_count > 0 && static_cast<size_t>(consume_count) <= stream_buf_.available()) {
            stream_buf_.consume(static_cast<size_t>(consume_count));
            frac_timing_offset_ -= static_cast<double>(consume_count);
        }
    }
}

} // namespace isdbt
