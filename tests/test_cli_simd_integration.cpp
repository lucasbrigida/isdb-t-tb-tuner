#include <iostream>
#include <vector>
#include <complex>
#include <cmath>
#include <cassert>
#include <string>
#include <algorithm>
#include "platform/platform_utils.hpp"
#include "third_party/volk/volk.h"
#include "third_party/volk/volk_cpu.h"
#include "sync/sync_factory.h"
#include "sync/sp_cir_sync.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int main() {
    std::cout << "[TEST] Iniciando validacao de integracao SIMD / VOLK nos comandos e DSP...\n";

    // 1. Validar inicializacao via setup_platform()
    isdbt::setup_platform();
    
    std::string kernel_name = volk_cpu.active_kernel_name();
    std::cout << "  Kernel ativo detectado: " << kernel_name << "\n";
    assert(!kernel_name.empty());
    assert(volk_32fc_x2_multiply_32fc != nullptr);

    // 2. Validar precisao numerica do kernel ativo contra referencia escalar
    constexpr size_t N = 1024;
    std::vector<std::complex<float>> input(N);
    std::vector<std::complex<float>> rotator(N);
    std::vector<std::complex<float>> output_simd(N);
    std::vector<std::complex<float>> output_ref(N);

    float freq_offset = 12345.67f;
    float fs = 2031746.0f;
    float phase_step = -2.0f * static_cast<float>(M_PI) * freq_offset / fs;
    float cur_phase = 0.5f;

    for (size_t i = 0; i < N; ++i) {
        float re = std::sin(static_cast<float>(i) * 0.1f);
        float im = std::cos(static_cast<float>(i) * 0.15f);
        input[i] = std::complex<float>(re, im);

        rotator[i] = std::complex<float>(std::cos(cur_phase), std::sin(cur_phase));
        cur_phase = std::fmod(cur_phase + phase_step, 2.0f * static_cast<float>(M_PI));

        output_ref[i] = input[i] * rotator[i];
    }

    // Executa via despacho VOLK SIMD
    volk_32fc_x2_multiply_32fc(output_simd.data(), input.data(), rotator.data(), static_cast<unsigned int>(N));

    float max_err = 0.0f;
    for (size_t i = 0; i < N; ++i) {
        float err_re = std::abs(output_simd[i].real() - output_ref[i].real());
        float err_im = std::abs(output_simd[i].imag() - output_ref[i].imag());
        max_err = std::max(max_err, std::max(err_re, err_im));
    }
    std::cout << "  Erro maximo absoluto (VOLK vs Ref): " << max_err << "\n";
    assert(max_err < 1e-5f);

    // 3. Validar integracao ponta a ponta com RobustGRISDBTSync (OFDM2)
    isdbt::SyncParams params;
    params.sample_rate = fs;
    auto sync = isdbt::make_synchronizer(isdbt::OFDMAlgorithm::RobustGRISDBT, params);
    assert(sync != nullptr);

    std::vector<std::complex<float>> useful_symbols;
    useful_symbols.reserve(N);

    // Processa bloco de amostras (executando NCO SIMD internamente)
    sync->process(input.data(), N, useful_symbols);
    std::cout << "  RobustGRISDBTSync processou " << N << " amostras com sucesso.\n";

    // Reset de sincronizador
    sync->reset();
    std::cout << "  RobustGRISDBTSync resetou com sucesso.\n";

    std::cout << "[SUCESSO] Integracao SIMD / VOLK validada com exito!\n";
    return 0;
}
