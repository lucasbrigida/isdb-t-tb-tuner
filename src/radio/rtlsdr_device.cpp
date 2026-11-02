#include "radio/rtlsdr_device.hpp"
#include <thread>
#include <chrono>

namespace isdbt {

bool safe_set_freq(rtlsdr_dev_t* dev, uint32_t freq_hz) {
    if (!dev) return false;
    rtlsdr_reset_buffer(dev);
    int r = rtlsdr_set_center_freq(dev, freq_hz);
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Estabilizacao do PLL do tuner R820T (Regra 8 AGENTS.md)
    rtlsdr_reset_buffer(dev);
    return (r == 0);
}

} // namespace isdbt

