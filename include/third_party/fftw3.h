#pragma once
#include <cstdlib>
#include <cmath>

typedef float fftwf_complex[2];
typedef struct fftwf_plan_s {
    int n;
    fftwf_complex* in;
    fftwf_complex* out;
    int sign;
}* fftwf_plan;

#define FFTW_FORWARD -1
#define FFTW_BACKWARD 1
#define FFTW_ESTIMATE 0

inline void* fftwf_malloc(size_t n) { return std::malloc(n); }
inline void fftwf_free(void* p) { std::free(p); }

inline fftwf_plan fftwf_plan_dft_1d(int n, fftwf_complex* in, fftwf_complex* out, int sign, unsigned) {
    fftwf_plan p = (fftwf_plan)std::malloc(sizeof(struct fftwf_plan_s));
    p->n = n; p->in = in; p->out = out; p->sign = sign;
    return p;
}

inline void fftwf_destroy_plan(fftwf_plan p) { std::free(p); }

inline void fftwf_execute(const fftwf_plan p) {
    int N = p->n;
    for (int i = 0; i < N; ++i) {
        p->out[i][0] = p->in[i][0];
        p->out[i][1] = p->in[i][1];
    }
    int j = 0;
    for (int i = 1; i < N; ++i) {
        int bit = N >> 1;
        while (j & bit) { j ^= bit; bit >>= 1; }
        j ^= bit;
        if (i < j) {
            float tr = p->out[i][0], ti = p->out[i][1];
            p->out[i][0] = p->out[j][0]; p->out[i][1] = p->out[j][1];
            p->out[j][0] = tr; p->out[j][1] = ti;
        }
    }
    for (int len = 2; len <= N; len <<= 1) {
        float ang = 2.0f * 3.14159265358979323846f / len * p->sign;
        float wlen_r = std::cos(ang), wlen_i = std::sin(ang);
        for (int i = 0; i < N; i += len) {
            float wr = 1.0f, wi = 0.0f;
            for (int k = 0; k < len / 2; ++k) {
                float ur = p->out[i + k][0], ui = p->out[i + k][1];
                float vr = p->out[i + k + len / 2][0] * wr - p->out[i + k + len / 2][1] * wi;
                float vi = p->out[i + k + len / 2][0] * wi + p->out[i + k + len / 2][1] * wr;
                p->out[i + k][0] = ur + vr; p->out[i + k][1] = ui + vi;
                p->out[i + k + len / 2][0] = ur - vr; p->out[i + k + len / 2][1] = ui - vi;
                float nwr = wr * wlen_r - wi * wlen_i;
                wi = wr * wlen_i + wi * wlen_r;
                wr = nwr;
            }
        }
    }
}

