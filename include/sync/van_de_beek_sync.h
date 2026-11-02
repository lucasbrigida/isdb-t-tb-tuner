#pragma once

#include "sync/iofdm_synchronizer.h"
#include "sync/sync_params.h"
#include "dsp/ofdm_sync.hpp"
#include "dsp/iq_stream_buffer.hpp"

namespace isdbt {

/**
 * @brief Adaptador estrito sobre o estimador ML de Van de Beek (R5, R6).
 * Mantem FSM tripartite { Acquisition, Tracking, LostLock } bit-a-bit inalterada.
 */
class VanDeBeekSync : public IOFDMSynchronizer {
public:
    explicit VanDeBeekSync(const SyncParams& params = SyncParams{});
    ~VanDeBeekSync() override = default;

    void process(const std::complex<float>* in, size_t n,
                 std::vector<std::complex<float>>& out) override;

    const SyncState& state() const noexcept override;
    void reset() noexcept override;

    // Acesso direto ao sincronizador interno para interoperabilidade
    OfdmSync& inner_sync() noexcept { return sync_; }
    const OfdmSync& inner_sync() const noexcept { return sync_; }

private:
    SyncParams params_;
    OfdmSync sync_;
    IqStreamBuffer stream_iq_buf_;
    std::vector<std::complex<float>> temp_buf_;

    bool ofdm_locked_ = false;
    float tracked_ffo_hz_ = 0.0f;
    int unlock_counter_ = 0;
    size_t gi_len_ = 128;
    size_t sym_len_ = 2048 + 128;
    float frac_pos_ = 0.0f;
    mutable SyncState cur_state_ = SyncState::Acquisition;
};

} // namespace isdbt

