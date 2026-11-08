#pragma once
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define ISDBT_ARCH_X86 1
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

namespace isdbt_simd {

inline void get_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t regs[4]) {
#if defined(ISDBT_ARCH_X86)
#if defined(__GNUC__) || defined(__clang__)
    uint32_t eax, ebx, ecx, edx;
#if defined(__i386__) && defined(__PIC__)
    // Em x86 32-bit PIC, o registrador EBX e reservado para a GOT
    __asm__ __volatile__(
        "xchg %%ebx, %%edi\n\t"
        "cpuid\n\t"
        "xchg %%ebx, %%edi\n\t"
        : "=a"(eax), "=D"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(leaf), "c"(subleaf)
    );
#else
    __asm__ __volatile__(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(leaf), "c"(subleaf)
    );
#endif
    regs[0] = eax;
    regs[1] = ebx;
    regs[2] = ecx;
    regs[3] = edx;
#elif defined(_MSC_VER)
    __cpuidex(reinterpret_cast<int*>(regs), static_cast<int>(leaf), static_cast<int>(subleaf));
#else
    regs[0] = regs[1] = regs[2] = regs[3] = 0;
#endif
#else
    regs[0] = regs[1] = regs[2] = regs[3] = 0;
#endif
}

inline uint64_t get_xgetbv(uint32_t index) {
#if defined(ISDBT_ARCH_X86)
#if defined(_MSC_VER)
    return _xgetbv(index);
#elif defined(__GNUC__) || defined(__clang__)
    uint32_t eax, edx;
    __asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(index));
    return (static_cast<uint64_t>(edx) << 32) | eax;
#else
    return 0;
#endif
#else
    return 0;
#endif
}

struct CpuFeatures {
    bool has_sse2{false};
    bool has_sse3{false};
    bool has_avx{false};
    bool has_avx2{false};
    bool has_fma{false};
    bool os_supports_ymm{false};
    bool initialized{false};

    void detect() {
        if (initialized) return;
#if defined(ISDBT_ARCH_X86)
        uint32_t regs[4] = {0};
        get_cpuid(0, 0, regs);
        uint32_t max_leaf = regs[0];

        if (max_leaf >= 1) {
            get_cpuid(1, 0, regs);
            uint32_t ecx = regs[2];
            uint32_t edx = regs[3];

            has_sse2 = (edx & (1u << 26)) != 0;
            has_sse3 = (ecx & (1u << 0)) != 0;
            bool cpu_fma = (ecx & (1u << 12)) != 0;
            bool osxsave = (ecx & (1u << 27)) != 0;
            bool cpu_avx = (ecx & (1u << 28)) != 0;

            if (osxsave) {
                uint64_t xcr0 = get_xgetbv(0);
                // Bits 1 (XMM) e 2 (YMM) habilitados pelo SO
                os_supports_ymm = ((xcr0 & 0x06) == 0x06);
            }

            has_avx = cpu_avx && os_supports_ymm;
            has_fma = cpu_fma && os_supports_ymm;
        }

        if (max_leaf >= 7 && os_supports_ymm) {
            get_cpuid(7, 0, regs);
            uint32_t ebx = regs[1];
            bool cpu_avx2 = (ebx & (1u << 5)) != 0;
            has_avx2 = cpu_avx2 && has_avx;
        }
#else
        has_sse2 = false;
        has_sse3 = false;
        has_avx = false;
        has_avx2 = false;
        has_fma = false;
        os_supports_ymm = false;
#endif
        initialized = true;
    }
};

inline CpuFeatures& get_cpu_features() {
    static CpuFeatures features;
    if (!features.initialized) {
        features.detect();
    }
    return features;
}

} // namespace isdbt_simd

struct VolkCpu {
    int (*has_avx2)() = []() -> int {
        return isdbt_simd::get_cpu_features().has_avx2 ? 1 : 0;
    };
    int (*has_fma)() = []() -> int {
        return isdbt_simd::get_cpu_features().has_fma ? 1 : 0;
    };
    int (*has_sse2)() = []() -> int {
        return isdbt_simd::get_cpu_features().has_sse2 ? 1 : 0;
    };
    int (*has_sse3)() = []() -> int {
        return isdbt_simd::get_cpu_features().has_sse3 ? 1 : 0;
    };
    const char* (*active_kernel_name)() = []() -> const char* {
        auto& f = isdbt_simd::get_cpu_features();
        if (f.has_avx2 && f.has_fma) return "AVX2+FMA (256-bit)";
        if (f.has_avx2) return "AVX2 (256-bit)";
        if (f.has_sse3) return "SSE3 (128-bit)";
        if (f.has_sse2) return "SSE2 (128-bit)";
        return "Escalar (Fallback C++)";
    };
};

inline VolkCpu volk_cpu;
inline void volk_cpu_init() {
    isdbt_simd::get_cpu_features().detect();
}
