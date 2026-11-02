#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

namespace isdbt {

struct BenchRunResult {
    int ofdm_mode = 1;
    double wall_time_s = 0.0;
    double cpu_user_s = 0.0;
    double cpu_sys_s = 0.0;
    uint64_t cycles_per_symbol_mean = 0;
    uint64_t cycles_per_symbol_p95 = 0;
    double peak_rss_mb = 0.0;
    uint64_t heap_allocs_hot = 0;

    uint64_t symbols_processed = 0;
    uint64_t symbols_locked = 0;
    double locked_time_ratio = 0.0;

    uint64_t sync_loss_count = 0;
    uint64_t resync_count = 0;
    double avg_lock_recovery = 0.0;

    double ber_pre_viterbi = 0.0;
    double ber_post_reed_solomon = 0.0;
    double rssi_mean_dbfs = 0.0;
    double snr_mean_db = 0.0;
    double ts_bitrate_mbps = 0.0;
    uint64_t ts_continuity_errors = 0;
    uint64_t valid_ts_packets = 0;
};

class BenchTimer {
public:
    BenchTimer();
    void start();
    void stop();

    double elapsed_wall_s() const;
    double elapsed_cpu_user_s() const;
    double elapsed_cpu_sys_s() const;
    static double get_peak_rss_mb();

private:
    std::chrono::steady_clock::time_point wall_start_;
    std::chrono::steady_clock::time_point wall_end_;

    uint64_t cpu_user_start_us_ = 0;
    uint64_t cpu_user_end_us_ = 0;
    uint64_t cpu_sys_start_us_ = 0;
    uint64_t cpu_sys_end_us_ = 0;
};

BenchRunResult run_benchmark(int ofdm_mode, const std::string& iq_path,
                             double segment_start_s = 0.0, double segment_duration_s = 0.0);

} // namespace isdbt

