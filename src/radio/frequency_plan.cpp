#include "radio/frequency_plan.hpp"

namespace isdbt {

uint32_t get_channel_freq(int channel_number) {
    double freq_mhz = 470.0 + (channel_number - 14) * 6.0 + 3.0 + (1.0 / 7.0);
    return static_cast<uint32_t>(freq_mhz * 1e6);
}

bool is_valid_channel(int channel_number) {
    return (channel_number >= 14 && channel_number <= 69);
}

SignalRating get_signal_rating(float rssi_dbfs) {
    if (rssi_dbfs >= -6.0f) {
        return {"Alto (Forte)",   "", "[#####]"};
    } else if (rssi_dbfs >= -12.0f) {
        return {"Normal (Bom)",   "", "[#### ]"};
    } else if (rssi_dbfs >= -18.0f) {
        return {"Moderado",       "", "[###  ]"};
    } else if (rssi_dbfs >= -26.5f) {
        return {"Fraco",          "", "[##   ]"};
    } else {
        return {"Sem Sinal",      "", "[     ]"};
    }
}

std::string pad_string(const std::string& str, size_t width, bool left_align) {
    if (str.length() >= width) return str.substr(0, width);
    if (left_align) {
        return str + std::string(width - str.length(), ' ');
    } else {
        return std::string(width - str.length(), ' ') + str;
    }
}

} // namespace isdbt
