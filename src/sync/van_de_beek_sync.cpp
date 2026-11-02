#include "sync/van_de_beek_sync.h"
#include <algorithm>
#include <cmath>

namespace isdbt {

VanDeBeekSync::VanDeBeekSync(const SyncParams& params)
    : params_(params),
      sync_(params.mode, params.gi, params.sample_rate),
      stream_iq_buf_(65536 * 2),
      gi_len_(params.gi_size),
      sym_len_(params.fft_size + params.gi_size) {
    temp_buf_.reserve(65536);
}

void VanDeBeekSync::reset() noexcept {
    sync_.reset();
    ofdm_locked_ = false;
    tracked_ffo_hz_ = 0.0f;
    unlock_counter_ = 0;
    frac_pos_ = 0.0f;
    gi_len_ = static_cast<size_t>(sync_.gi_size());
    sym_len_ = params_.fft_size + gi_len_;
    stream_iq_buf_.clear();
    cur_state_ = SyncState::Acquisition;
}

const SyncState& VanDeBeekSync::state() const noexcept {
    cur_state_ = sync_.sync_state();
    return cur_state_;
}

void VanDeBeekSync::process(const std::complex<float>* in, size_t n,
                           std::vector<std::complex<float>>& out) {
    out.clear();
    if (in == nullptr || n == 0) return;

    if (temp_buf_.size() < n) {
        temp_buf_.resize(n);
    }
    std::copy(in, in + n, temp_buf_.begin());

    auto sync_res = sync_.find_symbol_boundary(in, n);
    cur_state_ = sync_.sync_state();

    if (!ofdm_locked_) {
        if (sync_res.locked) {
            ofdm_locked_ = true;
            unlock_counter_ = 0;
            gi_len_ = static_cast<size_t>(sync_.gi_size());
            sym_len_ = params_.fft_size + gi_len_;
            frac_pos_ = 0.0f;
            tracked_ffo_hz_ = sync_res.ffo_hz;
            sync_.reset_phase();
            sync_.correct_frequency_offset(temp_buf_.data(), n, tracked_ffo_hz_);

            size_t gi_start = (sync_res.symbol_offset >= static_cast<int>(gi_len_))
                              ? static_cast<size_t>(sync_res.symbol_offset - static_cast<int>(gi_len_))
                              : 0;
            stream_iq_buf_.assign(temp_buf_.data() + gi_start, n - gi_start);
        }
    } else {
        if (!sync_res.locked) {
            unlock_counter_++;
            if (unlock_counter_ > 200) {
                reset();
            }
        } else {
            unlock_counter_ = 0;
            const float spacing = params_.sample_rate / static_cast<float>(params_.fft_size);
            float residual = sync_res.ffo_hz - tracked_ffo_hz_;
            residual -= spacing * std::round(residual / spacing);
            tracked_ffo_hz_ += 0.1f * residual;
        }

        if (ofdm_locked_) {
            sync_.correct_frequency_offset(temp_buf_.data(), n, tracked_ffo_hz_);
            stream_iq_buf_.append(temp_buf_.data(), n);
        }
    }

    if (ofdm_locked_) {
        while (static_cast<size_t>(std::lround(frac_pos_)) + sym_len_ + 8 <= stream_iq_buf_.available()) {
            int base_pos = static_cast<int>(std::lround(frac_pos_));
            int max_neg = (base_pos >= 4) ? 4 : base_pos;
            int best_sh = 0;
            float best_mag = -1.0f;
            for (int sh = -max_neg; sh <= 4; ++sh) {
                std::complex<float> g(0.0f, 0.0f);
                for (size_t m = 0; m < gi_len_; ++m) {
                    g += stream_iq_buf_[base_pos + sh + m] * std::conj(stream_iq_buf_[base_pos + sh + m + params_.fft_size]);
                }
                float mag = std::norm(g);
                if (mag > best_mag) {
                    best_mag = mag;
                    best_sh = sh;
                }
            }
            frac_pos_ += 0.25f * static_cast<float>(best_sh);
            int sym_d = std::max(0, static_cast<int>(std::lround(frac_pos_)));
            if (static_cast<size_t>(sym_d) + sym_len_ > stream_iq_buf_.available()) break;

            const std::complex<float>* useful_ptr = stream_iq_buf_.data_at(sym_d + gi_len_);
            out.insert(out.end(), useful_ptr, useful_ptr + params_.fft_size);

            frac_pos_ += static_cast<float>(sym_len_);
        }

        int consume_i = static_cast<int>(std::floor(frac_pos_)) - 8;
        if (consume_i > 0 && static_cast<size_t>(consume_i) <= stream_iq_buf_.available()) {
            stream_iq_buf_.consume(static_cast<size_t>(consume_i));
            frac_pos_ -= static_cast<float>(consume_i);
        }
    }
}

} // namespace isdbt

