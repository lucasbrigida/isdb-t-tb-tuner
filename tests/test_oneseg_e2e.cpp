#include <iostream>
#include <vector>
#include <complex>
#include <cstring>
#include <cassert>
#include <cmath>
#include "dsp/ofdm_sync.hpp"
#include "dsp/oneseg_demodulator.hpp"
#include "dsp/reed_solomon.hpp"
#include "dsp/descrambler.hpp"
#include "dsp/viterbi_decoder.hpp"

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

int main() {
    std::cout << "[TEST] Testando Pipeline Completo 1-Seg (OfdmSync + OneSegDemodulator) compativel com gr-isdbt...\n";

    isdbt::ReedSolomon rs;
    isdbt::Descrambler tx_desc;

    isdbt::OneSegDemodulator demod(isdbt::OfdmMode::MODE_1_2K, isdbt::GuardInterval::GI_1_16);
    demod.set_parameters(isdbt::CarrierModulation::QPSK, isdbt::CodeRate::RATE_1_2, 0);

    const int NUM_PKTS = 48;
    std::vector<std::vector<uint8_t>> orig_ts(NUM_PKTS, std::vector<uint8_t>(188));
    for (int p = 0; p < NUM_PKTS; ++p) {
        orig_ts[p][0] = 0x47;
        orig_ts[p][1] = 0x01;
        orig_ts[p][2] = 0x00;
        orig_ts[p][3] = static_cast<uint8_t>(0x10 | (p & 0x0F));
        for (int i = 4; i < 188; ++i) {
            orig_ts[p][i] = static_cast<uint8_t>((p * 37 + i * 13) & 0xFF);
        }
    }

    // Forney Interleaver no transmissor: ramo j (0..11) tem retardo 17 * j bytes
    std::vector<std::deque<uint8_t>> tx_forney(12);
    for (int j = 0; j < 12; ++j) {
        tx_forney[j].assign(17 * j, 0);
    }

    std::vector<uint8_t> forney_stream;
    tx_desc.reset();
    for (int p = 0; p < NUM_PKTS; ++p) {
        uint8_t parity[16];
        rs.encode(orig_ts[p].data(), parity);
        uint8_t tx_pkt[204];
        std::memcpy(tx_pkt, orig_ts[p].data() + 1, 187);
        std::memcpy(tx_pkt + 187, parity, 16);
        tx_pkt[203] = 0x47;

        if (p == 0) tx_desc.reset();
        for (int i = 0; i < 203; ++i) {
            tx_pkt[i] ^= tx_desc.clock_byte();
        }
        tx_desc.clock_byte(); // clock extra no sync 0x47

        for (int i = 0; i < 204; ++i) {
            int j = i % 12;
            tx_forney[j].push_back(tx_pkt[i]);
            uint8_t out_b = tx_forney[j].front();
            tx_forney[j].pop_front();
            forney_stream.push_back(out_b);
        }
    }

    auto popcnt8 = [](uint8_t x) {
        int c = 0;
        while (x) { c += (x & 1); x >>= 1; }
        return c & 1;
    };

    uint8_t conv_state = 0;
    std::vector<uint8_t> coded_bits;
    for (uint8_t byte_val : forney_stream) {
        for (int b_idx = 7; b_idx >= 0; --b_idx) {
            uint8_t bit = (byte_val >> b_idx) & 1;
            uint8_t reg7 = static_cast<uint8_t>((conv_state << 1) | bit);
            uint8_t g1 = static_cast<uint8_t>(popcnt8(reg7 & 0x4F));
            uint8_t g2 = static_cast<uint8_t>(popcnt8(reg7 & 0x6D));
            coded_bits.push_back(g1);
            coded_bits.push_back(g2);
            conv_state = static_cast<uint8_t>(reg7 & 0x3F);
        }
    }

    // Bit Interleaver QPSK TX: b0 (MSB) atraso 0, b1 (LSB) atraso 120
    std::deque<uint8_t> tx_bit_fifo1(120, 0);
    std::vector<std::complex<float>> qpsk_syms;
    const float inv_sqrt2 = 0.70710678f;
    for (size_t i = 0; i + 1 < coded_bits.size(); i += 2) {
        uint8_t b0 = coded_bits[i];
        tx_bit_fifo1.push_back(coded_bits[i + 1]);
        uint8_t b1 = tx_bit_fifo1.front();
        tx_bit_fifo1.pop_front();
        float re = (b0 == 0) ? inv_sqrt2 : -inv_sqrt2;
        float im = (b1 == 0) ? inv_sqrt2 : -inv_sqrt2;
        qpsk_syms.emplace_back(re, im);
    }

    // PRBS do Segmento 0 (indice 2592 no canal Modo 3)
    std::vector<float> seg0_prbs(433);
    uint16_t reg = 0x7FF;
    for (int k = 0; k <= 2592 + 432; ++k) {
        uint8_t bit = static_cast<uint8_t>(reg & 1);
        uint8_t fb = static_cast<uint8_t>(((reg >> 2) ^ (reg >> 0)) & 1);
        reg = static_cast<uint16_t>((reg >> 1) | (fb << 10));
        if (k >= 2592) {
            seg0_prbs[k - 2592] = bit ? (-4.0f / 3.0f) : (4.0f / 3.0f);
        }
    }

    auto is_tmcc_or_ac = [](int c) {
        return (c == 7 || c == 89 || c == 101 || c == 131 ||
                c == 206 || c == 209 || c == 226 || c == 244 ||
                c == 286 || c == 349 || c == 377 || c == 407);
    };

    const int IFO_SHIFT = 2; // Simula erro de frequencia inteiro de +2 subportadoras (~2 kHz)
    size_t num_ofdm_syms = qpsk_syms.size() / 384;

    fftwf_complex* ifft_in = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * 2048);
    fftwf_complex* ifft_out = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * 2048);
    fftwf_plan ifft_plan = fftwf_plan_dft_1d(2048, ifft_in, ifft_out, FFTW_BACKWARD, FFTW_ESTIMATE);

    std::vector<std::vector<uint8_t>> rx_packets;
    for (size_t s = 0; s < num_ofdm_syms; ++s) {
        size_t sym_mod4 = s % 4;
        std::vector<std::complex<float>> raw_data(384);
        for (int k = 0; k < 384; ++k) {
            raw_data[FREQ_PERM_MODE3[k]] = qpsk_syms[s * 384 + k];
        }

        std::vector<std::complex<float>> spec(2048, std::complex<float>(0.0f, 0.0f));
        int start_bin = (2048 / 2) - (432 / 2) + IFO_SHIFT;
        int d_idx = 0;
        for (int c = 0; c < 432; ++c) {
            if (static_cast<size_t>(c % 12) == sym_mod4 * 3) {
                spec[start_bin + c] = std::complex<float>(seg0_prbs[c], 0.0f);
            } else if (is_tmcc_or_ac(c)) {
                spec[start_bin + c] = std::complex<float>(seg0_prbs[c], 0.0f);
            } else {
                if (d_idx < 384) spec[start_bin + c] = raw_data[d_idx++];
            }
        }
        spec[start_bin + 432] = std::complex<float>(seg0_prbs[432], 0.0f);

        float norm = 1.0f / std::sqrt(2048.0f);
        for (int i = 0; i < 1024; ++i) {
            ifft_in[i][0] = spec[i + 1024].real() * norm;
            ifft_in[i][1] = spec[i + 1024].imag() * norm;
            ifft_in[i + 1024][0] = spec[i].real() * norm;
            ifft_in[i + 1024][1] = spec[i].imag() * norm;
        }
        fftwf_execute(ifft_plan);

        std::vector<std::complex<float>> time_sym(2048);
        for (int i = 0; i < 2048; ++i) {
            time_sym[i] = std::complex<float>(ifft_out[i][0], ifft_out[i][1]);
        }

        demod.process_symbol(time_sym.data(), rx_packets);
    }

    fftwf_destroy_plan(ifft_plan);
    fftwf_free(ifft_in);
    fftwf_free(ifft_out);

    std::cout << "  - Simbolos OFDM processados: " << demod.total_symbols_processed() << "\n";
    std::cout << "  - IFO detectado: " << demod.get_ifo() << " bins (esperado: " << IFO_SHIFT << ")\n";
    std::cout << "  - Pacotes MPEG-TS recuperados: " << rx_packets.size() << "\n";
    assert(demod.get_ifo() == IFO_SHIFT);
    assert(!rx_packets.empty());

    int exact_matches = 0;
    for (const auto& pkt : rx_packets) {
        for (int p = 0; p < NUM_PKTS; ++p) {
            if (std::memcmp(pkt.data(), orig_ts[p].data(), 188) == 0) {
                exact_matches++;
                break;
            }
        }
    }
    std::cout << "  - Pacotes 188B identicos byte-a-byte aos originais TX: " << exact_matches << " / " << rx_packets.size() << "\n";
    assert(exact_matches == static_cast<int>(rx_packets.size()));
    std::cout << "[SUCESSO] Demodulador 1-Seg completou a recuperacao 100% exata de pacotes MPEG-TS com IFO=" << IFO_SHIFT << "!\n";
    return 0;
}

