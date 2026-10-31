#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace isdbt {

enum class CodeRate {
    RATE_1_2,
    RATE_2_3,
    RATE_3_4,
    RATE_5_6,
    RATE_7_8
};

/**
 * @brief Decodificador Viterbi Soft-Decision para ISDB-T (K=7, G1=171o, G2=133o)
 * 
 * Suporta taxas de codigo puncionadas padrao ISDB-T/DVB: 1/2, 2/3, 3/4, 5/6, 7/8.
 * Entradas: soft-decision (0 = bit 0 com certeza maxima, 255 = bit 1 com certeza maxima, 128 = puncionado/apagamento).
 */
class ViterbiDecoder {
public:
    static constexpr int NUM_STATES = 64; // 2^(K-1) para K=7
    static constexpr int TRACEBACK_DEPTH = 96; // Profundidade de traceback (~5 a 10 vezes o comprimento de restricao)

    explicit ViterbiDecoder(CodeRate rate = CodeRate::RATE_1_2);

    void set_code_rate(CodeRate rate);
    CodeRate code_rate() const { return rate_; }

    void reset();

    /**
     * @brief Codifica bits brutos (para testes unitarios deterministas e geracao de vetores).
     * @param in_bits Bits de entrada (0 ou 1)
     * @param out_symbols Simbolos codificados e puncionados de saida (0 ou 1)
     */
    void encode(const std::vector<uint8_t>& in_bits, std::vector<uint8_t>& out_symbols);

    /**
     * @brief Decodifica simbolos soft (valores 0..255).
     * @param soft_in Vetor de soft metrics de entrada
     * @param out_bytes Vetor de bytes decodificados
     */
    void decode(const std::vector<uint8_t>& soft_in, std::vector<uint8_t>& out_bytes);

private:
    void init_trellis();
    void depuncture(const std::vector<uint8_t>& soft_in, std::vector<uint8_t>& unpunctured);

    CodeRate rate_;

    // Tabelas de transicao da Trellis (64 estados)
    uint8_t next_state_[NUM_STATES][2];
    uint8_t prev_state_[NUM_STATES][2];
    uint8_t output_parity_[NUM_STATES][2]; // 2 bits de paridade gerados por transicao: G1 e G2
};

} // namespace isdbt

