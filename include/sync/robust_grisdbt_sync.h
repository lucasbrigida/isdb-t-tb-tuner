/*
 * robust_grisdbt_sync.h - Port otimizado em C++ puro do algoritmo de sincronismo
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

#pragma once

#include "sync/iofdm_synchronizer.h"
#include "sync/sync_params.h"
#include "dsp/iq_stream_buffer.hpp"
#include <vector>
#include <complex>
#include <cstdint>

namespace isdbt {

/**
 * @brief Maquina de estados detalhada do sincronizador robusto (R7.e).
 */
enum class RobustSyncState {
    Acquisition, ///< Aquisicao coarse exaustiva via correlacao de CP
    Tracking,    ///< Rastreamento fino ativo baseado em CP e ajuste de SCO
    LostLock,    ///< Perda de sincronismo por degradacao de metricas
    Resync       ///< Re-sincronizacao rapida com histerese de 3 simbolos
};

/**
 * @brief Implementacao robusta do sincronizador OFDM portado do gr-isdbt (R7).
 *
 * Caracteristicas:
 * - Aquisicao coarse de borda e estimativa de FFO por prefixo ciclico.
 * - Refinamento fino de timing e estimativa inicial de CFO pela correlacao do CP.
 * - Malha de segunda ordem (PI) para rastreamento de timing/SCO.
 * - FSM de 4 estados com confirmacao de re-lock apos 3 simbolos validos consecutivos.
 * - Zero alocacoes dinamicas no heap durante o caminho critico (process).
 */
class RobustGRISDBTSync : public IOFDMSynchronizer {
public:
    explicit RobustGRISDBTSync(const SyncParams& params = SyncParams{});
    ~RobustGRISDBTSync() override = default;

    void process(const std::complex<float>* in, size_t n,
                 std::vector<std::complex<float>>& out) override;

    const SyncState& state() const noexcept override;
    float peak_correlation() const noexcept override { return peak_correlation_; }
    RobustSyncState robust_state() const noexcept { return robust_state_; }

    void reset() noexcept override;

    // Metricas operacionais especificas do modo 2 (N6)
    uint64_t state_transitions() const noexcept { return state_transitions_; }
    uint64_t resync_recovered() const noexcept { return resync_recovered_; }
    uint64_t resync_fallback() const noexcept { return resync_fallback_; }
    float estimated_sco_ppm() const noexcept { return sco_ppm_; }
    float estimated_cfo_hz() const noexcept { return current_cfo_hz_; }
    size_t guard_interval_size() const noexcept { return gi_size_; }

private:
    SyncParams params_;
    mutable SyncState exposed_state_ = SyncState::Acquisition;
    RobustSyncState robust_state_ = RobustSyncState::Acquisition;

    size_t fft_size_ = 2048;
    size_t gi_size_ = 128;
    size_t symbol_len_ = 2176;

    // Buffers pre-alocados para zero alocacao no hot path (N5)
    IqStreamBuffer stream_buf_;
    std::vector<std::complex<float>> derot_buf_;
    std::vector<std::complex<float>> window_gamma_;
    std::vector<float> window_energy_;
    // Variaveis de rastreamento de fase e timing
    double nco_phase_ = 0.0;
    float current_cfo_hz_ = 0.0f;
    float peak_correlation_ = 0.0f;
    float sco_ppm_ = 0.0f;
    double frac_timing_offset_ = 0.0;
    double sco_integrator_ = 0.0;

    int consecutive_good_symbols_ = 0;
    int consecutive_bad_symbols_ = 0;
    int symbol_counter_ = 0;
    bool announced_ = false;

    // Metricas
    uint64_t state_transitions_ = 0;
    uint64_t resync_recovered_ = 0;
    uint64_t resync_fallback_ = 0;

    void set_robust_state(RobustSyncState new_state);
    float compute_cp_correlation(const std::complex<float>* ptr, size_t gi, size_t fft) const;
    int refine_timing_cp(const std::complex<float>* symbol_time, size_t left_context,
                         float* out_metric = nullptr);
};

} // namespace isdbt
