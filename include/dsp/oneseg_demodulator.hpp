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
 * @brief Esquemas de modulacao das portadoras de dados ISDB-T (Camada A / 1-Seg).
 */
enum class CarrierModulation {
    DQPSK = 0,
    QPSK  = 1,
    QAM16 = 2,
    QAM64 = 3
};

/**
 * @brief Demodulador completo de Camada Fisica ISDB-T/ISDB-Tb (1-Seg / Segmento Central)
 * 
 * Implementa a cadeia DSP 100% condizente com git-artes/gr-isdbt e ABNT NBR 15601 / ARIB STD-B31:
 * 1. Extracao das subportadoras do Segmento 0 com estimativa e correcao de IFO (Integer Frequency Offset).
 * 2. Estimacao de Canal e Equalizacao linear baseada nos Pilotos Dispersos (SP) e Piloto Continuo (CP)
 *    usando o polinomio PRBS X^11 + X^2 + 1 (ofdm_synchronization_impl.cc).
 * 3. Decodificador TMCC DBPSK nas 4 portadoras dedicadas com verificacao BCH(273, 191),
 *    detectando inicio de quadro OFDM (204 simbolos) e parametros da Camada A (modulacao, taxa Viterbi, I).
 * 4. Desentrelaçamento em Frequencia (tabela de permutacao de 384 portadoras).
 * 5. Desentrelaçamento no Tempo (banco de atrasos FIFO com profundidade configuravel I=4/8/16).
 * 6. Demapeamento QPSK / 16QAM com desentrelaçador de bits.
 * 7. Decodificador Convolucional Viterbi continuo (K=7, POLYA=0x4F, POLYB=0x6D, taxas 1/2, 2/3, 3/4, 5/6, 7/8).
 * 8. Sincronismo de byte 0x47 e Desentrelaçador de Bytes Convolucional Forney (I=12, M=17).
 * 9. Descrambler de Dispersao de Energia ISDB-T (reset por quadro OFDM, 8 clocks extras no sync).
 * 10. Decodificador Reed-Solomon RS(204, 188, t=8) gerando pacotes MPEG-TS de 188 bytes.
 */
class OneSegDemodulator {
public:
    OneSegDemodulator(OfdmMode mode = OfdmMode::MODE_1_2K, GuardInterval gi = GuardInterval::GI_1_16);
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
     * @brief Altera manualmente a taxa de codigo ou profundidade de tempo se necessario.
     */
    void set_parameters(CarrierModulation mod, CodeRate rate, int time_interleave_I);

    /**
     * @brief Reinicia o estado de sincronismo e buffers do demodulador.
     */
    void reset();

    /**
     * @brief Retorna metricas operacionais da demodulacao.
     */
    uint64_t total_symbols_processed() const { return symbol_count_; }
    uint64_t total_ts_packets() const { return total_ts_packets_; }
    uint64_t rs_errors_corrected() const { return rs_corrected_; }
    bool tmcc_locked() const { return tmcc_locked_; }
    bool is_tmcc_locked() const { return tmcc_locked_; }
    int integer_freq_offset_bins() const { return ifo_bins_; }
    int get_ifo() const { return ifo_bins_; }

private:
    void init_pilots();
    void init_time_deinterleaver(int I);
    void init_prbs_frame_masks();
    bool process_tmcc(const std::complex<float>* eq_seg_carriers);
    bool check_tmcc_bch_parity() const;
    void decode_tmcc_parameters();

    OfdmMode mode_;
    GuardInterval gi_;
    int fft_size_{2048};
    uint64_t symbol_count_{0};
    uint64_t total_ts_packets_{0};
    uint64_t rs_corrected_{0};

    // Parametros ativos da Camada A (1-Seg)
    CarrierModulation modulation_{CarrierModulation::QPSK};
    CodeRate code_rate_{CodeRate::RATE_1_2};
    int time_interleave_I_{4}; // Padrao Modo 3 1-Seg: I=4 (equivale a I=16 em Modo 1)

    // Rastreamento de IFO (Integer Frequency Offset) e fase de simbolo SP (0..3)
    int ifo_bins_{0};
    int ifo_agree_count_{0};
    bool ifo_locked_{false};
    size_t sym_mod4_{0};
    int sym_phase_agree_count_{0};
    bool sym_phase_locked_{false};

    // Estado do Decodificador TMCC
    std::array<std::complex<float>, 4> prev_tmcc_{};
    std::deque<uint8_t> tmcc_bits_;
    int since_last_tmcc_{203};
    bool tmcc_locked_{false};
    bool frame_begin_pending_{false};

    // Modulos DSP
    OfdmSync ofdm_sync_;
    ViterbiDecoder viterbi_;
    Descrambler descrambler_;
    ReedSolomon rs_;

    // Buffers internos
    std::vector<std::complex<float>> fft_carriers_;
    std::vector<float> full_prbs_;   // 5617 valores ±4/3 de todo o canal Modo 3
    std::vector<float> prbs_pilots_; // 433 valores ±4/3 do Segmento 0 (+1 borda direita)

    // Desentrelaçamento no tempo (384 FIFOs)
    std::vector<std::deque<std::complex<float>>> time_deint_fifos_;

    // Desentrelaçamento de bits (QPSK: 2 ramos [0, 120]; 16QAM: 4 ramos [0, 40, 80, 120])
    std::array<std::deque<float>, 4> bit_deint_fifos_;

    // Sincronizador pre-Forney e Desentrelaçador de bytes Forney (12 ramos, retardo j * 17)
    std::vector<uint8_t> viterbi_byte_stream_;
    int viterbi_sync_offset_{-1};
    int sync_miss_count_{0};
    uint64_t forney_blocks_fed_{0};
    std::vector<std::deque<uint8_t>> forney_fifos_;

    // Mascaras PRBS pre-calculadas para os pacotes de um quadro OFDM (ate 128 pacotes/quadro)
    std::vector<std::array<uint8_t, 203>> prbs_frame_masks_;
    int packet_idx_in_frame_{-1};
};

} // namespace isdbt
