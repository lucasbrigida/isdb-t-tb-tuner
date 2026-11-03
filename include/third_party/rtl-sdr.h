#pragma once
#include <cstdint>
#ifdef _WIN32
#include <windows.h>
#endif

typedef struct rtlsdr_dev rtlsdr_dev_t;

#ifdef _WIN32
namespace rtlsdr_dyn {
    inline HMODULE get_lib() {
        static HMODULE h = nullptr;
        static bool tried = false;
        if (!tried) {
            tried = true;
            // 1. Tenta carregar do diretorio do executavel ou PATH padrao
            h = LoadLibraryA("librtlsdr.dll");
            if (!h) h = LoadLibraryA("rtlsdr.dll");
            // 2. Se executado dentro da arvore do repositorio, tenta carregar da pasta tools/ correspondente
            if (!h) {
                if (sizeof(void*) == 4) {
                    h = LoadLibraryA("tools\\rtl-sdr\\x86\\librtlsdr.dll");
                } else {
                    h = LoadLibraryA("tools\\rtl-sdr\\x64\\librtlsdr.dll");
                }
            }
        }
        return h;
    }
}
#endif

inline uint32_t rtlsdr_get_device_count() {
#ifdef _WIN32
    if (HMODULE h = rtlsdr_dyn::get_lib()) {
        auto fn = reinterpret_cast<uint32_t(*)()>(GetProcAddress(h, "rtlsdr_get_device_count"));
        if (fn) return fn();
    }
#endif
    return 0;
}

inline const char* rtlsdr_get_device_name(uint32_t index) {
#ifdef _WIN32
    if (HMODULE h = rtlsdr_dyn::get_lib()) {
        auto fn = reinterpret_cast<const char*(*)(uint32_t)>(GetProcAddress(h, "rtlsdr_get_device_name"));
        if (fn) return fn(index);
    }
#endif
    return "Generic RTL2832U OEM";
}

inline int rtlsdr_get_device_usb_strings(uint32_t index, char* manufact, char* product, char* serial) {
#ifdef _WIN32
    if (HMODULE h = rtlsdr_dyn::get_lib()) {
        auto fn = reinterpret_cast<int(*)(uint32_t, char*, char*, char*)>(GetProcAddress(h, "rtlsdr_get_device_usb_strings"));
        if (fn) return fn(index, manufact, product, serial);
    }
#endif
    return -1;
}

inline int rtlsdr_open(rtlsdr_dev_t** dev, uint32_t index) {
#ifdef _WIN32
    if (HMODULE h = rtlsdr_dyn::get_lib()) {
        auto fn = reinterpret_cast<int(*)(rtlsdr_dev_t**, uint32_t)>(GetProcAddress(h, "rtlsdr_open"));
        if (fn) return fn(dev, index);
    }
#endif
    return -1;
}

inline int rtlsdr_close(rtlsdr_dev_t* dev) {
#ifdef _WIN32
    if (HMODULE h = rtlsdr_dyn::get_lib()) {
        auto fn = reinterpret_cast<int(*)(rtlsdr_dev_t*)>(GetProcAddress(h, "rtlsdr_close"));
        if (fn && dev) return fn(dev);
    }
#endif
    return 0;
}

inline int rtlsdr_get_tuner_gains(rtlsdr_dev_t* dev, int* gains) {
#ifdef _WIN32
    if (HMODULE h = rtlsdr_dyn::get_lib()) {
        auto fn = reinterpret_cast<int(*)(rtlsdr_dev_t*, int*)>(GetProcAddress(h, "rtlsdr_get_tuner_gains"));
        if (fn && dev) return fn(dev, gains);
    }
#endif
    return 0;
}

inline int rtlsdr_set_sample_rate(rtlsdr_dev_t* dev, uint32_t samp_rate) {
#ifdef _WIN32
    if (HMODULE h = rtlsdr_dyn::get_lib()) {
        auto fn = reinterpret_cast<int(*)(rtlsdr_dev_t*, uint32_t)>(GetProcAddress(h, "rtlsdr_set_sample_rate"));
        if (fn && dev) return fn(dev, samp_rate);
    }
#endif
    return 0;
}

inline int rtlsdr_set_center_freq(rtlsdr_dev_t* dev, uint32_t freq) {
#ifdef _WIN32
    if (HMODULE h = rtlsdr_dyn::get_lib()) {
        auto fn = reinterpret_cast<int(*)(rtlsdr_dev_t*, uint32_t)>(GetProcAddress(h, "rtlsdr_set_center_freq"));
        if (fn && dev) return fn(dev, freq);
    }
#endif
    return 0;
}

inline int rtlsdr_set_tuner_gain_mode(rtlsdr_dev_t* dev, int manual) {
#ifdef _WIN32
    if (HMODULE h = rtlsdr_dyn::get_lib()) {
        auto fn = reinterpret_cast<int(*)(rtlsdr_dev_t*, int)>(GetProcAddress(h, "rtlsdr_set_tuner_gain_mode"));
        if (fn && dev) return fn(dev, manual);
    }
#endif
    return 0;
}

inline int rtlsdr_set_tuner_gain(rtlsdr_dev_t* dev, int gain) {
#ifdef _WIN32
    if (HMODULE h = rtlsdr_dyn::get_lib()) {
        auto fn = reinterpret_cast<int(*)(rtlsdr_dev_t*, int)>(GetProcAddress(h, "rtlsdr_set_tuner_gain"));
        if (fn && dev) return fn(dev, gain);
    }
#endif
    return 0;
}

inline int rtlsdr_set_agc_mode(rtlsdr_dev_t* dev, int on) {
#ifdef _WIN32
    if (HMODULE h = rtlsdr_dyn::get_lib()) {
        auto fn = reinterpret_cast<int(*)(rtlsdr_dev_t*, int)>(GetProcAddress(h, "rtlsdr_set_agc_mode"));
        if (fn && dev) return fn(dev, on);
    }
#endif
    return 0;
}

inline int rtlsdr_reset_buffer(rtlsdr_dev_t* dev) {
#ifdef _WIN32
    if (HMODULE h = rtlsdr_dyn::get_lib()) {
        auto fn = reinterpret_cast<int(*)(rtlsdr_dev_t*)>(GetProcAddress(h, "rtlsdr_reset_buffer"));
        if (fn && dev) return fn(dev);
    }
#endif
    return 0;
}

inline int rtlsdr_read_sync(rtlsdr_dev_t* dev, void* buf, int len, int* n_read) {
#ifdef _WIN32
    if (HMODULE h = rtlsdr_dyn::get_lib()) {
        auto fn = reinterpret_cast<int(*)(rtlsdr_dev_t*, void*, int, int*)>(GetProcAddress(h, "rtlsdr_read_sync"));
        if (fn && dev) return fn(dev, buf, len, n_read);
    }
#endif
    if (n_read) *n_read = 0;
    return -1;
}

