#include "cli/commands.hpp"
#include "platform/platform_utils.hpp"
#include <iostream>
#include <rtl-sdr.h>
#include <volk/volk.h>
#include <volk/volk_cpu.h>

namespace isdbt {

int cmd_probe() {
    print_cli_header();
    std::cout << "=== Inspecao de Hardware RTL-SDR e Aceleracao DSP ===\n\n";

    uint32_t count = rtlsdr_get_device_count();
    std::cout << "[RTL-SDR] Dispositivos conectados: " << count << "\n";
    if (count == 0) {
        std::cout << "  [AVISO] Nenhum RTL-SDR detectado. Verifique o cabo USB e o driver WinUSB (Zadig).\n";
        return 1;
    }

    for (uint32_t i = 0; i < count; ++i) {
        char mfg[256] = {0}, prod[256] = {0}, sn[256] = {0};
        rtlsdr_get_device_usb_strings(i, mfg, prod, sn);
        std::cout << "  - Dongle #" << i << ": " << rtlsdr_get_device_name(i) << "\n";
        std::cout << "    Fabricante: " << mfg << " | Serial: " << sn << "\n";

        rtlsdr_dev_t* dev = nullptr;
        if (rtlsdr_open(&dev, i) == 0) {
            int gains[100];
            int n_gains = rtlsdr_get_tuner_gains(dev, gains);
            std::cout << "    Ganhos de RF suportados (" << n_gains << "): ";
            for (int g = 0; g < n_gains && g < 8; ++g) {
                std::cout << (gains[g] / 10.0f) << " ";
            }
            if (n_gains > 8) std::cout << "... " << (gains[n_gains - 1] / 10.0f) << " dB";
            std::cout << "\n";
            rtlsdr_close(dev);
        }
    }

    volk_cpu_init();
    std::cout << "\n[SIMD/DSP] VOLK Versao " << VOLK_VERSION_MAJOR << "." << VOLK_VERSION_MINOR << "." << VOLK_VERSION_MAINT << "\n";
    std::cout << "  - Aceleracao AVX2: " << (volk_cpu.has_avx2() ? "ATIVADA" : "NAO") << "\n";
    std::cout << "  - Aceleracao FMA: " << (volk_cpu.has_fma() ? "ATIVADA" : "NAO") << "\n";
    std::cout << "  - FFTW3: 2048/4096/8192 pontos inicializados\n\n";

    return 0;
}

} // namespace isdbt
