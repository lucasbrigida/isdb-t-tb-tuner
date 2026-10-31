#pragma once
#include <complex>

typedef std::complex<float> lv_32fc_t;

#define VOLK_VERSION_MAJOR 3
#define VOLK_VERSION_MINOR 1
#define VOLK_VERSION_MAINT 2

inline void volk_32fc_x2_multiply_32fc(lv_32fc_t* cVector, const lv_32fc_t* aVector, const lv_32fc_t* bVector, unsigned int num_points) {
    for (unsigned int i = 0; i < num_points; ++i) {
        cVector[i] = aVector[i] * bVector[i];
    }
}

