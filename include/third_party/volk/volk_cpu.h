#pragma once

struct VolkCpu {
    int (*has_avx2)() = []() -> int { return 1; };
    int (*has_fma)() = []() -> int { return 1; };
};

inline VolkCpu volk_cpu;
inline void volk_cpu_init() {}

