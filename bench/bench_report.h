#pragma once

#include "bench_metrics.h"
#include <string>
#include <vector>

namespace isdbt {

struct SanityCheckResult {
    bool passed = true;
    std::string report;
};

class BenchReport {
public:
    static void print_comparison(const BenchRunResult& r1, const BenchRunResult& r2,
                                 const std::string& title_extra = "");

    static SanityCheckResult verify_sanity(const BenchRunResult& r1, const BenchRunResult& r2);

    static void export_json(const BenchRunResult& r1, const BenchRunResult& r2,
                            const std::string& filepath);
};

} // namespace isdbt

