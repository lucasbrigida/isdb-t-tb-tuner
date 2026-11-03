#include "bench_report.h"
#include "utils/ascii_table.h"
#include "third_party/json.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>

using json = nlohmann::json;

namespace isdbt {

static std::string format_double(double val, int precision = 3) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << val;
    return ss.str();
}

static std::string format_uint(uint64_t val) {
    return std::to_string(val);
}

static void evaluate_row(const std::string& name, double val1, double val2, bool lower_is_better,
                         int precision, std::vector<std::string>& out_row) {
    double diff = std::abs(val1 - val2);
    double max_val = std::max(std::abs(val1), std::abs(val2));
    bool is_tie = (max_val < 1e-9) || (diff / max_val <= 0.01);

    std::string s1 = format_double(val1, precision);
    std::string s2 = format_double(val2, precision);
    std::string winner = "empate";

    if (!is_tie) {
        if (lower_is_better) {
            if (val1 < val2) {
                s1 += " *";
                winner = "ofdm=1";
            } else {
                s2 += " *";
                winner = "ofdm=2";
            }
        } else {
            if (val1 > val2) {
                s1 += " *";
                winner = "ofdm=1";
            } else {
                s2 += " *";
                winner = "ofdm=2";
            }
        }
    }

    out_row = {name, s1, s2, winner};
}

static void evaluate_row_uint(const std::string& name, uint64_t val1, uint64_t val2, bool lower_is_better,
                              std::vector<std::string>& out_row) {
    std::string s1 = format_uint(val1);
    std::string s2 = format_uint(val2);
    std::string winner = "empate";

    if (val1 != val2) {
        double diff = std::abs(static_cast<double>(val1) - static_cast<double>(val2));
        double max_val = static_cast<double>(std::max(val1, val2));
        if (max_val > 0 && diff / max_val <= 0.01) {
            winner = "empate";
        } else if (lower_is_better) {
            if (val1 < val2) {
                s1 += " *";
                winner = "ofdm=1";
            } else {
                s2 += " *";
                winner = "ofdm=2";
            }
        } else {
            if (val1 > val2) {
                s1 += " *";
                winner = "ofdm=1";
            } else {
                s2 += " *";
                winner = "ofdm=2";
            }
        }
    }

    out_row = {name, s1, s2, winner};
}

void BenchReport::print_comparison(const BenchRunResult& r1, const BenchRunResult& r2,
                                  const std::string& title_extra) {
    if (!title_extra.empty()) {
        std::cout << "\n=== BENCHMARK: " << title_extra << " ===\n";
    }

    AsciiTable table;
    table.set_headers({"Metrica", "ofdm=1", "ofdm=2", "Melhor"});
    table.set_alignments({Alignment::Left, Alignment::Right, Alignment::Right, Alignment::Left});

    std::vector<std::string> row;

    evaluate_row("wall_time_s", r1.wall_time_s, r2.wall_time_s, true, 3, row);
    table.add_row(row);

    evaluate_row("cpu_user_s", r1.cpu_user_s, r2.cpu_user_s, true, 3, row);
    table.add_row(row);

    evaluate_row_uint("cycles_per_symbol", r1.cycles_per_symbol_mean, r2.cycles_per_symbol_mean, true, row);
    table.add_row(row);

    evaluate_row("peak_rss_mb", r1.peak_rss_mb, r2.peak_rss_mb, true, 1, row);
    table.add_row(row);

    evaluate_row_uint("heap_allocs_hot", r1.heap_allocs_hot, r2.heap_allocs_hot, true, row);
    table.add_row(row);

    table.add_separator();

    evaluate_row("locked_time_ratio", r1.locked_time_ratio, r2.locked_time_ratio, false, 3, row);
    table.add_row(row);

    evaluate_row_uint("sync_loss_count", r1.sync_loss_count, r2.sync_loss_count, true, row);
    table.add_row(row);

    evaluate_row_uint("resync_count", r1.resync_count, r2.resync_count, true, row);
    table.add_row(row);

    evaluate_row("avg_lock_recovery", r1.avg_lock_recovery, r2.avg_lock_recovery, true, 1, row);
    table.add_row(row);

    evaluate_row("snr_mean_db", r1.snr_mean_db, r2.snr_mean_db, false, 1, row);
    table.add_row(row);

    evaluate_row_uint("valid_ts_packets", r1.valid_ts_packets, r2.valid_ts_packets, false, row);
    table.add_row(row);

    table.print(std::cout, 100);

    // Bloco == SANITY == (R22)
    auto sanity = verify_sanity(r1, r2);
    std::cout << sanity.report;
}

SanityCheckResult BenchReport::verify_sanity(const BenchRunResult& r1, const BenchRunResult& r2) {
    SanityCheckResult res;
    std::ostringstream ss;

    ss << "\n== SANITY ==\n";

    // 1. cycles_per_symbol(ofdm=2) <= 2.5 * cycles_per_symbol(ofdm=1)
    double cyc_limit = 2.5 * static_cast<double>(r1.cycles_per_symbol_mean);
    bool check1 = (static_cast<double>(r2.cycles_per_symbol_mean) <= cyc_limit) || (r1.cycles_per_symbol_mean == 0);
    ss << "  [1] cycles_per_symbol(ofdm=2) <= 2.5 * cycles_per_symbol(ofdm=1): "
       << (check1 ? "OK" : "FALHOU")
       << " (" << r2.cycles_per_symbol_mean << " <= " << static_cast<uint64_t>(cyc_limit) << ")\n";

    // 2. locked_time_ratio(ofdm=2) >= locked_time_ratio(ofdm=1)
    // Permite tolerância de 1%
    bool check2 = (r2.locked_time_ratio + 0.01 >= r1.locked_time_ratio);
    ss << "  [2] locked_time_ratio(ofdm=2) >= locked_time_ratio(ofdm=1): "
       << (check2 ? "OK" : "FALHOU")
       << " (" << format_double(r2.locked_time_ratio, 3) << " >= " << format_double(r1.locked_time_ratio, 3) << ")\n";

    // 3. heap_allocs_hot_path(ofdm=2) == 0
    bool check3 = (r2.heap_allocs_hot == 0);
    ss << "  [3] heap_allocs_hot_path(ofdm=2) == 0: "
       << (check3 ? "OK" : "FALHOU")
       << " (" << r2.heap_allocs_hot << " alocacoes)\n\n";

    res.passed = check1 && check2 && check3;
    res.report = ss.str();
    return res;
}

void BenchReport::export_json(const BenchRunResult& r1, const BenchRunResult& r2,
                             const std::string& filepath) {
    json j;
    j["ofdm_1"] = {
        {"wall_time_s", r1.wall_time_s},
        {"cpu_user_s", r1.cpu_user_s},
        {"cpu_sys_s", r1.cpu_sys_s},
        {"cycles_per_symbol_mean", r1.cycles_per_symbol_mean},
        {"cycles_per_symbol_p95", r1.cycles_per_symbol_p95},
        {"peak_rss_mb", r1.peak_rss_mb},
        {"heap_allocs_hot", r1.heap_allocs_hot},
        {"locked_time_ratio", r1.locked_time_ratio},
        {"sync_loss_count", r1.sync_loss_count},
        {"resync_count", r1.resync_count},
        {"avg_lock_recovery", r1.avg_lock_recovery},
        {"snr_mean_db", r1.snr_mean_db},
        {"valid_ts_packets", r1.valid_ts_packets}
    };

    j["ofdm_2"] = {
        {"wall_time_s", r2.wall_time_s},
        {"cpu_user_s", r2.cpu_user_s},
        {"cpu_sys_s", r2.cpu_sys_s},
        {"cycles_per_symbol_mean", r2.cycles_per_symbol_mean},
        {"cycles_per_symbol_p95", r2.cycles_per_symbol_p95},
        {"peak_rss_mb", r2.peak_rss_mb},
        {"heap_allocs_hot", r2.heap_allocs_hot},
        {"locked_time_ratio", r2.locked_time_ratio},
        {"sync_loss_count", r2.sync_loss_count},
        {"resync_count", r2.resync_count},
        {"avg_lock_recovery", r2.avg_lock_recovery},
        {"snr_mean_db", r2.snr_mean_db},
        {"valid_ts_packets", r2.valid_ts_packets}
    };

    std::ofstream f(filepath);
    if (f.is_open()) {
        f << j.dump(2) << "\n";
    }
}

} // namespace isdbt

