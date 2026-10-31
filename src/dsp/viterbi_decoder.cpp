#include "dsp/viterbi_decoder.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <array>

namespace isdbt {

// Polinomios padrao DVB/ISDB-T:
// G1 = 171 octal = 0b1111001
// G2 = 133 octal = 0b1011011
constexpr uint8_t POLY_G1 = 0171;
constexpr uint8_t POLY_G2 = 0133;

static inline uint8_t parity(uint8_t x) {
    x ^= x >> 4;
    x ^= x >> 2;
    x ^= x >> 1;
    return x & 1;
}

ViterbiDecoder::ViterbiDecoder(CodeRate rate) : rate_(rate) {
    init_trellis();
}

void ViterbiDecoder::set_code_rate(CodeRate rate) {
    rate_ = rate;
}

void ViterbiDecoder::reset() {
    // Reset de estado se necessario
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
        for (size_t i = 0; i + 3 < raw_pairs.size(); i += 4) {
            out_symbols.push_back(raw_pairs[i + 0]); // X1
            out_symbols.push_back(raw_pairs[i + 1]); // Y1
            out_symbols.push_back(raw_pairs[i + 3]); // Y2
        }
    } else if (rate_ == CodeRate::RATE_3_4) {
        for (size_t i = 0; i + 5 < raw_pairs.size(); i += 6) {
            out_symbols.push_back(raw_pairs[i + 0]); // X1
            out_symbols.push_back(raw_pairs[i + 1]); // Y1
            out_symbols.push_back(raw_pairs[i + 3]); // Y2
            out_symbols.push_back(raw_pairs[i + 4]); // X3
        }
    } else {
        out_symbols = std::move(raw_pairs);
    }
}

void ViterbiDecoder::depuncture(const std::vector<uint8_t>& soft_in, std::vector<uint8_t>& unpunctured) {
    unpunctured.clear();
    constexpr uint8_t ERASED = 128;

    if (rate_ == CodeRate::RATE_1_2) {
        unpunctured = soft_in;
    } else if (rate_ == CodeRate::RATE_2_3) {
        for (size_t i = 0; i + 2 < soft_in.size(); i += 3) {
            unpunctured.push_back(soft_in[i + 0]);
            unpunctured.push_back(soft_in[i + 1]);
            unpunctured.push_back(ERASED);
            unpunctured.push_back(soft_in[i + 2]);
        }
    } else if (rate_ == CodeRate::RATE_3_4) {
        for (size_t i = 0; i + 3 < soft_in.size(); i += 4) {
            unpunctured.push_back(soft_in[i + 0]);
            unpunctured.push_back(soft_in[i + 1]);
            unpunctured.push_back(ERASED);
            unpunctured.push_back(soft_in[i + 2]);
            unpunctured.push_back(soft_in[i + 3]);
            unpunctured.push_back(ERASED);
        }
    } else {
        unpunctured = soft_in;
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

                uint32_t branch_metric = std::abs(static_cast<int>(rx_g1) - static_cast<int>(exp_g1)) +
                                         std::abs(static_cast<int>(rx_g2) - static_cast<int>(exp_g2));

                uint32_t total = cur_m + branch_metric;
                if (total < next_metric[ns]) {
                    next_metric[ns] = total;
                    if (state & 1) {
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

} // namespace isdbt
