#pragma once

#include <cstdint>
#include "dsp/constants.hpp"
#include "dsp/ofdm_sync.hpp"

namespace isdbt {

enum class OFDMAlgorithm : int {
    VanDeBeek = 1,     ///< Algoritmo baseline de Van de Beek (padrao)
    SpCir = 2,         ///< Sincronizador SP-CIR de 2 estagios (Speth & Meyr / Larroca et al.)
    RobustGRISDBT = 2  ///< Alias de compatibilidade retroativa com a denominacao legada
};

struct SyncParams {
    OfdmMode mode = OfdmMode::MODE_1_2K;
    GuardInterval gi = GuardInterval::GI_1_16;
    float sample_rate = constants::SAMPLE_RATE_HZ;
    size_t fft_size = 2048;
    size_t gi_size = 128;
    float min_correlation = 0.40f;
};

} // namespace isdbt
