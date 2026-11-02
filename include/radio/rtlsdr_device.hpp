#pragma once

#include <cstdint>
#include <rtl-sdr.h>

namespace isdbt {

// Sintoniza com seguranca a frequencia central no RTL-SDR sem colidir endpoints USB (LIBUSB_ERROR_PIPE -9)
// e aguarda a estabilizacao do sintetizador PLL do tuner Rafael Micro R820T/R828D (Regra 8 e Erro 14 AGENTS.md).
bool safe_set_freq(rtlsdr_dev_t* dev, uint32_t freq_hz);

} // namespace isdbt

