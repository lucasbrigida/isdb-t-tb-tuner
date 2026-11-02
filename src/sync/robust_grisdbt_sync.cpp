/*
 * robust_grisdbt_sync.cpp - Port otimizado em C++ puro do algoritmo de sincronismo
 * OFDM do gr-isdbt (https://github.com/git-artes/gr-isdbt).
 *
 * Copyright (C) 2014-2026 Universidad de la Republica (Montevideo, Uruguay),
 * gr-isdbt contributors, and Lucas Brigida.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "sync/robust_grisdbt_sync.h"
#include <iostream>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace isdbt {

RobustGRISDBTSync::RobustGRISDBTSync(const SyncParams& params)
    : params_(params),
      fft_size_(params.fft_size),
      gi_size_(params.gi_size),
      symbol_len_(params.fft_size + params.gi_size),
      stream_buf_(65536 * 4) {

    // Mensagem obrigatoria de inicializacao para o usuario final (R7)
    if (!announced_) {
        std::cerr << "[info] Modo reforcado ativado.\n";
        announced_ = true;
    }

    derot_buf_.reserve(65536 * 2);
    init_sp_tables();
    reset();
}

void RobustGRISDBTSync::init_sp_tables() {
    // No ISDB-T Modo 3 amostrado a Fs = 2031746 S/s (FFT de 2048 pontos),
    // o Segmento 0 contem 432 subportadoras.
    // Os pilotos dispersos (SP) ocorrem a cada 12 portadoras com deslocamento 3 * (l % 4).
    for (int l = 0; l < 4; ++l) {
        sp_indices_[l].clear();
        int offset = 3 * l;
        for (int k = offset; k < static_cast<int>(fft_size_); k += 12) {
            sp_indices_[l].push_back(k);
        }
    }
}

void RobustGRISDBTSync::reset() noexcept {
    stream_buf_.clear();
    nco_phase_ = 0.0;
    current_cfo_hz_ = 0.0f;
    sco_ppm_ = 0.0f;
    frac_timing_offset_ = 0.0;
    sco_integrator_ = 0.0;
    consecutive_good_symbols_ = 0;
    consecutive_bad_symbols_ = 0;
    symbol_counter_ = 0;
    set_robust_state(RobustSyncState::Acquisition);
}

void RobustGRISDBTSync::set_robust_state(RobustSyncState new_state) {
    if (robust_state_ != new_state) {
        state_transitions_++;
        robust_state_ = new_state;
        switch (new_state) {
            case RobustSyncState::Tracking:
                exposed_state_ = SyncState::Tracking;
                break;
            case RobustSyncState::LostLock:
                exposed_state_ = SyncState::LostLock;
                break;
            case RobustSyncState::Resync:
            case RobustSyncState::Acquisition:
            default:
                exposed_state_ = SyncState::Acquisition;
                break;
        }
    }
}

const SyncState& RobustGRISDBTSync::state() const noexcept {
    return exposed_state_;
}

float RobustGRISDBTSync::compute_cp_correlation(const std::complex<float>* ptr, size_t gi, size_t fft) const {
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

int RobustGRISDBTSync::refine_timing_sp(const std::complex<float>* symbol_time, float* out_metric) {
    // Estimador de timing fino de Van de Beek / gr-isdbt:
    // avalia correlacao do prefixo ciclico em janela estreita [-4 .. +4]
    int best_shift = 0;
    float best_corr = -1.0f;
    for (int sh = -4; sh <= 4; ++sh) {
        std::complex<float> g(0.0f, 0.0f);
        float pwr = 0.0f;
        for (size_t m = 0; m < gi_size_; ++m) {
            std::complex<float> a = symbol_time[sh + m];
            std::complex<float> b = symbol_time[sh + m + fft_size_];
            g += a * std::conj(b);
            pwr += 0.5f * (std::norm(a) + std::norm(b));
        }
        float metric = (pwr > 1e-12f) ? (std::abs(g) / pwr) : 0.0f;
        if (metric > best_corr) {
            best_corr = metric;
            best_shift = sh;
        }
    }
    if (out_metric) *out_metric = best_corr;
    return best_shift;
}

void RobustGRISDBTSync::process(const std::complex<float>* in, size_t n,
                               std::vector<std::complex<float>>& out) {
    out.clear();
    if (in == nullptr || n == 0) return;

    // Zero-allocation: redimensiona derot_buf_ se necessario (somente na inicializacao)
    if (derot_buf_.size() < n) {
        derot_buf_.resize(n);
    }

    // 1. Correcao de FFO/CFO e desrotacao rapida via fasor incremental (NCO)
    double phase_inc = -2.0 * M_PI * (static_cast<double>(current_cfo_hz_) / static_cast<double>(params_.sample_rate));
    std::complex<float> phase_step(static_cast<float>(std::cos(phase_inc)), static_cast<float>(std::sin(phase_inc)));
    std::complex<float> cur_phasor(static_cast<float>(std::cos(nco_phase_)), static_cast<float>(std::sin(nco_phase_)));

    for (size_t i = 0; i < n; ++i) {
        derot_buf_[i] = in[i] * cur_phasor;
        cur_phasor *= phase_step;
    }
    // Re-normaliza o fasor para evitar deriva de magnitude ao longo do tempo
    nco_phase_ += phase_inc * static_cast<double>(n);
    while (nco_phase_ > M_PI) nco_phase_ -= 2.0 * M_PI;
    while (nco_phase_ < -M_PI) nco_phase_ += 2.0 * M_PI;

    stream_buf_.append(derot_buf_.data(), n);

    // 2. Maquina de estados tripartite + Resync (R7.e)
    if (robust_state_ == RobustSyncState::Acquisition || robust_state_ == RobustSyncState::LostLock) {
        if (stream_buf_.available() < symbol_len_ * 2) {
            return;
        }

        // Busca global exaustiva por borda e GI
        size_t search_len = stream_buf_.available() - symbol_len_;
        float max_corr = -1.0f;
        size_t best_d = 0;
        std::complex<float> best_cross(0.0f, 0.0f);

        for (size_t d = 0; d < search_len; d += 2) {
            std::complex<float> cross(0.0f, 0.0f);
            float pwr = 0.0f;
            for (size_t m = 0; m < gi_size_; ++m) {
                std::complex<float> r1 = stream_buf_[d + m];
                std::complex<float> r2 = stream_buf_[d + m + fft_size_];
                cross += r1 * std::conj(r2);
                pwr += 0.5f * (std::norm(r1) + std::norm(r2));
            }
            float corr = (pwr > 1e-12f) ? (std::abs(cross) / pwr) : 0.0f;
            if (corr > max_corr) {
                max_corr = corr;
                best_d = d;
                best_cross = cross;
            }
        }

        if (max_corr >= 0.20f) {
            // Estima CFO fracionario inicial
            float angle = std::arg(best_cross);
            float ffo_hz = -angle / (2.0f * static_cast<float>(M_PI) * (static_cast<float>(fft_size_) / params_.sample_rate));
            current_cfo_hz_ = ffo_hz;

            best_d = best_d % symbol_len_;
            frac_timing_offset_ = static_cast<double>(best_d);
            consecutive_good_symbols_ = 1;
            consecutive_bad_symbols_ = 0;
            set_robust_state(RobustSyncState::Tracking);
        } else {
            // Continua em Acquisition, descarta excesso
            if (stream_buf_.available() > symbol_len_ * 4) {
                stream_buf_.consume(symbol_len_);
            }
            return;
        }
    }

    // 3. Regime de Tracking / Resync / LostLock
    if (robust_state_ == RobustSyncState::Tracking ||
        robust_state_ == RobustSyncState::Resync ||
        robust_state_ == RobustSyncState::LostLock) {
        while (static_cast<size_t>(std::lround(frac_timing_offset_)) + symbol_len_ + 8 <= stream_buf_.available()) {
            size_t base_idx = static_cast<size_t>(std::lround(frac_timing_offset_));
            float metric = 0.0f;
            int fine_sh = refine_timing_sp(stream_buf_.data_at(base_idx), &metric);

            bool is_good = (metric >= params_.min_correlation);
            if (is_good) {
                consecutive_good_symbols_++;
                consecutive_bad_symbols_ = 0;

                // Rastreamento fino de timing com ganho suave
                frac_timing_offset_ += 0.25 * static_cast<double>(fine_sh);
                size_t sym_start = static_cast<size_t>(std::max<int>(0, static_cast<int>(std::lround(frac_timing_offset_))));
                if (sym_start + symbol_len_ > stream_buf_.available()) break;

                // Malha PI de Sampling Clock Offset (SCO)
                double sco_err = static_cast<double>(fine_sh);
                sco_integrator_ += 0.005 * sco_err;
                double sco_step = 0.05 * sco_err + sco_integrator_;
                frac_timing_offset_ += sco_step;
                sco_ppm_ = static_cast<float>(sco_integrator_ * 1e6 / static_cast<double>(symbol_len_));

                if (robust_state_ == RobustSyncState::LostLock) {
                    set_robust_state(RobustSyncState::Resync);
                }

                if (robust_state_ == RobustSyncState::Resync) {
                    // R7.e: Confirmacao de re-lock apos 3 simbolos consecutivos com metrica valida
                    if (consecutive_good_symbols_ >= 3) {
                        resync_recovered_++;
                        set_robust_state(RobustSyncState::Tracking);
                    }
                }

                // Extrai as fft_size_ amostras uteis (pos-GI) para saida
                const std::complex<float>* useful = stream_buf_.data_at(sym_start + gi_size_);
                out.insert(out.end(), useful, useful + fft_size_);
            } else {
                consecutive_bad_symbols_++;
                consecutive_good_symbols_ = 0;

                if (robust_state_ == RobustSyncState::Tracking) {
                    if (consecutive_bad_symbols_ >= 3) {
                        set_robust_state(RobustSyncState::Resync);
                    }
                } else if (robust_state_ == RobustSyncState::Resync) {
                    if (consecutive_bad_symbols_ >= 6) {
                        resync_fallback_++;
                        set_robust_state(RobustSyncState::LostLock);
                    }
                }
            }

            frac_timing_offset_ += static_cast<double>(symbol_len_);
            symbol_counter_++;
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
