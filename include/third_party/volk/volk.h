#pragma once
#include <complex>
#include <cstdint>
#include "volk_cpu.h"

#if defined(ISDBT_ARCH_X86)
#include <immintrin.h>
#endif

typedef std::complex<float> lv_32fc_t;

#define VOLK_VERSION_MAJOR 3
#define VOLK_VERSION_MINOR 1
#define VOLK_VERSION_MAINT 2

namespace isdbt_simd {

inline void multiply_32fc_scalar(lv_32fc_t* cVector, const lv_32fc_t* aVector, const lv_32fc_t* bVector, unsigned int num_points) {
    for (unsigned int i = 0; i < num_points; ++i) {
        cVector[i] = aVector[i] * bVector[i];
    }
}

#if defined(ISDBT_ARCH_X86)
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2,fma")))
#endif
inline void multiply_32fc_avx2(lv_32fc_t* cVector, const lv_32fc_t* aVector, const lv_32fc_t* bVector, unsigned int num_points) {
    unsigned int i = 0;
    for (; i + 4 <= num_points; i += 4) {
        __m256 va = _mm256_loadu_ps(reinterpret_cast<const float*>(aVector + i));
        __m256 vb = _mm256_loadu_ps(reinterpret_cast<const float*>(bVector + i));
        __m256 a_re = _mm256_moveldup_ps(va);
        __m256 a_im = _mm256_movehdup_ps(va);
        __m256 b_swap = _mm256_permute_ps(vb, 0xB1);
        __m256 prod1 = _mm256_mul_ps(a_re, vb);
        __m256 prod2 = _mm256_mul_ps(a_im, b_swap);
        __m256 res = _mm256_addsub_ps(prod1, prod2);
        _mm256_storeu_ps(reinterpret_cast<float*>(cVector + i), res);
    }
    for (; i < num_points; ++i) {
        cVector[i] = aVector[i] * bVector[i];
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("sse3")))
#endif
inline void multiply_32fc_sse3(lv_32fc_t* cVector, const lv_32fc_t* aVector, const lv_32fc_t* bVector, unsigned int num_points) {
    unsigned int i = 0;
    for (; i + 2 <= num_points; i += 2) {
        __m128 va = _mm_loadu_ps(reinterpret_cast<const float*>(aVector + i));
        __m128 vb = _mm_loadu_ps(reinterpret_cast<const float*>(bVector + i));
        __m128 a_re = _mm_moveldup_ps(va);
        __m128 a_im = _mm_movehdup_ps(va);
        __m128 b_swap = _mm_shuffle_ps(vb, vb, _MM_SHUFFLE(2, 3, 0, 1));
        __m128 prod1 = _mm_mul_ps(a_re, vb);
        __m128 prod2 = _mm_mul_ps(a_im, b_swap);
        __m128 res = _mm_addsub_ps(prod1, prod2);
        _mm_storeu_ps(reinterpret_cast<float*>(cVector + i), res);
    }
    for (; i < num_points; ++i) {
        cVector[i] = aVector[i] * bVector[i];
    }
}
#endif

typedef void (*Multiply32fcFn)(lv_32fc_t*, const lv_32fc_t*, const lv_32fc_t*, unsigned int);

inline Multiply32fcFn resolve_multiply_32fc() {
    auto& f = get_cpu_features();
#if defined(ISDBT_ARCH_X86)
    if (f.has_avx2) {
        return multiply_32fc_avx2;
    }
    if (f.has_sse3) {
        return multiply_32fc_sse3;
    }
#endif
    return multiply_32fc_scalar;
}

inline Multiply32fcFn g_multiply_32fc_fn = resolve_multiply_32fc();

} // namespace isdbt_simd

inline void volk_32fc_x2_multiply_32fc(lv_32fc_t* cVector, const lv_32fc_t* aVector, const lv_32fc_t* bVector, unsigned int num_points) {
    isdbt_simd::g_multiply_32fc_fn(cVector, aVector, bVector, num_points);
}
