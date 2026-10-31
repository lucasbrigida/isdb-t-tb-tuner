#include "dsp/oneseg_demodulator.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace isdbt {

// Tabela de Permutacao em Frequencia para Modo 3 (384 portadoras de dados)
// Conforme ABNT NBR 15601 / ARIB STD-B31
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

void OneSegDemodulator::init_pilots() {
    // Gerador de PRBS de 11 bits (X^11 + X^9 + 1) conforme ABNT NBR 15601 / ARIB STD-B31
    uint32_t reg = 0x7FF; // 11 bits em '1'
    std::vector<float> full_prbs(2592 + 432);
    for (size_t i = 0; i < full_prbs.size(); ++i) {
        uint32_t w = reg & 1;
        uint32_t new_bit = ((reg >> 9) ^ reg) & 1;
        reg = (reg >> 1) | (new_bit << 10);
        full_prbs[i] = (4.0f * 2.0f * (0.5f - static_cast<float>(w))) / 3.0f;
    }
    prbs_pilots_.assign(full_prbs.begin() + 2592, full_prbs.begin() + 2592 + 432);
}

OneSegDemodulator::OneSegDemodulator(OfdmMode mode, GuardInterval gi)
    : mode_(mode), gi_(gi), fft_size_(static_cast<int>(mode)),
      ofdm_sync_(mode, gi), viterbi_(CodeRate::RATE_1_2) {
    init_pilots();
    fft_carriers_.resize(fft_size_);

    // Inicializa desentrelaçador de tempo (I = 4)
    // 384 portadoras de dados com retardo 4 * (95 - (5c % 96))
    time_deint_fifos_.resize(384);
    for (size_t c = 0; c < 384; ++c) {
        size_t mi = (5 * c) % 96;
        size_t delay = 4 * (95 - mi);
        time_deint_fifos_[c].assign(delay, std::complex<float>(0.0f, 0.0f));
    }

    // Desentrelaçador de bits MSB (delay 120)
    bit_deint_msb_fifo_.assign(120, 0.0f);

    // Desentrelaçador de bytes Forney (I=12, M=17)
    // Ramo j (0..11) tem retardo 17 * (11 - j)
    forney_fifos_.resize(12);
    for (size_t j = 0; j < 12; ++j) {
        size_t delay = 17 * (11 - j);
        forney_fifos_[j].assign(delay, 0);
    }
}

size_t OneSegDemodulator::process_symbol(const std::complex<float>* symbol_time_domain,
                                        std::vector<std::vector<uint8_t>>& out_ts_packets) {
    if (!symbol_time_domain) return 0;
    size_t pkts_before = out_ts_packets.size();

    // 1. Processamento da FFT de 2048 pontos e fftshift centralizado
    ofdm_sync_.process_symbol_fft(symbol_time_domain, fft_carriers_.data());

    // 2. Extracao do Segmento 0 (central) de 432 portadoras
    // Em Modo 3 (2048 pontos), o segmento central inicia no bin (2048 - 432) / 2 = 808
    const size_t SEG_OFFSET = (fft_size_ - 432) / 2;
    std::vector<std::complex<float>> seg_carriers(432);
    for (size_t i = 0; i < 432; ++i) {
        seg_carriers[i] = fft_carriers_[SEG_OFFSET + i];
    }

    // 3. Estimacao de canal nos Pilotos Dispersos (SP)
    // Deteccao dinamica da fase do simbolo (0..3) medindo a coerencia entre pilotos SP adjacentes
    size_t sym_mod4 = 0;
    float max_coh = -1.0f;
    for (size_t p = 0; p < 4; ++p) {
        std::complex<float> num(0.0f, 0.0f);
        float den = 0.0f;
        std::complex<float> prev(0.0f, 0.0f);
        bool has_prev = false;
        for (size_t l = 3 * p; l < 432; l += 12) {
            std::complex<float> h_val = seg_carriers[l] / prbs_pilots_[l];
            if (has_prev) {
                num += h_val * std::conj(prev);
                den += std::norm(h_val) * std::norm(prev);
            }
            prev = h_val;
            has_prev = true;
        }
        float coh = (den > 1e-12f) ? (std::abs(num) / std::sqrt(den)) : 0.0f;
        if (coh > max_coh) {
            max_coh = coh;
            sym_mod4 = p;
        }
    }

    std::vector<std::complex<float>> h(432, std::complex<float>(1.0f, 0.0f));
    std::vector<size_t> sp_indices;

    for (size_t l = 3 * sym_mod4; l < 432; l += 12) {
        sp_indices.push_back(l);
        // Canal estimado na portadora piloto
        h[l] = seg_carriers[l] / prbs_pilots_[l];
    }

    // Interpolacao linear entre SPs adjacentes
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

    // Extrapolacao para as bordas
    if (!sp_indices.empty()) {
        size_t first_sp = sp_indices.front();
        for (size_t l = 0; l < first_sp; ++l) h[l] = h[first_sp];
        size_t last_sp = sp_indices.back();
        for (size_t l = last_sp + 1; l < 432; ++l) h[l] = h[last_sp];
    }

    // 4. Equalizacao e extracao das 384 portadoras de dados
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
            // Equalizacao Zero-Forcing com regularizacao
            float h_mag2 = std::norm(h[l]) + 1e-4f;
            std::complex<float> eq_sym = (seg_carriers[l] * std::conj(h[l])) / h_mag2;
            data_carriers.push_back(eq_sym);
        }
    }

    if (data_carriers.size() != 384) return 0;

    // 5. Desentrelaçamento em Frequencia (Permutacao de 384 portadoras)
    std::vector<std::complex<float>> freq_deint(384);
    for (size_t i = 0; i < 384; ++i) {
        freq_deint[i] = data_carriers[FREQ_PERM_MODE3[i]];
    }

    // 6. Desentrelaçamento no Tempo (FIFOs de profundidade I=4)
    std::vector<std::complex<float>> time_deint(384);
    for (size_t c = 0; c < 384; ++c) {
        time_deint_fifos_[c].push_back(freq_deint[c]);
        time_deint[c] = time_deint_fifos_[c].front();
        time_deint_fifos_[c].pop_front();
    }

    // 7. Demapeamento QPSK suave com desentrelaçamento de bits
    // QPSK: LSB = -imag, MSB = -real (MSB tem retardo de 120 portadoras)
    std::vector<uint8_t> soft_bits;
    soft_bits.reserve(384 * 2);

    for (size_t c = 0; c < 384; ++c) {
        float lsb_raw = std::clamp(-time_deint[c].imag(), -8.0f, 8.0f);
        float msb_raw = std::clamp(-time_deint[c].real(), -8.0f, 8.0f);

        bit_deint_msb_fifo_.push_back(msb_raw);
        float msb_deint = bit_deint_msb_fifo_.front();
        bit_deint_msb_fifo_.pop_front();

        // Converte soft LLR para uint8_t [0..255] (0 = bit 0 forte, 255 = bit 1 forte)
        uint8_t lsb_byte = static_cast<uint8_t>(std::clamp(static_cast<int>(128.0f + lsb_raw * 16.0f), 0, 255));
        uint8_t msb_byte = static_cast<uint8_t>(std::clamp(static_cast<int>(128.0f + msb_deint * 16.0f), 0, 255));

        soft_bits.push_back(msb_byte);
        soft_bits.push_back(lsb_byte);
    }

    // 8. Decodificacao Convolucional Viterbi (Inner FEC)
    std::vector<uint8_t> decoded_bytes;
    viterbi_.decode(soft_bits, decoded_bytes);

    // 9. Desentrelaçador de Bytes Convolucional Forney (I=12, M=17)
    for (uint8_t b : decoded_bytes) {
        size_t branch = forney_idx_ % 12;
        forney_fifos_[branch].push_back(b);
        uint8_t out_byte = forney_fifos_[branch].front();
        forney_fifos_[branch].pop_front();
        forney_idx_++;

        byte_stream_.push_back(out_byte);
    }

    // 10. Sincronismo de Pacote TS e Decodificacao Reed-Solomon
    // Cada bloco FEC tem 204 bytes, iniciando com sync byte 0x47 ou 0xB8
    while (byte_stream_.size() >= 204) {
        // Se ainda nao estiver alinhado, procura pelo sync byte 0x47 ou 0xB8
        if (sync_offset_ < 0) {
            bool found = false;
            for (size_t i = 0; i + 204 <= byte_stream_.size(); ++i) {
                if ((byte_stream_[i] == 0x47 || byte_stream_[i] == 0xB8) &&
                    (byte_stream_[i + 204] == 0x47 || byte_stream_[i + 204] == 0xB8)) {
                    sync_offset_ = static_cast<int>(i);
                    found = true;
                    break;
                }
            }
            if (!found) {
                if (byte_stream_.size() > 408) {
                    byte_stream_.erase(byte_stream_.begin(), byte_stream_.begin() + 204);
                }
                break;
            }
        }

        // Descarta bytes anteriores ao offset de sincronismo
        if (sync_offset_ > 0) {
            byte_stream_.erase(byte_stream_.begin(), byte_stream_.begin() + sync_offset_);
            sync_offset_ = 0;
        }

        if (byte_stream_.size() < 204) break;

        // Extrai bloco de 204 bytes
        uint8_t block204[204];
        std::memcpy(block204, byte_stream_.data(), 204);
        byte_stream_.erase(byte_stream_.begin(), byte_stream_.begin() + 204);

        // Dispersao de Energia (Descrambler PRBS)
        descrambler_.process_packet(block204, 204);

        // Decodificador Reed-Solomon RS(204, 188, t=8)
        int errs = rs_.decode(block204);

        if (errs >= 0) {
            rs_corrected_ += errs;
            total_ts_packets_++;
            out_ts_packets.push_back(std::vector<uint8_t>(block204, block204 + 188));
        } else {
            // Perdeu sincronismo se falhar consecutivamente
            sync_offset_ = -1;
        }
    }

    symbol_count_++;
    return out_ts_packets.size() - pkts_before;
}

} // namespace isdbt
