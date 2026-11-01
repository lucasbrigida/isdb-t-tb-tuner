#include "dsp/oneseg_demodulator.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace isdbt {

// Tabela de Permutacao em Frequencia para Modo 3 (384 portadoras de dados)
// Idêntica a gr-isdbt (frequency_deinterleaver_impl.cc) e ABNT NBR 15601 / ARIB STD-B31
static const uint16_t FREQ_PERM_MODE3[384] = {
    62, 13, 371, 11, 285, 336, 365, 220, 226, 92, 56, 46, 120, 175, 298, 352, 172, 235, 53, 164,
    368, 187, 125, 82, 5, 45, 173, 258, 135, 182, 141, 273, 126, 264, 286, 88, 233, 61, 249, 367,
    310, 179, 155, 57, 123, 208, 14, 227, 100, 311, 205, 79, 184, 185, 328, 77, 115, 277, 112, 20,
    199, 178, 143, 152, 215, 204, 139, 234, 358, 192, 309, 183, 81, 129, 256, 314, 101, 43, 97,
    324, 142, 157, 90, 214, 102, 29, 303, 363, 261, 31, 22, 52, 305, 301, 293, 177, 116, 296, 85,
    196, 191, 114, 58, 198, 16, 167, 145, 119, 245, 113, 295, 193, 232, 17, 108, 283, 246, 64, 237,
    189, 128, 373, 302, 320, 239, 335, 356, 39, 347, 351, 73, 158, 276, 243, 99, 38, 287, 3, 330,
    153, 315, 117, 289, 213, 210, 149, 383, 337, 339, 151, 241, 321, 217, 30, 334, 161, 322, 49,
    176, 359, 12, 346, 60, 28, 229, 265, 288, 225, 382, 59, 181, 170, 319, 341, 86, 251, 133, 344,
    361, 109, 44, 369, 268, 257, 323, 55, 317, 381, 121, 360, 260, 275, 190, 19, 63, 18, 248, 9,
    240, 211, 150, 230, 332, 231, 71, 255, 350, 355, 83, 87, 154, 218, 138, 269, 348, 130, 160,
    278, 377, 216, 236, 308, 223, 254, 25, 98, 300, 201, 137, 219, 36, 325, 124, 66, 353, 169, 21,
    35, 107, 50, 106, 333, 326, 262, 252, 271, 263, 372, 136, 0, 366, 206, 159, 122, 188, 6, 284,
    96, 26, 200, 197, 186, 345, 340, 349, 103, 84, 228, 212, 2, 67, 318, 1, 74, 342, 166, 194, 33,
    68, 267, 111, 118, 140, 195, 105, 202, 291, 259, 23, 171, 65, 281, 24, 165, 8, 94, 222, 331,
    34, 238, 364, 376, 266, 89, 80, 253, 163, 280, 247, 4, 362, 379, 290, 279, 54, 78, 180, 72,
    316, 282, 131, 207, 343, 370, 306, 221, 132, 7, 148, 299, 168, 224, 48, 47, 357, 313, 75, 104,
    70, 147, 40, 110, 374, 69, 146, 37, 375, 354, 174, 41, 32, 304, 307, 312, 15, 272, 134, 242,
    203, 209, 380, 162, 297, 327, 10, 93, 42, 250, 156, 338, 292, 144, 378, 294, 329, 127, 270, 76,
    95, 91, 244, 274, 27, 51
};

// Portadoras TMCC dedicadas (indices locais 0..431 dentro do segmento central)
static const size_t TMCC_CARRIERS[4] = {101, 131, 286, 349};

// Portadoras AC (Auxiliary Channel) dedicadas
static const size_t AC_CARRIERS[8] = {7, 89, 206, 209, 226, 244, 377, 407};

// Palavras de sincronismo TMCC (16 bits) para quadros pares e impares (tmcc_decoder_impl.cc)
static const uint8_t TMCC_SYNC_EVEN[16] = {0, 0, 1, 1, 0, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0};
static const uint8_t TMCC_SYNC_ODD[16]  = {1, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1};

// Polinomio verificador de paridade BCH(273, 191) do TMCC (tmcc_decoder_impl.cc linhas 59-66)
static const uint8_t TMCC_BCH_H[192] = {
    1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0,
    1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0,
    1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 1,
    0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1,
    0, 1, 1, 1, 0, 1, 1, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0,
    0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1,
    1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1
};

void OneSegDemodulator::init_pilots() {
    // Gerador PRBS de 11 bits (1 + X^9 + X^11) conforme gr-isdbt (ofdm_synchronization_impl.cc linha 275):
    // Como o registrador desloca para a direita (reg >> 1), o tap X^9 fica no bit 2 (11 - 9 = 2)!
    uint32_t reg_prbs = (1 << 11) - 1;
    full_prbs_.resize(5617);
    for (size_t k = 0; k < full_prbs_.size(); ++k) {
        uint32_t aux = reg_prbs & 0x01;
        uint32_t new_bit = ((reg_prbs >> 2) ^ (reg_prbs >> 0)) & 0x01;
        reg_prbs = (reg_prbs >> 1) | (new_bit << 10);
        full_prbs_[k] = (4.0f * 2.0f * (0.5f - static_cast<float>(aux))) / 3.0f;
    }
    // Segmento 0 (central) inicia na portadora ativa 6 * 432 = 2592 (inclui 433 pontos para o piloto de borda direita)
    prbs_pilots_.assign(full_prbs_.begin() + 2592, full_prbs_.begin() + 2592 + 433);
}

void OneSegDemodulator::init_time_deinterleaver(int I) {
    time_interleave_I_ = I;
    time_deint_fifos_.clear();
    time_deint_fifos_.resize(384);
    for (size_t c = 0; c < 384; ++c) {
        size_t mi = (5 * c) % 96;
        size_t delay = static_cast<size_t>(I) * (95 - mi);
        time_deint_fifos_[c].assign(delay, std::complex<float>(0.0f, 0.0f));
    }
}

void OneSegDemodulator::init_prbs_frame_masks() {
    // Pre-calcula as mascaras de 203 bytes do PRBS para cada indice de pacote dentro do quadro OFDM
    // conforme energy_descrambler_impl.cc do gr-isdbt (203 bytes XOR + 1 byte de clock no sync 0x47)
    Descrambler gen;
    gen.reset();
    prbs_frame_masks_.resize(128);
    for (size_t p = 0; p < 128; ++p) {
        for (size_t b = 0; b < 203; ++b) {
            prbs_frame_masks_[p][b] = gen.clock_byte();
        }
        gen.clock_byte(); // Clock extra durante o sync byte 0x47
    }
}

OneSegDemodulator::OneSegDemodulator(OfdmMode mode, GuardInterval gi)
    : mode_(mode), gi_(gi), fft_size_(static_cast<int>(mode)),
      ofdm_sync_(mode, gi), viterbi_(CodeRate::RATE_1_2) {
    init_pilots();
    init_prbs_frame_masks();
    fft_carriers_.resize(fft_size_);

    init_time_deinterleaver(4);

    // Desentrelaçador de bits para QPSK ({0, 120}) e 16QAM ({0, 40, 80, 120})
    bit_deint_fifos_[0].clear();
    bit_deint_fifos_[1].assign(120, 0.0f);
    bit_deint_fifos_[2].assign(80, 0.0f);
    bit_deint_fifos_[3].assign(120, 0.0f);

    // Desentrelaçador de bytes Forney (I=12, M=17): ramo j (0..11) tem retardo 17 * (11 - j)
    forney_fifos_.resize(12);
    for (size_t j = 0; j < 12; ++j) {
        size_t delay = 17 * (11 - j);
        forney_fifos_[j].assign(delay, 0);
    }

    tmcc_bits_.assign(204, 0);
}

void OneSegDemodulator::reset() {
    symbol_count_ = 0;
    ifo_bins_ = 0;
    ifo_agree_count_ = 0;
    ifo_locked_ = false;
    sym_mod4_ = 0;
    sym_phase_agree_count_ = 0;
    sym_phase_locked_ = false;
    prev_tmcc_.fill(std::complex<float>(0.0f, 0.0f));
    tmcc_bits_.assign(204, 0);
    since_last_tmcc_ = 203;
    tmcc_locked_ = false;
    frame_begin_pending_ = false;
    viterbi_.reset();
    descrambler_.reset();
    viterbi_byte_stream_.clear();
    viterbi_sync_offset_ = -1;
    sync_miss_count_ = 0;
    forney_blocks_fed_ = 0;
    packet_idx_in_frame_ = -1;
    init_time_deinterleaver(time_interleave_I_);
    for (size_t j = 0; j < 12; ++j) {
        forney_fifos_[j].assign(17 * (11 - j), 0);
    }
}

void OneSegDemodulator::set_parameters(CarrierModulation mod, CodeRate rate, int time_interleave_I) {
    bool changed = false;
    if (modulation_ != mod) {
        modulation_ = mod;
        changed = true;
        if (modulation_ == CarrierModulation::QPSK || modulation_ == CarrierModulation::DQPSK) {
            bit_deint_fifos_[0].clear();
            bit_deint_fifos_[1].assign(120, 0.0f);
        } else if (modulation_ == CarrierModulation::QAM16) {
            bit_deint_fifos_[0].clear();
            bit_deint_fifos_[1].assign(40, 0.0f);
            bit_deint_fifos_[2].assign(80, 0.0f);
            bit_deint_fifos_[3].assign(120, 0.0f);
        }
    }
    if (code_rate_ != rate) {
        code_rate_ = rate;
        changed = true;
        viterbi_.set_code_rate(rate);
        packet_idx_in_frame_ = -1;
    }
    if (time_interleave_I_ != time_interleave_I &&
        (time_interleave_I == 0 || time_interleave_I == 1 || time_interleave_I == 2 ||
         time_interleave_I == 4 || time_interleave_I == 8 || time_interleave_I == 16)) {
        changed = true;
        init_time_deinterleaver(time_interleave_I);
    }
    if (changed) {
        viterbi_byte_stream_.clear();
        viterbi_sync_offset_ = -1;
        sync_miss_count_ = 0;
        forney_blocks_fed_ = 0;
        packet_idx_in_frame_ = -1;
        for (size_t j = 0; j < 12; ++j) {
            forney_fifos_[j].assign(17 * (11 - j), 0);
        }
    }
}

bool OneSegDemodulator::check_tmcc_bch_parity() const {
    if (tmcc_bits_.size() < 204) return false;
    constexpr int N = 273;
    constexpr int K = 191;
    constexpr int R = N - K; // 82

    uint8_t tmcc_large[N] = {0};
    for (int k = 20; k < 204; ++k) {
        tmcc_large[89 + (k - 20)] = tmcc_bits_[k];
    }

    int s = 0;
    for (int j = 0; j < R; ++j) {
        int syndrome_j = 0;
        for (int i = 0; i < K + 1; ++i) {
            syndrome_j += tmcc_large[i + j] * TMCC_BCH_H[i];
        }
        if (syndrome_j % 2) s++;
    }
    return (s == 0);
}

void OneSegDemodulator::decode_tmcc_parameters() {
    // Extrai parametros da Camada A conforme tmcc_decoder_impl.cc linhas 435-456
    uint8_t mod_a = (tmcc_bits_[28] << 2) | (tmcc_bits_[29] << 1) | tmcc_bits_[30];
    uint8_t rate_a = (tmcc_bits_[31] << 2) | (tmcc_bits_[32] << 1) | tmcc_bits_[33];
    uint8_t int_a = (tmcc_bits_[34] << 2) | (tmcc_bits_[35] << 1) | tmcc_bits_[36];

    CarrierModulation new_mod = modulation_;
    if (mod_a == 0) new_mod = CarrierModulation::DQPSK;
    else if (mod_a == 1) new_mod = CarrierModulation::QPSK;
    else if (mod_a == 2) new_mod = CarrierModulation::QAM16;
    else if (mod_a == 3) new_mod = CarrierModulation::QAM64;

    CodeRate new_rate = code_rate_;
    if (rate_a == 0) new_rate = CodeRate::RATE_1_2;
    else if (rate_a == 1) new_rate = CodeRate::RATE_2_3;
    else if (rate_a == 2) new_rate = CodeRate::RATE_3_4;
    else if (rate_a == 3) new_rate = CodeRate::RATE_5_6;
    else if (rate_a == 4) new_rate = CodeRate::RATE_7_8;

    // Em Modo 3 (8K), os codigos de entrelaçamento de tempo (0, 1, 2, 3) correspondem a I = (0, 1, 2, 4)
    int new_I = time_interleave_I_;
    if (int_a == 0) new_I = 0;
    else if (int_a == 1) new_I = 1;
    else if (int_a == 2) new_I = 2;
    else if (int_a == 3) new_I = 4;

    set_parameters(new_mod, new_rate, new_I);
}

bool OneSegDemodulator::process_tmcc(const std::complex<float>* eq_seg_carriers) {
    // Demodulacao DBPSK das 4 portadoras TMCC do Segmento 0 (tmcc_decoder_impl.cc linhas 500-535)
    float soft_vote = 0.0f;
    for (size_t k = 0; k < 4; ++k) {
        size_t idx = TMCC_CARRIERS[k];
        std::complex<float> cur = eq_seg_carriers[idx] * (prbs_pilots_[idx] > 0.0f ? 1.0f : -1.0f);
        std::complex<float> phdiff = cur * std::conj(prev_tmcc_[k]);
        soft_vote += phdiff.real();
        prev_tmcc_[k] = cur;
    }

    uint8_t bit = (soft_vote >= 0.0f) ? 0 : 1;
    tmcc_bits_.pop_front();
    tmcc_bits_.push_back(bit);

    bool end_frame = false;
    if (since_last_tmcc_ >= 203) {
        bool match_even = true;
        bool match_odd = true;
        for (size_t i = 0; i < 16; ++i) {
            if (tmcc_bits_[1 + i] != TMCC_SYNC_EVEN[i]) match_even = false;
            if (tmcc_bits_[1 + i] != TMCC_SYNC_ODD[i])  match_odd = false;
        }
        if ((match_even || match_odd) && check_tmcc_bch_parity()) {
            end_frame = true;
            since_last_tmcc_ = 0;
            tmcc_locked_ = true;
            decode_tmcc_parameters();
        }
    }
    if (!end_frame) {
        since_last_tmcc_++;
    }
    return end_frame;
}

size_t OneSegDemodulator::process_symbol(const std::complex<float>* symbol_time_domain,
                                        std::vector<std::vector<uint8_t>>& out_ts_packets) {
    if (!symbol_time_domain) return 0;
    size_t pkts_before = out_ts_packets.size();

    // 1. Processamento da FFT de 2048 pontos e fftshift centralizado
    ofdm_sync_.process_symbol_fft(symbol_time_domain, fft_carriers_.data());

    // Em Modo 3 amostrado a 2031746 S/s (2048 pontos), o Segmento 0 nominal inicia no bin 808
    const int BASE_SEG_OFFSET = (fft_size_ - 432) / 2;

    // 2. Estimativa conjunta de IFO (Integer Frequency Offset) e Fase dos Pilotos Dispersos (sym_mod4 = 0..3)
    // Uma vez travado com coerencia > 0.60, restringe a busca a [ifo_bins_] para evitar que
    // ruidos transientes saltem o IFO e corrompam o fluxo TMCC DBPSK
    int min_delta = ifo_locked_ ? ifo_bins_ : -45;
    int max_delta = ifo_locked_ ? ifo_bins_ : +45;

    int best_delta = ifo_bins_;
    size_t best_phase = (sym_mod4_ + 1) % 4;
    float max_coh = -1.0f;

    for (int delta = min_delta; delta <= max_delta; ++delta) {
        int seg_start = BASE_SEG_OFFSET + delta;
        if (seg_start < 16 || seg_start + 433 >= fft_size_ - 16) continue;

        for (size_t p = 0; p < 4; ++p) {
            std::complex<float> sum_corr(0.0f, 0.0f);
            float sum_energy = 0.0f;
            for (size_t l = 3 * p; l + 12 < 432; l += 12) {
                std::complex<float> h0 = fft_carriers_[seg_start + l] / prbs_pilots_[l];
                std::complex<float> h1 = fft_carriers_[seg_start + l + 12] / prbs_pilots_[l + 12];
                sum_corr += h1 * std::conj(h0);
                sum_energy += std::sqrt(std::norm(h1) * std::norm(h0));
            }
            float coh = (sum_energy > 1e-12f) ? (std::abs(sum_corr) / sum_energy) : 0.0f;
            if (coh > max_coh) {
                max_coh = coh;
                best_delta = delta;
                best_phase = p;
            }
        }
    }

    if (!ifo_locked_) {
        // So aceita candidato a IFO se a coerencia for compativel com piloto verdadeiro (> 0.60)
        if (max_coh > 0.60f) {
            if (best_delta == ifo_bins_) {
                if (++ifo_agree_count_ >= 4) {
                    ifo_locked_ = true;
                    ifo_agree_count_ = 0;
                }
            } else {
                ifo_bins_ = best_delta;
                ifo_agree_count_ = 1;
            }
        }
    } else {
        // Se ja travou no IFO, mantem a portadora fixa. So destrava se a coerencia cair muito (< 0.20)
        // por 15 simbolos consecutivos
        if (max_coh < 0.20f) {
            if (++ifo_agree_count_ >= 15) {
                ifo_locked_ = false;
                ifo_agree_count_ = 0;
            }
        } else {
            ifo_agree_count_ = 0;
        }

        // Se o IFO travou mas o TMCC nao confirmou em 400 simbolos (~0.45s), destrava para reavaliar
        if (!tmcc_locked_ && symbol_count_ > 400 && (symbol_count_ % 204 == 0)) {
            ifo_locked_ = false;
            ifo_agree_count_ = 0;
        }
    }

    size_t expected_phase = (sym_mod4_ + 1) % 4;
    if (sym_phase_locked_) {
        if (best_phase == expected_phase || max_coh >= 0.50f) {
            sym_mod4_ = expected_phase;
        } else {
            sym_phase_locked_ = false;
            sym_phase_agree_count_ = 0;
            sym_mod4_ = best_phase;
        }
    } else {
        if (best_phase == expected_phase && max_coh > 0.50f) {
            if (++sym_phase_agree_count_ >= 3) sym_phase_locked_ = true;
        } else {
            sym_phase_agree_count_ = 1;
        }
        sym_mod4_ = best_phase;
    }

    // 3. Extracao das 433 portadoras (432 do Segmento 0 + 1 portadora de borda direita) corrigidas pelo IFO
    const int SEG_OFFSET = BASE_SEG_OFFSET + ifo_bins_;
    std::vector<std::complex<float>> seg_carriers(433);
    for (size_t i = 0; i < 433; ++i) {
        seg_carriers[i] = fft_carriers_[SEG_OFFSET + i];
    }

    // 4. Estimacao de Canal nos Pilotos Dispersos (SP) e Interpolacao Linear (ofdm_synchronization_impl.cc)
    std::vector<std::complex<float>> h(432, std::complex<float>(1.0f, 0.0f));
    std::vector<size_t> sp_indices;
    sp_indices.reserve(37);

    for (size_t l = 3 * sym_mod4_; l < 432; l += 12) {
        sp_indices.push_back(l);
        h[l] = seg_carriers[l] / prbs_pilots_[l];
    }

    // Interpolacao linear entre SPs adjacentes dentro do segmento
    for (size_t i = 0; i + 1 < sp_indices.size(); ++i) {
        size_t a = sp_indices[i];
        size_t b = sp_indices[i + 1];
        float span = static_cast<float>(b - a);
        auto ha = h[a];
        auto hb = h[b];
        for (size_t l = a + 1; l < b; ++l) {
            float t = static_cast<float>(l - a) / span;
            h[l] = ha * (1.0f - t) + hb * t;
        }
    }

    // Interpolacao nas bordas usando os pilotos dos segmentos adjacentes presentes na FFT
    if (!sp_indices.empty()) {
        size_t first_sp = sp_indices.front();
        if (first_sp > 0) {
            // Usa o ultimo SP do segmento anterior (SEG_OFFSET + first_sp - 12)
            int prev_seg_sp_bin = SEG_OFFSET + static_cast<int>(first_sp) - 12;
            size_t prev_prbs_idx = 2592 + first_sp - 12;
            std::complex<float> h_left = fft_carriers_[prev_seg_sp_bin] / full_prbs_[prev_prbs_idx];
            for (size_t l = 0; l < first_sp; ++l) {
                float t = static_cast<float>(l + 12 - first_sp) / 12.0f;
                h[l] = h_left * (1.0f - t) + h[first_sp] * t;
            }
        }
        size_t last_sp = sp_indices.back();
        if (last_sp < 431) {
            // Usa o primeiro SP do segmento seguinte (SEG_OFFSET + last_sp + 12)
            int next_seg_sp_bin = SEG_OFFSET + static_cast<int>(last_sp) + 12;
            size_t next_prbs_idx = 2592 + last_sp + 12;
            std::complex<float> h_right = fft_carriers_[next_seg_sp_bin] / full_prbs_[next_prbs_idx];
            for (size_t l = last_sp + 1; l < 432; ++l) {
                float t = static_cast<float>(l - last_sp) / 12.0f;
                h[l] = h[last_sp] * (1.0f - t) + h_right * t;
            }
        }
    }

    // 5. Equalizacao MMSE Regularizada de todas as 432 portadoras do Segmento 0
    // Em vez de Zero-Forcing puro que explode o ruido em nulos de fading (|H|^2 -> 0),
    // utiliza regularizacao MMSE proporcional a potencia media do canal
    float pwr_sum = 0.0f;
    for (size_t l = 0; l < 432; ++l) {
        pwr_sum += std::norm(h[l]);
    }
    float pwr_avg = pwr_sum / 432.0f;
    float sigma2 = std::max(1e-5f, 0.05f * pwr_avg); // SNR nominal de referencia ~13 dB

    std::vector<std::complex<float>> eq_seg(432);
    std::vector<float> csi_weight(432);
    for (size_t l = 0; l < 432; ++l) {
        float h_mag2 = std::norm(h[l]);
        eq_seg[l] = (seg_carriers[l] * std::conj(h[l])) / (h_mag2 + sigma2);
        // Peso CSI normalizado: portadoras atenuadas encolhem para ~0 (soft-bit 128 / neutro),
        // impedindo que ruido aleatorio desvie o caminho do Trellis do Viterbi
        csi_weight[l] = std::clamp(h_mag2 / (pwr_avg + 1e-6f), 0.0f, 2.0f);
    }

    // 6. Decodificacao TMCC e deteccao de inicio de quadro OFDM (204 simbolos)
    if (frame_begin_pending_) {
        packet_idx_in_frame_ = 0;
        frame_begin_pending_ = false;
    }
    if (process_tmcc(eq_seg.data())) {
        frame_begin_pending_ = true;
    }

    // 7. Extracao das 384 portadoras de dados ponderadas por CSI (excluindo SP, TMCC e AC)
    std::vector<std::complex<float>> data_carriers;
    data_carriers.reserve(384);

    bool is_sp[432] = {false};
    for (size_t sp : sp_indices) is_sp[sp] = true;
    bool is_tmcc[432] = {false};
    for (size_t tm : TMCC_CARRIERS) is_tmcc[tm] = true;
    bool is_ac[432] = {false};
    for (size_t ac : AC_CARRIERS) is_ac[ac] = true;

    for (size_t l = 0; l < 432; ++l) {
        if (!is_sp[l] && !is_tmcc[l] && !is_ac[l]) {
            // Aplica a ponderacao CSI diretamente ao simbolo complexo antes dos desentrelaçadores
            data_carriers.push_back(eq_seg[l] * csi_weight[l]);
        }
    }

    // 8. Desentrelaçamento em Frequencia (Permutacao de 384 portadoras)
    std::vector<std::complex<float>> freq_deint(384);
    for (size_t i = 0; i < 384; ++i) {
        freq_deint[i] = data_carriers[FREQ_PERM_MODE3[i]];
    }

    // 9. Desentrelaçamento no Tempo (FIFOs de profundidade I)
    std::vector<std::complex<float>> time_deint(384);
    for (size_t c = 0; c < 384; ++c) {
        if (time_deint_fifos_[c].empty()) {
            time_deint[c] = freq_deint[c];
        } else {
            time_deint_fifos_[c].push_back(freq_deint[c]);
            time_deint[c] = time_deint_fifos_[c].front();
            time_deint_fifos_[c].pop_front();
        }
    }

    // 10. Demapeamento suave (QPSK / 16QAM) com desentrelaçamento de bits (symbol_demapper + bit_deinterleaver)
    std::vector<uint8_t> soft_bits;
    if (modulation_ == CarrierModulation::QAM16) {
        soft_bits.reserve(384 * 4);
        constexpr float TH_16QAM = 2.0f / 3.16227766f; // 2 / sqrt(10)
        // Normaliza amplitude media pela energia dos pilotos (4/3)
        for (size_t c = 0; c < 384; ++c) {
            float re = time_deint[c].real();
            float im = time_deint[c].imag();
            // b3 (MSB): re < 0 (delay 120)
            // b2:       im < 0 (delay 80)
            // b1:       |re| < th (delay 40)
            // b0 (LSB): |im| < th (delay 0)
            float b0_raw = TH_16QAM - std::abs(im);
            float b1_raw = TH_16QAM - std::abs(re);
            float b2_raw = -im;
            float b3_raw = -re;

            bit_deint_fifos_[1].push_back(b1_raw);
            float b1_deint = bit_deint_fifos_[1].front();
            bit_deint_fifos_[1].pop_front();

            bit_deint_fifos_[2].push_back(b2_raw);
            float b2_deint = bit_deint_fifos_[2].front();
            bit_deint_fifos_[2].pop_front();

            bit_deint_fifos_[3].push_back(b3_raw);
            float b3_deint = bit_deint_fifos_[3].front();
            bit_deint_fifos_[3].pop_front();

            auto to_soft = [](float v) -> uint8_t {
                return static_cast<uint8_t>(std::clamp(static_cast<int>(128.0f + v * 64.0f), 0, 255));
            };
            soft_bits.push_back(to_soft(b3_deint));
            soft_bits.push_back(to_soft(b2_deint));
            soft_bits.push_back(to_soft(b1_deint));
            soft_bits.push_back(to_soft(b0_raw));
        }
    } else {
        // QPSK padrao 1-Seg: MSB (b1 = re < 0) tem atraso de 120, LSB (b0 = im < 0) tem atraso 0
        soft_bits.reserve(384 * 2);
        for (size_t c = 0; c < 384; ++c) {
            float lsb_raw = std::clamp(-time_deint[c].imag(), -4.0f, 4.0f);
            float msb_raw = std::clamp(-time_deint[c].real(), -4.0f, 4.0f);

            bit_deint_fifos_[1].push_back(msb_raw);
            float msb_deint = bit_deint_fifos_[1].front();
            bit_deint_fifos_[1].pop_front();

            uint8_t msb_byte = static_cast<uint8_t>(std::clamp(static_cast<int>(128.0f + msb_deint * 48.0f), 0, 255));
            uint8_t lsb_byte = static_cast<uint8_t>(std::clamp(static_cast<int>(128.0f + lsb_raw * 48.0f), 0, 255));

            soft_bits.push_back(msb_byte);
            soft_bits.push_back(lsb_byte);
        }
    }

    // 11. Decodificacao Convolucional Viterbi Continua (Inner FEC)
    std::vector<uint8_t> decoded_bytes;
    viterbi_.decode_continuous(soft_bits, decoded_bytes);
    viterbi_byte_stream_.insert(viterbi_byte_stream_.end(), decoded_bytes.begin(), decoded_bytes.end());

    // 12. Alinhamento de Pacotes de 204 Bytes, Desentrelaçador Forney (I=12, M=17),
    //     Descrambler ISDB-T e Decodificador Reed-Solomon RS(204, 188, t=8)
    // Conforme gr-isdbt (byte_deinterleaver_impl.cc + energy_descrambler_impl.cc):
    // Cada bloco de 204 bytes pos-Viterbi possui 203 bytes embaralhados seguidos de 0x47 na posicao 203,
    // de modo que o byte 0 (pos-0x47) entra no ramo 0 do Forney (atraso 187) e o byte 203 (0x47) entra no ramo 11 (atraso 0).
    while (viterbi_byte_stream_.size() >= 204) {
        if (viterbi_sync_offset_ < 0) {
            size_t sync_win_pkts = (time_interleave_I_ == 0) ? 2 : 8;
            int min_hits_req     = (time_interleave_I_ == 0) ? 2 : 5;
            size_t req_bytes     = sync_win_pkts * 204;

            if (viterbi_byte_stream_.size() < req_bytes) break;

            int best_off = -1;
            int best_hits = 0;
            for (int off = 0; off < 204; ++off) {
                int hits = 0;
                for (size_t k = 0; k < sync_win_pkts; ++k) {
                    if (viterbi_byte_stream_[off + k * 204 + 203] == 0x47) {
                        hits++;
                    }
                }
                if (hits > best_hits) {
                    best_hits = hits;
                    best_off = off;
                }
            }

            if (best_hits >= min_hits_req) {
                viterbi_sync_offset_ = best_off;
                sync_miss_count_ = 0;
            } else {
                size_t drop_bytes = (sync_win_pkts > 2) ? ((sync_win_pkts - 2) * 204) : 204;
                viterbi_byte_stream_.erase(viterbi_byte_stream_.begin(), viterbi_byte_stream_.begin() + drop_bytes);
                break;
            }
        }

        if (viterbi_sync_offset_ > 0) {
            if (viterbi_byte_stream_.size() < static_cast<size_t>(viterbi_sync_offset_)) break;
            viterbi_byte_stream_.erase(viterbi_byte_stream_.begin(), viterbi_byte_stream_.begin() + viterbi_sync_offset_);
            viterbi_sync_offset_ = 0;
        }

        if (viterbi_byte_stream_.size() < 204) break;

        // Monitora continuidade do sync byte 0x47 na posicao 203 com histerese (ate 16 pacotes)
        // para nunca deslocar os 187 pacotes acumulados no Forney por um erro de bit isolado
        if (viterbi_byte_stream_[203] == 0x47) {
            sync_miss_count_ = 0;
        } else {
            if (++sync_miss_count_ >= 16) {
                viterbi_sync_offset_ = -1;
                sync_miss_count_ = 0;
                forney_blocks_fed_ = 0;
                packet_idx_in_frame_ = -1;
                for (size_t j = 0; j < 12; ++j) {
                    forney_fifos_[j].assign(17 * (11 - j), 0);
                }
                viterbi_byte_stream_.erase(viterbi_byte_stream_.begin(), viterbi_byte_stream_.begin() + 204);
                continue;
            }
        }

        // Passa os 204 bytes alinhados pelo Desentrelaçador Convolucional Forney (ramos 0..11)
        uint8_t deint_block[204];
        for (size_t k = 0; k < 204; ++k) {
            size_t branch = k % 12;
            uint8_t in_b = viterbi_byte_stream_[k];
            if (forney_fifos_[branch].empty()) {
                deint_block[k] = in_b;
            } else {
                forney_fifos_[branch].push_back(in_b);
                deint_block[k] = forney_fifos_[branch].front();
                forney_fifos_[branch].pop_front();
            }
        }
        viterbi_byte_stream_.erase(viterbi_byte_stream_.begin(), viterbi_byte_stream_.begin() + 204);
        forney_blocks_fed_++;

        // Aguarda o preenchimento das 187 linhas de atraso do Forney (11 * 17 = 187 pacotes)
        if (time_interleave_I_ > 0 && forney_blocks_fed_ <= 187) {
            continue;
        }

        // Calcula o numero de TSPs por quadro OFDM (48 para QPSK 1/2, 64 para QPSK 2/3, 72 para QPSK 3/4)
        int tsp_per_frame = 48;
        if (code_rate_ == CodeRate::RATE_2_3) tsp_per_frame = 64;
        else if (code_rate_ == CodeRate::RATE_3_4) tsp_per_frame = 72;
        else if (code_rate_ == CodeRate::RATE_5_6) tsp_per_frame = 80;
        else if (code_rate_ == CodeRate::RATE_7_8) tsp_per_frame = 84;
        if (modulation_ == CarrierModulation::QAM16) tsp_per_frame *= 2;

        // Descrambla e decodifica com Reed-Solomon RS(204, 188, t=8)
        int decoded_errs = -1;
        uint8_t valid_ts[204];

        auto try_slot = [&](int slot) -> int {
            uint8_t cand[204];
            cand[0] = 0x47; // Restaura sync byte 0x47 no inicio do pacote RS
            const auto& mask = prbs_frame_masks_[slot % 128];
            for (int i = 0; i < 203; ++i) {
                cand[i + 1] = deint_block[i] ^ mask[i];
            }
            int errs = rs_.decode(cand);
            if (errs >= 0) {
                cand[0] = 0x47;
                std::memcpy(valid_ts, cand, 204);
            }
            return errs;
        };

        if (packet_idx_in_frame_ >= 0) {
            decoded_errs = try_slot(packet_idx_in_frame_);
            if (decoded_errs >= 0) {
                packet_idx_in_frame_ = (packet_idx_in_frame_ + 1) % tsp_per_frame;
            }
        }

        // Se ainda nao travou na fase do quadro PRBS, busca entre os slots possiveis do quadro OFDM
        if (decoded_errs < 0) {
            for (int s = 0; s < tsp_per_frame; ++s) {
                int errs = try_slot(s);
                if (errs >= 0) {
                    decoded_errs = errs;
                    packet_idx_in_frame_ = (s + 1) % tsp_per_frame;
                    break;
                }
            }
        }

        if (decoded_errs >= 0) {
            rs_corrected_ += static_cast<uint64_t>(decoded_errs);
            total_ts_packets_++;
            out_ts_packets.emplace_back(valid_ts, valid_ts + 188);
        }
    }

    symbol_count_++;
    return out_ts_packets.size() - pkts_before;
}

} // namespace isdbt
