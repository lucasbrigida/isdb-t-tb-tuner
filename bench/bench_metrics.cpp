#include "bench_metrics.h"
#include "sync/sync_factory.h"
#include "sync/sp_cir_sync.h"
#include "sync/van_de_beek_sync.h"
#include "dsp/oneseg_demodulator.hpp"
#include "mpegts/psi_parser.hpp"
#include <fstream>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstdlib>
#include <new>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#include <intrin.h>
#pragma comment(lib, "psapi.lib")
#else
#include <sys/resource.h>
#include <sys/time.h>
#include <x86intrin.h>
#endif

namespace {
std::atomic<bool> track_hot_allocations{false};
std::atomic<uint64_t> hot_allocation_count{0};
}

void* operator new(std::size_t size) {
    if (void* ptr = std::malloc(size == 0 ? 1 : size)) {
        if (track_hot_allocations.load(std::memory_order_relaxed)) {
            hot_allocation_count.fetch_add(1, std::memory_order_relaxed);
        }
        return ptr;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept {
    ::operator delete(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    ::operator delete(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    ::operator delete(ptr);
}

namespace isdbt {

BenchTimer::BenchTimer() = default;

void BenchTimer::start() {
    wall_start_ = std::chrono::steady_clock::now();
#if defined(_WIN32)
    FILETIME create_time, exit_time, kernel_time, user_time;
    if (GetProcessTimes(GetCurrentProcess(), &create_time, &exit_time, &kernel_time, &user_time)) {
        ULARGE_INTEGER u, k;
        u.LowPart = user_time.dwLowDateTime;
        u.HighPart = user_time.dwHighDateTime;
        k.LowPart = kernel_time.dwLowDateTime;
        k.HighPart = kernel_time.dwHighDateTime;
        cpu_user_start_us_ = u.QuadPart / 10;
        cpu_sys_start_us_ = k.QuadPart / 10;
    }
#else
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        cpu_user_start_us_ = static_cast<uint64_t>(ru.ru_utime.tv_sec) * 1000000 + ru.ru_utime.tv_usec;
        cpu_sys_start_us_ = static_cast<uint64_t>(ru.ru_stime.tv_sec) * 1000000 + ru.ru_stime.tv_usec;
    }
#endif
}

void BenchTimer::stop() {
    wall_end_ = std::chrono::steady_clock::now();
#if defined(_WIN32)
    FILETIME create_time, exit_time, kernel_time, user_time;
    if (GetProcessTimes(GetCurrentProcess(), &create_time, &exit_time, &kernel_time, &user_time)) {
        ULARGE_INTEGER u, k;
        u.LowPart = user_time.dwLowDateTime;
        u.HighPart = user_time.dwHighDateTime;
        k.LowPart = kernel_time.dwLowDateTime;
        k.HighPart = kernel_time.dwHighDateTime;
        cpu_user_end_us_ = u.QuadPart / 10;
        cpu_sys_end_us_ = k.QuadPart / 10;
    }
#else
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        cpu_user_end_us_ = static_cast<uint64_t>(ru.ru_utime.tv_sec) * 1000000 + ru.ru_utime.tv_usec;
        cpu_sys_end_us_ = static_cast<uint64_t>(ru.ru_stime.tv_sec) * 1000000 + ru.ru_stime.tv_usec;
    }
#endif
}

double BenchTimer::elapsed_wall_s() const {
    return std::chrono::duration<double>(wall_end_ - wall_start_).count();
}

double BenchTimer::elapsed_cpu_user_s() const {
    return static_cast<double>(cpu_user_end_us_ - cpu_user_start_us_) / 1000000.0;
}

double BenchTimer::elapsed_cpu_sys_s() const {
    return static_cast<double>(cpu_sys_end_us_ - cpu_sys_start_us_) / 1000000.0;
}

double BenchTimer::get_peak_rss_mb() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return static_cast<double>(pmc.PeakWorkingSetSize) / (1024.0 * 1024.0);
    }
    return 0.0;
#else
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        return static_cast<double>(ru.ru_maxrss) / 1024.0;
    }
    return 0.0;
#endif
}

BenchRunResult run_benchmark(int ofdm_mode, const std::string& iq_path,
                             double segment_start_s, double segment_duration_s) {
    BenchRunResult res;
    res.ofdm_mode = ofdm_mode;

    std::ifstream file(iq_path, std::ios::binary);
    if (!file.is_open()) return res;

    constexpr size_t chunk_samples = 32768;
    constexpr size_t chunk_bytes = chunk_samples * 2;
    constexpr size_t bytes_per_sec = constants::SAMPLE_RATE * 2;

    if (segment_start_s > 0.0) {
        std::streampos offset = static_cast<std::streampos>(segment_start_s * static_cast<double>(bytes_per_sec));
        file.seekg(offset);
    }

    size_t max_bytes = (segment_duration_s > 0.0)
                       ? static_cast<size_t>(segment_duration_s * static_cast<double>(bytes_per_sec))
                       : static_cast<size_t>(-1);

    SyncParams params;
    params.mode = OfdmMode::MODE_1_2K;
    params.gi = GuardInterval::GI_1_16;
    params.sample_rate = constants::SAMPLE_RATE_HZ;
    params.fft_size = 2048;
    params.gi_size = 128;

    auto synchronizer = make_synchronizer(static_cast<OFDMAlgorithm>(ofdm_mode), params);
    OneSegDemodulator demod(params.mode, params.gi);
    PsiParser psi;

    std::vector<uint8_t> raw_data(chunk_bytes);
    std::vector<std::complex<float>> iq_buf(chunk_samples);
    std::vector<std::complex<float>> useful_symbols;
    useful_symbols.reserve(chunk_samples);

    std::vector<uint64_t> cycles_per_symbol;
    cycles_per_symbol.reserve(120000);

    size_t bytes_read_total = 0;
    SyncState prev_state = SyncState::Acquisition;
    std::vector<double> rssi_vals;
    rssi_vals.reserve(8000);

    BenchTimer timer;
    timer.start();

    while (file.read(reinterpret_cast<char*>(raw_data.data()), chunk_bytes)) {
        bytes_read_total += chunk_bytes;
        float pwr_sum = 0.0f;
        for (size_t i = 0; i < chunk_samples; ++i) {
            float iv = (static_cast<float>(raw_data[2 * i + 0]) - 127.5f) / 127.5f;
            float qv = (static_cast<float>(raw_data[2 * i + 1]) - 127.5f) / 127.5f;
            iq_buf[i] = std::complex<float>(iv, qv);
            pwr_sum += (iv * iv + qv * qv);
        }
        float rssi_dbfs = 10.0f * std::log10((pwr_sum / chunk_samples) + 1e-12f);
        rssi_vals.push_back(rssi_dbfs);

        uint64_t t0 = __rdtsc();
        std::vector<std::vector<uint8_t>> decoded_pkts;
        hot_allocation_count.store(0, std::memory_order_relaxed);
        track_hot_allocations.store(true, std::memory_order_relaxed);
        synchronizer->process(iq_buf.data(), chunk_samples, useful_symbols);
        uint64_t t1 = __rdtsc();

        SyncState cur_state = synchronizer->state();
        if (cur_state != prev_state) {
            if (cur_state == SyncState::LostLock) {
                res.sync_loss_count++;
            } else if (prev_state == SyncState::LostLock && cur_state == SyncState::Tracking) {
                res.resync_count++;
            }
            prev_state = cur_state;
        }

        bool is_locked = (cur_state == SyncState::Tracking);
        size_t n_syms = useful_symbols.size() / 2048;
        for (size_t s = 0; s + 2048 <= useful_symbols.size(); s += 2048) {
            demod.process_symbol(&useful_symbols[s], decoded_pkts);
            res.symbols_processed++;
            if (is_locked) res.symbols_locked++;
        }
        if (!useful_symbols.empty() && demod.is_tmcc_locked()) {
            synchronizer->notify_frame_sync((demod.sp_phase() + 1) & 3, demod.integer_freq_offset_bins());
        }
        track_hot_allocations.store(false, std::memory_order_relaxed);

        if (n_syms > 0) {
            uint64_t cycles_each = (t1 - t0) / n_syms;
            for (size_t s = 0; s < n_syms; ++s) {
                cycles_per_symbol.push_back(cycles_each);
            }
        }

        for (const auto& pkt : decoded_pkts) {
            psi.parse_packet(pkt.data());
            res.valid_ts_packets++;
        }
        res.heap_allocs_hot += hot_allocation_count.load(std::memory_order_relaxed);

        if (bytes_read_total >= max_bytes) break;
    }

    timer.stop();

    res.wall_time_s = timer.elapsed_wall_s();
    res.cpu_user_s = timer.elapsed_cpu_user_s();
    res.cpu_sys_s = timer.elapsed_cpu_sys_s();
    res.peak_rss_mb = BenchTimer::get_peak_rss_mb();

    if (!cycles_per_symbol.empty()) {
        uint64_t sum_cyc = std::accumulate(cycles_per_symbol.begin(), cycles_per_symbol.end(), uint64_t(0));
        res.cycles_per_symbol_mean = sum_cyc / cycles_per_symbol.size();

        std::sort(cycles_per_symbol.begin(), cycles_per_symbol.end());
        size_t p95_idx = static_cast<size_t>(cycles_per_symbol.size() * 0.95);
        res.cycles_per_symbol_p95 = cycles_per_symbol[p95_idx];
    }

    if (res.symbols_processed > 0) {
        res.locked_time_ratio = static_cast<double>(res.symbols_locked) / static_cast<double>(res.symbols_processed);
    }
    if (res.resync_count > 0) {
        res.avg_lock_recovery = static_cast<double>(res.symbols_processed - res.symbols_locked) / static_cast<double>(res.resync_count);
    }

    res.snr_mean_db = demod.average_snr_db();
    if (!rssi_vals.empty()) {
        double r_sum = std::accumulate(rssi_vals.begin(), rssi_vals.end(), 0.0);
        res.rssi_mean_dbfs = r_sum / rssi_vals.size();
    }

    double dur_s = (res.wall_time_s > 0.0) ? res.wall_time_s : 1.0;
    res.ts_bitrate_mbps = (static_cast<double>(res.valid_ts_packets * 188 * 8) / dur_s) / 1e6;

    return res;
}

} // namespace isdbt
