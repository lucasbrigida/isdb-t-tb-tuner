#include <iostream>
#include <vector>
#include <complex>
#include <cassert>
#include <cmath>
#include <algorithm>
#define private public
#include "sync/sp_cir_sync.h"
#undef private
#include "dsp/constants.hpp"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while(0)

int main() {
    std::cout << "=== Teste Unitario SpCirSynchronizer (Speth & Meyr / Larroca et al.) ===\n";

    isdbt::SyncParams params;
    params.mode = isdbt::OfdmMode::MODE_1_2K;
    params.gi = isdbt::GuardInterval::GI_1_16;
    params.sample_rate = isdbt::constants::SAMPLE_RATE_HZ;
    params.fft_size = 2048;
    params.gi_size = 128;

    isdbt::SpCirSynchronizer sync(params);

    // 1. Estado inicial
    TEST_ASSERT(sync.sp_cir_state() == isdbt::SpCirSyncState::Acquisition);
    TEST_ASSERT(sync.state() == isdbt::SyncState::Acquisition);

    // 2. Gera sinal OFDM sintetico continuo
    constexpr size_t N_FFT = 2048;
    constexpr size_t N_GI  = 128;
    constexpr size_t SYM_LEN = N_FFT + N_GI;
    constexpr size_t NUM_SYMS = 30;
    std::vector<std::complex<float>> signal(SYM_LEN * NUM_SYMS);

    uint32_t lfsr = 0xACE1u;
    size_t pos = 0;
    for (size_t s = 0; s < NUM_SYMS; ++s) {
        std::vector<std::complex<float>> useful(N_FFT);
        for (size_t k = 0; k < N_FFT; ++k) {
            lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xD0000001u);
            float re = (lfsr & 2u) ? 0.7071f : -0.7071f;
            float im = (lfsr & 4u) ? 0.7071f : -0.7071f;
            useful[k] = std::complex<float>(re, im);
        }
        for (size_t m = 0; m < SYM_LEN; ++m, ++pos) {
            signal[pos] = (m < N_GI) ? useful[N_FFT - N_GI + m] : useful[m - N_GI];
        }
    }

    std::vector<std::complex<float>> offset_signal(32768);
    constexpr size_t offset_samples = 2 * SYM_LEN + 2;
    std::copy_n(signal.begin(), std::min(signal.size(), offset_signal.size() - offset_samples),
                offset_signal.begin() + offset_samples);
    isdbt::SpCirSynchronizer offset_sync(params);
    std::vector<std::complex<float>> offset_output;
    offset_sync.process(offset_signal.data(), offset_signal.size(), offset_output);
    TEST_ASSERT(offset_sync.sp_cir_state() == isdbt::SpCirSyncState::Tracking);
    TEST_ASSERT(!offset_output.empty());

    std::vector<std::complex<float>> out_symbols;
    out_symbols.reserve(SYM_LEN * 4);

    // Processa primeiro bloco
    sync.process(signal.data(), 32768, out_symbols);
    TEST_ASSERT(sync.sp_cir_state() == isdbt::SpCirSyncState::Tracking);
    TEST_ASSERT(sync.state() == isdbt::SyncState::Tracking);
    TEST_ASSERT(!out_symbols.empty());

    // 3. Processa restante do sinal em modo tracking
    sync.process(signal.data() + 32768, signal.size() - 32768, out_symbols);
    TEST_ASSERT(sync.sp_cir_state() == isdbt::SpCirSyncState::Tracking);
    TEST_ASSERT(!out_symbols.empty());

    // 4. Injeta ruido puro para testar transicao Tracking -> Resync -> LostLock
    std::vector<std::complex<float>> noise(32768);
    for (size_t i = 0; i < noise.size(); ++i) {
        lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xD0000001u);
        noise[i] = std::complex<float>((lfsr & 2u) ? 0.5f : -0.5f, (lfsr & 4u) ? 0.5f : -0.5f);
    }

    for (int it = 0; it < 5; ++it) {
        sync.process(noise.data(), noise.size(), out_symbols);
    }
    TEST_ASSERT(sync.sp_cir_state() == isdbt::SpCirSyncState::LostLock);
    TEST_ASSERT(sync.state() == isdbt::SyncState::LostLock);

    // 5. Reinicia e valida re-lock
    sync.reset();
    TEST_ASSERT(sync.sp_cir_state() == isdbt::SpCirSyncState::Acquisition);
    sync.process(signal.data(), 32768, out_symbols);
    TEST_ASSERT(sync.sp_cir_state() == isdbt::SpCirSyncState::Tracking);

    // 6. Detecta cada um dos intervalos de guarda suportados pelo ISDB-T
    for (size_t gi_size : {size_t{64}, size_t{128}, size_t{256}, size_t{512}}) {
        const size_t symbol_len = N_FFT + gi_size;
        std::vector<std::complex<float>> gi_signal(symbol_len * NUM_SYMS);
        size_t gi_pos = 0;
        for (size_t s = 0; s < NUM_SYMS; ++s) {
            std::vector<std::complex<float>> useful(N_FFT);
            for (size_t k = 0; k < N_FFT; ++k) {
                lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xD0000001u);
                useful[k] = std::complex<float>((lfsr & 2u) ? 0.7071f : -0.7071f,
                                                (lfsr & 4u) ? 0.7071f : -0.7071f);
            }
            for (size_t m = 0; m < symbol_len; ++m, ++gi_pos) {
                gi_signal[gi_pos] = (m < gi_size)
                    ? useful[N_FFT - gi_size + m]
                    : useful[m - gi_size];
            }
        }

        isdbt::SyncParams gi_params = params;
        gi_params.gi_size = gi_size;
        gi_params.gi = static_cast<isdbt::GuardInterval>(N_FFT / gi_size);
        isdbt::SpCirSynchronizer gi_sync(gi_params);
        std::vector<std::complex<float>> gi_output;
        gi_sync.process(gi_signal.data(), 32768, gi_output);
        TEST_ASSERT(gi_sync.sp_cir_state() == isdbt::SpCirSyncState::Tracking);
        TEST_ASSERT(gi_sync.guard_interval_size() == gi_size);
        gi_sync.process(gi_signal.data() + 32768, gi_signal.size() - 32768, gi_output);
        TEST_ASSERT(gi_sync.sp_cir_state() == isdbt::SpCirSyncState::Tracking);
    }

    // 7. Validacao do detector de Resposta ao Impulso de Canal (CIR) e First Arrival Path
    {
        // 7.1 Rejeicao de ruido puro (PAPR baixo de ~4.7)
        std::vector<std::complex<float>> cir_noise(64);
        for (size_t i = 0; i < 64; ++i) {
            lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xD0000001u);
            cir_noise[i] = std::complex<float>((lfsr & 2u) ? 0.7f : -0.7f,
                                               (lfsr & 4u) ? 0.7f : -0.7f);
        }
        float metric_noise = 1.0f;
        int shift_noise = sync.detect_first_path(cir_noise.data(), 64, &metric_noise);
        TEST_ASSERT(shift_noise == 0);
        TEST_ASSERT(metric_noise == 0.0f); // Rejeita como nao-piloto (PAPR < 12)

        // 7.2 Deteccao de canal ideal com atraso tau = +3 amostras
        std::vector<std::complex<float>> cir_ideal(64, std::complex<float>(0.0f, 0.0f));
        cir_ideal[3] = std::complex<float>(10.0f, 0.0f); // Pico dominante
        for (size_t i = 0; i < 64; ++i) {
            if (i != 3) cir_ideal[i] = std::complex<float>(0.05f, -0.05f); // Piso de ruido
        }
        float metric_ideal = 0.0f;
        int shift_ideal = sync.detect_first_path(cir_ideal.data(), 64, &metric_ideal);
        TEST_ASSERT(shift_ideal == 3);
        TEST_ASSERT(metric_ideal > 0.5f);

        // 7.3 Multipercurso urbano severo:
        // Primeiro caminho em tau = 0 (magnitude 6.0, 36% da potencia do pico) e eco em tau = +4 (magnitude 10.0)
        // O sincronizador DEVE selecionar o Primeiro Caminho (tau = 0) para prevenir ISI
        std::vector<std::complex<float>> cir_multipath(64, std::complex<float>(0.0f, 0.0f));
        cir_multipath[0] = std::complex<float>(6.0f, 0.0f);  // Primeiro caminho
        cir_multipath[4] = std::complex<float>(10.0f, 0.0f); // Eco mais forte
        float metric_mp = 0.0f;
        int shift_mp = sync.detect_first_path(cir_multipath.data(), 64, &metric_mp);
        TEST_ASSERT(shift_mp == 0); // Detecta primeiro caminho, e nao o pico em +4
        TEST_ASSERT(metric_mp > 0.5f);
    }

    // 8. Validacao da ancoragem de quadro TMCC e polimorfismo DIP
    {
        isdbt::IOFDMSynchronizer* poly_sync = &sync;
        TEST_ASSERT(!poly_sync->is_tmcc_anchored());
        poly_sync->notify_frame_sync(0); // Ancoragem do simbolo 0 do TMCC
        TEST_ASSERT(poly_sync->is_tmcc_anchored());
        TEST_ASSERT(std::string(poly_sync->detailed_state_str()).find("TMCC") != std::string::npos ||
                    std::string(poly_sync->detailed_state_str()).find("Tracking") != std::string::npos);
        // Anti-windup: garante que o SCO em ppm nunca extrapole o limite fisico do cristal (~37 ppm)
        TEST_ASSERT(std::abs(poly_sync->estimated_sco_ppm()) <= 40.0f);
    }

    std::cout << "  Transicoes de estado: " << sync.state_transitions() << "\n";
    std::cout << "[PASS] SpCirSynchronizer testado e aprovado com sucesso!\n";
    return 0;
}
