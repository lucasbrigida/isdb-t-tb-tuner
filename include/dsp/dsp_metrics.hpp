#pragma once

#include <cstdint>

namespace isdbt {

/**
 * @brief Metricas operacionais e estatisticas de runtime do pipeline DSP.
 */
struct DspMetrics {
    uint64_t input_blocks{0};
    uint64_t dropped_blocks{0};
    uint64_t sequence_gaps{0};
    uint64_t decoder_resets{0};
    uint64_t processed_symbols{0};
    uint64_t valid_ts_packets{0};
    uint64_t corrected_rs_packets{0};
    uint64_t uncorrectable_rs_packets{0};
    uint64_t sync_acquisitions{0};
    uint64_t sync_losses{0};

    double average_symbol_us{0.0};
    double average_evm{0.0};
    double average_snr_db{0.0};
};

} // namespace isdbt
