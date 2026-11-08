/*
 * sp_cir_sync.h - Sincronizador OFDM de dois estagios baseado em Resposta ao Impulso
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

#pragma once

#include "sync/iofdm_synchronizer.h"
#include "sync/sync_params.h"
#include "dsp/iq_stream_buffer.hpp"
#include <vector>
#include <complex>
#include <array>
#include <cstdint>
#include <fftw3.h>

namespace isdbt {

/**
 * @brief Maquina de estados detalhada do sincronizador SP-CIR.
 */
enum class SpCirSyncState {
    Acquisition, ///< Aquisicao coarse exaustiva via correlacao de CP e estimativa inicial de CFO
    Tracking,    ///< Rastreamento fino ativo guiado por Pilotos Dispersos (SP-CIR) e SCO
    LostLock,    ///< Perda de sincronismo por degradacao severa de metricas
    Resync       ///< Re-sincronizacao com histerese de confirmacao (3 simbolos)
};

// Alias de retrocompatibilidade
using RobustSyncState = SpCirSyncState;

/**
 * @brief Sincronizador OFDM de alta estabilidade baseado em SP-CIR (Speth & Meyr / Larroca et al.).
 *
 * Caracteristicas arquiteturais fundamentadas na literatura formal:
 * 1. Dois estagios (Speth & Meyr 1999/2001):
 *    - Estagio 1 (Dominio do tempo): Aquisicao inicial de borda de simbolo e estimativa fracionaria
 *      de CFO via correlacao do Prefixo Ciclico (Van de Beek et al., 1997).
 *    - Estagio 2 (Dominio da frequencia): Rastreamento fino contínuo de timing por IDFT de 64 pontos
 *      sobre os 36 Pilotos Dispersos (SP) do Segmento 0, com deteccao do Primeiro Caminho
 *      (First Arrival Path, tau em [-16..+16], >= 25% P_max) para imunidade contra multipercurso urbano.
 * 2. Imunidade a Deriva de CFO sob Multipercurso Urbano:
 *    - Em regime de tracking contínuo, elimina qualquer integracao de fase por Prefixo Ciclico,
 *      impedindo que vieses de multipercurso urbano desloquem o NCO com o tempo.
 * 3. Zero Alocacoes Dinamicas no Heap:
 *    - Planos FFTW3 e buffers I/Q alocados estaticamente na inicializacao.
 */
class SpCirSynchronizer : public IOFDMSynchronizer {
public:
    // Dimensoes fixas do motor SP-CIR (Modo 3 / Segmento 0); fft_size diferente e rejeitado no construtor
    static constexpr size_t kSpFftSize = 2048;
    static constexpr size_t kCirSize = 64;

    explicit SpCirSynchronizer(const SyncParams& params = SyncParams{});
    ~SpCirSynchronizer() override;

    void process(const std::complex<float>* in, size_t n,
                 std::vector<std::complex<float>>& out) override;

    const SyncState& state() const noexcept override;
    float peak_correlation() const noexcept override { return peak_correlation_; }
    SpCirSyncState sp_cir_state() const noexcept { return sp_cir_state_; }

    // Compatibilidade com APIs anteriores
    SpCirSyncState robust_state() const noexcept { return sp_cir_state_; }

    void reset() noexcept override;

    // Metricas operacionais
    uint64_t state_transitions() const noexcept { return state_transitions_; }
    uint64_t resync_recovered() const noexcept { return resync_recovered_; }
    uint64_t resync_fallback() const noexcept { return resync_fallback_; }
    float estimated_sco_ppm() const noexcept override { return sco_ppm_; }
    float estimated_cfo_hz() const noexcept override { return current_cfo_hz_; }
    size_t guard_interval_size() const noexcept { return gi_size_; }

    const char* detailed_state_str() const noexcept override;
    void notify_frame_sync(int sp_phase, int ifo_bins = 0) noexcept override;
    bool is_tmcc_anchored() const noexcept override { return sp_tmcc_anchored_; }
    bool is_sp_locked() const noexcept { return sp_phase_locked_; }
    bool is_sp_active() const noexcept { return is_sp_active_; }

    // Metodos de analise de canal expostos para testes unitarios e diagnostico
    int detect_first_path(const std::complex<float>* cir_data, size_t len, float* out_metric);
    int refine_timing_sp(const std::complex<float>* symbol_time, float* out_metric = nullptr, int max_neg_shift = 4);

private:
    SyncParams params_;
    mutable SyncState exposed_state_ = SyncState::Acquisition;
    SpCirSyncState sp_cir_state_ = SpCirSyncState::Acquisition;

    size_t fft_size_ = 2048;
    size_t gi_size_ = 128;
    size_t symbol_len_ = 2176;

    // Buffers pre-alocados para zero alocacao no hot path
    IqStreamBuffer stream_buf_;
    std::vector<std::complex<float>> derot_buf_;
    std::vector<std::complex<float>> rot_vec_;

    // Estruturas de FFT e IFFT para calculo da CIR instantanea via Pilotos Dispersos (SP)
    std::array<std::complex<float>, kSpFftSize> fft_in_{};
    std::array<std::complex<float>, kSpFftSize> fft_out_{};
    std::array<std::complex<float>, kCirSize> cir_in_{};
    std::array<std::complex<float>, kCirSize> cir_out_{};
    std::array<float, kCirSize> pdp_avg_{};
    bool pdp_initialized_ = false;

    fftwf_plan fft_plan_ = nullptr;
    fftwf_plan ifft_cir_plan_ = nullptr;

    // Tabelas de subportadoras e sequencia PRBS de pilotos dispersos (SP)
    std::array<std::vector<int>, 4> sp_indices_{};
    std::array<std::vector<float>, 4> sp_prbs_{};

    std::vector<std::complex<float>> window_gamma_;
    std::vector<float> window_energy_;

    // Rastreamento de fase de pilotos dispersos e ancoragem no TMCC
    bool sp_phase_locked_ = false;
    bool sp_tmcc_anchored_ = false;
    bool is_sp_active_ = false;
    int sp_phase_offset_ = 0;
    int ifo_bins_ = 0;

    // Variaveis de rastreamento de fase e timing
    double nco_phase_ = 0.0;
    float current_cfo_hz_ = 0.0f;
    float peak_correlation_ = 0.0f;
    float sco_ppm_ = 0.0f;
    double frac_timing_offset_ = 0.0;
    double sco_integrator_ = 0.0;
    std::complex<float> block_cp_gamma_{0.0f, 0.0f};
    float block_cp_power_ = 0.0f;

    int consecutive_good_symbols_ = 0;
    int consecutive_bad_symbols_ = 0;
    int symbol_counter_ = 0;
    bool announced_ = false;

    // Metricas
    uint64_t state_transitions_ = 0;
    uint64_t resync_recovered_ = 0;
    uint64_t resync_fallback_ = 0;

    void set_sp_cir_state(SpCirSyncState new_state);
    void init_sp_tables();
    float compute_cp_correlation(const std::complex<float>* ptr, size_t gi, size_t fft) const;
};

// Alias de retrocompatibilidade com a nomenclatura legada
using RobustGRISDBTSync = SpCirSynchronizer;

} // namespace isdbt
