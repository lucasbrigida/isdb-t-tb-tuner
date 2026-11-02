#include "dsp/viterbi_decoder.hpp"
#include "dsp/constants.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <array>

namespace isdbt {

// Polinomios padrao NASA K=7 usados em ISDB-T / gr-isdbt (viterbi_decoder_impl.h):
// Como o registrador desloca para a esquerda (reg7 = (state << 1) | b) com o bit mais recente no LSB (bit 0):
// POLYA (G1 = 171o = 1 + D + D^2 + D^3 + D^6) -> 0b1001111 = 0x4F
// POLYB (G2 = 133o = 1 + D^2 + D^3 + D^5 + D^6) -> 0b1101101 = 0x6D
constexpr uint8_t POLY_G1 = 0x4F;
constexpr uint8_t POLY_G2 = 0x6D;

static inline uint8_t parity(uint8_t x) {
    x ^= x >> 4;
    x ^= x >> 2;
    x ^= x >> 1;
    return x & 1;
}

ViterbiDecoder::ViterbiDecoder(CodeRate rate) : rate_(rate) {
    init_trellis();
    reset();
}

void ViterbiDecoder::set_code_rate(CodeRate rate) {
    if (rate_ != rate) {
        rate_ = rate;
        reset();
    }
}

void ViterbiDecoder::reset() {
    cont_metric_.fill(1000000);
    cont_metric_[0] = 0;
    cont_history_ring_.fill(0);
    history_write_ = 0;
    history_count_ = 0;
    cont_soft_rem_.clear();
    cont_bit_buf_.clear();
}

void ViterbiDecoder::init_trellis() {
    for (int state = 0; state < NUM_STATES; ++state) {
        for (int b = 0; b < 2; ++b) {
            uint8_t reg7 = (state << 1) | b;
            uint8_t nstate = reg7 & 0x3F;
            next_state_[state][b] = nstate;

            uint8_t out_g1 = parity(reg7 & POLY_G1);
            uint8_t out_g2 = parity(reg7 & POLY_G2);
            output_parity_[state][b] = (out_g1 << 1) | out_g2;
        }
    }

    int count[NUM_STATES] = {0};
    for (int state = 0; state < NUM_STATES; ++state) {
        for (int b = 0; b < 2; ++b) {
            uint8_t ns = next_state_[state][b];
            prev_state_[ns][count[ns]++] = state;
        }
    }
}

void ViterbiDecoder::encode(const std::vector<uint8_t>& in_bits, std::vector<uint8_t>& out_symbols) {
    out_symbols.clear();
    uint8_t state = 0;

    std::vector<uint8_t> raw_pairs;
    raw_pairs.reserve(in_bits.size() * 2);

    for (uint8_t bit : in_bits) {
        uint8_t reg7 = (state << 1) | (bit & 1);
        uint8_t g1 = parity(reg7 & POLY_G1);
        uint8_t g2 = parity(reg7 & POLY_G2);
        raw_pairs.push_back(g1);
        raw_pairs.push_back(g2);
        state = reg7 & 0x3F;
    }

    if (rate_ == CodeRate::RATE_1_2) {
        out_symbols = std::move(raw_pairs);
    } else if (rate_ == CodeRate::RATE_2_3) {
        // Puncture [1, 1, 0, 1]
        for (size_t i = 0; i + 3 < raw_pairs.size(); i += 4) {
            out_symbols.push_back(raw_pairs[i + 0]);
            out_symbols.push_back(raw_pairs[i + 1]);
            out_symbols.push_back(raw_pairs[i + 3]);
        }
    } else if (rate_ == CodeRate::RATE_3_4) {
        // Puncture [1, 1, 0, 1, 1, 0]
        for (size_t i = 0; i + 5 < raw_pairs.size(); i += 6) {
            out_symbols.push_back(raw_pairs[i + 0]);
            out_symbols.push_back(raw_pairs[i + 1]);
            out_symbols.push_back(raw_pairs[i + 3]);
            out_symbols.push_back(raw_pairs[i + 4]);
        }
    } else if (rate_ == CodeRate::RATE_5_6) {
        // Puncture [1, 1, 0, 1, 1, 0, 0, 1, 1, 0]
        for (size_t i = 0; i + 9 < raw_pairs.size(); i += 10) {
            out_symbols.push_back(raw_pairs[i + 0]);
            out_symbols.push_back(raw_pairs[i + 1]);
            out_symbols.push_back(raw_pairs[i + 3]);
            out_symbols.push_back(raw_pairs[i + 4]);
            out_symbols.push_back(raw_pairs[i + 7]);
            out_symbols.push_back(raw_pairs[i + 8]);
        }
    } else if (rate_ == CodeRate::RATE_7_8) {
        // Puncture [1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0]
        for (size_t i = 0; i + 13 < raw_pairs.size(); i += 14) {
            out_symbols.push_back(raw_pairs[i + 0]);
            out_symbols.push_back(raw_pairs[i + 1]);
            out_symbols.push_back(raw_pairs[i + 3]);
            out_symbols.push_back(raw_pairs[i + 5]);
            out_symbols.push_back(raw_pairs[i + 7]);
            out_symbols.push_back(raw_pairs[i + 8]);
            out_symbols.push_back(raw_pairs[i + 11]);
            out_symbols.push_back(raw_pairs[i + 12]);
        }
    } else {
        out_symbols = std::move(raw_pairs);
    }
}

void ViterbiDecoder::depuncture(const std::vector<uint8_t>& soft_in, std::vector<uint8_t>& unpunctured) {
    depuncture(soft_in.data(), soft_in.size(), unpunctured);
}

void ViterbiDecoder::depuncture(const uint8_t* in, size_t in_size, std::vector<uint8_t>& unpunctured) {
    unpunctured.clear();
    const uint8_t ERASED = static_cast<uint8_t>(constants::SOFT_NEUTRAL);

    if (rate_ == CodeRate::RATE_1_2) {
        unpunctured.assign(in, in + in_size);
    } else if (rate_ == CodeRate::RATE_2_3) {
        unpunctured.reserve((in_size / 3) * 4);
        for (size_t i = 0; i + 2 < in_size; i += 3) {
            unpunctured.push_back(in[i + 0]);
            unpunctured.push_back(in[i + 1]);
            unpunctured.push_back(ERASED);
            unpunctured.push_back(in[i + 2]);
        }
    } else if (rate_ == CodeRate::RATE_3_4) {
        unpunctured.reserve((in_size / 4) * 6);
        for (size_t i = 0; i + 3 < in_size; i += 4) {
            unpunctured.push_back(in[i + 0]);
            unpunctured.push_back(in[i + 1]);
            unpunctured.push_back(ERASED);
            unpunctured.push_back(in[i + 2]);
            unpunctured.push_back(in[i + 3]);
            unpunctured.push_back(ERASED);
        }
    } else if (rate_ == CodeRate::RATE_5_6) {
        unpunctured.reserve((in_size / 6) * 10);
        for (size_t i = 0; i + 5 < in_size; i += 6) {
            unpunctured.push_back(in[i + 0]);
            unpunctured.push_back(in[i + 1]);
            unpunctured.push_back(ERASED);
            unpunctured.push_back(in[i + 2]);
            unpunctured.push_back(in[i + 3]);
            unpunctured.push_back(ERASED);
            unpunctured.push_back(ERASED);
            unpunctured.push_back(in[i + 4]);
            unpunctured.push_back(in[i + 5]);
            unpunctured.push_back(ERASED);
        }
    } else if (rate_ == CodeRate::RATE_7_8) {
        unpunctured.reserve((in_size / 8) * 16);
        for (size_t i = 0; i + 7 < in_size; i += 8) {
            unpunctured.push_back(in[i + 0]);
            unpunctured.push_back(in[i + 1]);
            unpunctured.push_back(ERASED);
            unpunctured.push_back(in[i + 2]);
            unpunctured.push_back(ERASED);
            unpunctured.push_back(in[i + 3]);
            unpunctured.push_back(ERASED);
            unpunctured.push_back(in[i + 4]);
            unpunctured.push_back(in[i + 5]);
            unpunctured.push_back(ERASED);
            unpunctured.push_back(ERASED);
            unpunctured.push_back(in[i + 6]);
            unpunctured.push_back(in[i + 7]);
            unpunctured.push_back(ERASED);
        }
    } else {
        unpunctured.assign(in, in + in_size);
    }
}

void ViterbiDecoder::decode(const std::vector<uint8_t>& soft_in, std::vector<uint8_t>& out_bytes) {
    out_bytes.clear();
    std::vector<uint8_t> unpunct;
    depuncture(soft_in, unpunct);

    size_t num_steps = unpunct.size() / 2;
    if (num_steps == 0) return;

    constexpr uint32_t INF_METRIC = 1000000000;
    std::array<uint32_t, NUM_STATES> path_metric;
    std::array<uint32_t, NUM_STATES> next_metric;
    path_metric.fill(INF_METRIC);
    path_metric[0] = 0;

    std::vector<uint64_t> survivor_history(num_steps, 0);

    for (size_t t = 0; t < num_steps; ++t) {
        uint8_t rx_g1 = unpunct[2 * t + 0];
        uint8_t rx_g2 = unpunct[2 * t + 1];

        next_metric.fill(INF_METRIC);
        uint64_t survivor_bits = 0;

        for (int state = 0; state < NUM_STATES; ++state) {
            uint32_t cur_m = path_metric[state];
            if (cur_m >= INF_METRIC) continue;

            for (int b = 0; b < 2; ++b) {
                uint8_t ns = next_state_[state][b];
                uint8_t parity = output_parity_[state][b];
                uint8_t exp_g1 = (parity >> 1) & 1 ? 255 : 0;
                uint8_t exp_g2 = (parity & 1) ? 255 : 0;

                constexpr uint8_t NEUTRAL_SOFT = static_cast<uint8_t>(constants::SOFT_NEUTRAL);
                uint32_t bm1 = (rx_g1 == NEUTRAL_SOFT) ? 0 : static_cast<uint32_t>(std::abs(static_cast<int>(rx_g1) - static_cast<int>(exp_g1)));
                uint32_t bm2 = (rx_g2 == NEUTRAL_SOFT) ? 0 : static_cast<uint32_t>(std::abs(static_cast<int>(rx_g2) - static_cast<int>(exp_g2)));
                uint32_t branch_metric = bm1 + bm2;

                uint32_t total = cur_m + branch_metric;
                if (total < next_metric[ns]) {
                    next_metric[ns] = total;
                    // Salva o MSB (bit 5 = 0x20) do estado anterior que foi deslocado para fora em (state << 1)
                    if (state & 0x20) {
                        survivor_bits |= (1ULL << ns);
                    } else {
                        survivor_bits &= ~(1ULL << ns);
                    }
                }
            }
        }

        path_metric = next_metric;
        survivor_history[t] = survivor_bits;

        uint32_t min_m = *std::min_element(path_metric.begin(), path_metric.end());
        if (min_m > 1000000) {
            for (auto& m : path_metric) {
                if (m < INF_METRIC) m -= min_m;
            }
        }
    }

    int best_state = 0;
    uint32_t best_m = path_metric[0];
    for (int s = 1; s < NUM_STATES; ++s) {
        if (path_metric[s] < best_m) {
            best_m = path_metric[s];
            best_state = s;
        }
    }

    std::vector<uint8_t> decoded_bits(num_steps);
    int cur_state = best_state;
    for (int t = static_cast<int>(num_steps) - 1; t >= 0; --t) {
        decoded_bits[t] = cur_state & 1;
        bool msb_was_1 = (survivor_history[t] >> cur_state) & 1;
        cur_state = (cur_state >> 1) | (msb_was_1 ? 0x20 : 0);
    }

    out_bytes.reserve(decoded_bits.size() / 8);
    for (size_t i = 0; i + 7 < decoded_bits.size(); i += 8) {
        uint8_t byte = 0;
        for (int b = 0; b < 8; ++b) {
            byte = (byte << 1) | (decoded_bits[i + b] & 1);
        }
        out_bytes.push_back(byte);
    }
}

void ViterbiDecoder::decode_continuous(const std::vector<uint8_t>& soft_in, std::vector<uint8_t>& out_bytes) {
    out_bytes.clear();

    // Acumula entradas soft para garantir alinhamento exato com o bloco de puncionamento
    cont_soft_rem_.insert(cont_soft_rem_.end(), soft_in.begin(), soft_in.end());

    size_t punct_block = 2;
    if (rate_ == CodeRate::RATE_2_3) punct_block = 3;
    else if (rate_ == CodeRate::RATE_3_4) punct_block = 4;
    else if (rate_ == CodeRate::RATE_5_6) punct_block = 6;
    else if (rate_ == CodeRate::RATE_7_8) punct_block = 8;

    size_t usable_len = (cont_soft_rem_.size() / punct_block) * punct_block;
    if (usable_len == 0) return;

    depuncture(cont_soft_rem_.data(), usable_len, unpunctured_buf_);

    size_t rem_count = cont_soft_rem_.size() - usable_len;
    if (rem_count > 0) {
        std::memmove(cont_soft_rem_.data(), cont_soft_rem_.data() + usable_len, rem_count);
        cont_soft_rem_.resize(rem_count);
    } else {
        cont_soft_rem_.clear();
    }

    size_t num_steps = unpunctured_buf_.size() / 2;
    constexpr uint32_t INF_METRIC = 1000000000;
    std::array<uint32_t, NUM_STATES> next_metric;

    for (size_t t = 0; t < num_steps; ++t) {
        uint8_t rx_g1 = unpunctured_buf_[2 * t + 0];
        uint8_t rx_g2 = unpunctured_buf_[2 * t + 1];

        next_metric.fill(INF_METRIC);
        uint64_t survivor_bits = 0;

        for (int state = 0; state < NUM_STATES; ++state) {
            uint32_t cur_m = cont_metric_[state];
            if (cur_m >= INF_METRIC) continue;

            for (int b = 0; b < 2; ++b) {
                uint8_t ns = next_state_[state][b];
                uint8_t parity = output_parity_[state][b];
                uint8_t exp_g1 = (parity >> 1) & 1 ? 255 : 0;
                uint8_t exp_g2 = (parity & 1) ? 255 : 0;

                constexpr uint8_t NEUTRAL_SOFT = static_cast<uint8_t>(constants::SOFT_NEUTRAL);
                uint32_t bm1 = (rx_g1 == NEUTRAL_SOFT) ? 0 : static_cast<uint32_t>(std::abs(static_cast<int>(rx_g1) - static_cast<int>(exp_g1)));
                uint32_t bm2 = (rx_g2 == NEUTRAL_SOFT) ? 0 : static_cast<uint32_t>(std::abs(static_cast<int>(rx_g2) - static_cast<int>(exp_g2)));
                uint32_t total = cur_m + bm1 + bm2;

                if (total < next_metric[ns]) {
                    next_metric[ns] = total;
                    if (state & 0x20) {
                        survivor_bits |= (1ULL << ns);
                    } else {
                        survivor_bits &= ~(1ULL << ns);
                    }
                }
            }
        }

        cont_metric_ = next_metric;
        cont_history_ring_[history_write_] = survivor_bits;
        history_write_ = (history_write_ + 1) % TRACEBACK_DEPTH;
        if (history_count_ < TRACEBACK_DEPTH) {
            history_count_++;
        }

        uint32_t min_m = *std::min_element(cont_metric_.begin(), cont_metric_.end());
        if (min_m > 100000) {
            for (auto& m : cont_metric_) {
                if (m < INF_METRIC) m -= min_m;
            }
        }

        // Quando acumular TRACEBACK_DEPTH passos, realiza traceback do bit mais antigo atraves do ring buffer
        if (history_count_ >= TRACEBACK_DEPTH) {
            int cur_state = 0;
            uint32_t best_m = cont_metric_[0];
            for (int s = 1; s < NUM_STATES; ++s) {
                if (cont_metric_[s] < best_m) {
                    best_m = cont_metric_[s];
                    cur_state = s;
                }
            }

            for (size_t step = 0; step < TRACEBACK_DEPTH - 1; ++step) {
                size_t idx = (history_write_ - 1 - step + TRACEBACK_DEPTH) % TRACEBACK_DEPTH;
                bool msb_was_1 = (cont_history_ring_[idx] >> cur_state) & 1;
                cur_state = (cur_state >> 1) | (msb_was_1 ? 0x20 : 0);
            }
            uint8_t oldest_bit = cur_state & 1;

            cont_bit_buf_.push_back(oldest_bit);
            if (cont_bit_buf_.size() == 8) {
                uint8_t byte = 0;
                for (int b = 0; b < 8; ++b) {
                    byte = (byte << 1) | (cont_bit_buf_[b] & 1);
                }
                out_bytes.push_back(byte);
                cont_bit_buf_.clear();
            }
        }
    }
}

} // namespace isdbt
