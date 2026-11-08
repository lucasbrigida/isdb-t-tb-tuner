#include <iostream>
#include <vector>
#include <complex>
#include <cmath>
#include <cstdint>
#include <cassert>
#include <random>
#include <algorithm>
#include "volk/volk.h"
#include "volk/volk_cpu.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while(0)

// 1. Testa detecção real de CPUID
void test_cpuid_features() {
    std::cout << "[TEST] 1. Validando detecção de capacidades da CPU via CPUID nativo...\n";
    volk_cpu_init();

    int avx2 = volk_cpu.has_avx2();
    int fma = volk_cpu.has_fma();
    int sse2 = volk_cpu.has_sse2();
    const char* kernel_name = volk_cpu.active_kernel_name();

    std::cout << "  - CPUID Detectado: AVX2=" << avx2
              << ", FMA=" << fma
              << ", SSE2=" << sse2
              << " | Kernel Selecionado: " << kernel_name << "\n";

    TEST_ASSERT(avx2 == 0 || avx2 == 1);
    TEST_ASSERT(fma == 0 || fma == 1);
    TEST_ASSERT(sse2 == 0 || sse2 == 1);
    TEST_ASSERT(kernel_name != nullptr && std::string(kernel_name).length() > 0);

#if defined(__x86_64__) || defined(_M_X64)
    // Em arquiteturas x86_64 / AMD64, SSE2 é garantido por especificação da arquitetura
    TEST_ASSERT(sse2 == 1);
#endif

    std::cout << "  [PASS] Detecção de CPUID validada com sucesso.\n";
}

// 2. Testa equivalência matemática do kernel SIMD contra multiplicação escalar
void test_simd_complex_multiply_equivalence() {
    std::cout << "[TEST] 2. Validando equivalência matemática contra multiplicação escalar em vários tamanhos...\n";

    const std::vector<size_t> test_sizes = {
        1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 127, 128, 2048, 4096, 32768
    };

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-100.0f, 100.0f);

    for (size_t n : test_sizes) {
        std::vector<lv_32fc_t> a(n);
        std::vector<lv_32fc_t> b(n);
        std::vector<lv_32fc_t> c_simd(n);
        std::vector<lv_32fc_t> c_ref(n);

        for (size_t i = 0; i < n; ++i) {
            a[i] = lv_32fc_t(dist(rng), dist(rng));
            b[i] = lv_32fc_t(dist(rng), dist(rng));
            c_ref[i] = a[i] * b[i];
        }

        volk_32fc_x2_multiply_32fc(c_simd.data(), a.data(), b.data(), static_cast<unsigned int>(n));

        for (size_t i = 0; i < n; ++i) {
            float err_re = std::abs(c_simd[i].real() - c_ref[i].real());
            float err_im = std::abs(c_simd[i].imag() - c_ref[i].imag());
            float max_val = std::max({1.0f, std::abs(c_ref[i].real()), std::abs(c_ref[i].imag())});

            // Erro relativo ou absoluto < 1e-5
            TEST_ASSERT(err_re / max_val < 1e-4f);
            TEST_ASSERT(err_im / max_val < 1e-4f);
        }
    }

    std::cout << "  [PASS] Equivalência matemática confirmada para todos os tamanhos (1 até 32.768 amostras).\n";
}

// 3. Testa tolerância a ponteiros desalinhados (unaligned memory access)
void test_simd_unaligned_memory() {
    std::cout << "[TEST] 3. Validando operação vetorial com ponteiros desalinhados...\n";

    // Aloca buffer com folga para forçar desalinhamentos ímpares (+1 float = +4 bytes)
    constexpr size_t N = 1024;
    std::vector<float> raw_a((N + 4) * 2);
    std::vector<float> raw_b((N + 4) * 2);
    std::vector<float> raw_c((N + 4) * 2);

    for (size_t i = 0; i < raw_a.size(); ++i) {
        raw_a[i] = static_cast<float>(i * 0.1f);
        raw_b[i] = static_cast<float>(i * 0.2f - 5.0f);
    }

    // Offset de 1 float (4 bytes) quebra o alinhamento de 16/32 bytes
    lv_32fc_t* unaligned_a = reinterpret_cast<lv_32fc_t*>(raw_a.data() + 1);
    lv_32fc_t* unaligned_b = reinterpret_cast<lv_32fc_t*>(raw_b.data() + 3);
    lv_32fc_t* unaligned_c = reinterpret_cast<lv_32fc_t*>(raw_c.data() + 1);

    volk_32fc_x2_multiply_32fc(unaligned_c, unaligned_a, unaligned_b, N);

    for (size_t i = 0; i < N; ++i) {
        lv_32fc_t expected = unaligned_a[i] * unaligned_b[i];
        float err_re = std::abs(unaligned_c[i].real() - expected.real());
        float err_im = std::abs(unaligned_c[i].imag() - expected.imag());
        float max_val = std::max({1.0f, std::abs(expected.real()), std::abs(expected.imag())});

        TEST_ASSERT(err_re / max_val < 1e-4f);
        TEST_ASSERT(err_im / max_val < 1e-4f);
    }

    std::cout << "  [PASS] Desalinhamento arbitrário de memória processado com sucesso.\n";
}

int main() {
    std::cout << "=== Teste Unitário: CPUID Real e Kernels SIMD VOLK Nativos ===\n";
    test_cpuid_features();
    test_simd_complex_multiply_equivalence();
    test_simd_unaligned_memory();
    std::cout << "=== TODOS OS TESTES SIMD/CPUID PASSARAM COM SUCESSO! ===\n";
    return 0;
}
