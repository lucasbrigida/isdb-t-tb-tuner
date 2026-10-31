#pragma once

#include <complex>
#include <vector>
#include <deque>
#include <array>
#include <cstdint>
#include "dsp/ofdm_sync.hpp"
#include "dsp/viterbi_decoder.hpp"
#include "dsp/descrambler.hpp"
#include "dsp/reed_solomon.hpp"

namespace isdbt {

/**
 * @brief Demodulador completo de Camada Fisica ISDB-T/ISDB-Tb (1-Seg / Segmento Central)
 * 
 * Implementa a cadeia DSP conforme ABNT NBR 15601 / ARIB STD-B31:
 * 1. Extracao das 432 subportadoras ativas do Segmento 0 (central) em Modo 3.
 * 2. Estimacao de Canal e Equalizacao baseada nos Pilotos Dispersos (SP).
 * 3. Desentrelaçamento em Frequencia (tabela de permutacao de 384 portadoras).
 * 4. Desentrelaçamento no Tempo (banco de atrasos FIFO com profundidade I=4).
 * 5. Demapeamento QPSK suave (LLR) com desentrelaçador de bits.
 * 6. Decodificador Convolucional Viterbi (Inner FEC, K=7, 171/133 octal).
 * 7. Desentrelaçador de Bytes Convolucional Forney (I=12, M=17).
 * 8. Descrambler de Dispersao de Energia PRBS (LFSR de 15 bits).
 * 9. Decodificador Reed-Solomon RS(204, 188, t=8) gerando pacotes MPEG-TS validos.
 */
class OneSegDemodulator {
public:
    OneSegDemodulator(OfdmMode mode = OfdmMode::MODE_3_8K, GuardInterval gi = GuardInterval::GI_1_16);
    ~OneSegDemodulator() = default;

    /**
     * @brief Processa um simbolo OFDM completo no dominio do tempo.
     * @param symbol_time_domain Vetor de fft_size_ amostras I/Q complexas (2048 amostras).
     * @param out_ts_packets Vetor que recebe os pacotes MPEG-TS (188 bytes) decodificados.
     * @return Quantidade de pacotes MPEG-TS 188B gerados neste simbolo.
     */
    size_t process_symbol(const std::complex<float>* symbol_time_domain,
                          std::vector<std::vector<uint8_t>>& out_ts_packets);

    /**
     * @brief Retorna metricas operacionais da demodulacao.
     */
    uint64_t total_symbols_processed() const { return symbol_count_; }
    uint64_t total_ts_packets() const { return total_ts_packets_; }
    uint64_t rs_errors_corrected() const { return rs_corrected_; }

private:
    void init_pilots();

    OfdmMode mode_;
    GuardInterval gi_;
    int fft_size_{2048};
    uint64_t symbol_count_{0};
    uint64_t total_ts_packets_{0};
    uint64_t rs_corrected_{0};

    // Modulos DSP
    OfdmSync ofdm_sync_;
    ViterbiDecoder viterbi_;
    Descrambler descrambler_;
    ReedSolomon rs_;

    // Buffers internos
    std::vector<std::complex<float>> fft_carriers_;
    std::vector<float> prbs_pilots_; // 432 valores ±4/3

    // Desentrelaçamento no tempo (384 FIFOs)
    std::vector<std::deque<std::complex<float>>> time_deint_fifos_;

    // Desentrelaçamento de bits (delay de 120 para MSB)
    std::deque<float> bit_deint_msb_fifo_;

    // Desentrelaçador de bytes Forney (12 ramos, retardo j * 17)
    std::vector<std::deque<uint8_t>> forney_fifos_;
    size_t forney_idx_{0};

    // Buffer de sincronizacao de pacotes TS (204 bytes)
    std::vector<uint8_t> byte_stream_;
    int sync_offset_{-1};
};

} // namespace isdbt

