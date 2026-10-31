#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace isdbt {

/**
 * @brief Taxas de codigo suportadas pelo ISDB-T atraves do puncionamento do codigo base 1/2.
 */
enum class CodeRate {
    RATE_1_2, ///< Taxa 1/2: sem puncionamento (transmite X1, Y1) - Mais robusta, padrao em 1-Seg
    RATE_2_3, ///< Taxa 2/3: punciona X2 (transmite X1, Y1, Y2)
    RATE_3_4, ///< Taxa 3/4: punciona X2, Y3 (transmite X1, Y1, Y2, X3)
    RATE_5_6, ///< Taxa 5/6: matriz de puncionamento padrao DVB/ISDB-T
    RATE_7_8  ///< Taxa 7/8: maior taxa de dados, menor redundancia
};

/**
 * @brief Decodificador Viterbi Soft-Decision para ISDB-T (K=7, G1=171o, G2=133o)
 * 
 * Implementa a decodificacao de Maxima Verossimilhanca (Maximum Likelihood Sequence Estimation)
 * sobre a Trellis de 64 estados gerada pelo codigo convolucional de restricao K=7:
 *   G1 = 171 octal = 0b1111001 (x^6 + x^5 + x^4 + x^3 + 1)
 *   G2 = 133 octal = 0b1011011 (x^6 + x^4 + x^3 + x + 1)
 * 
 * Aceita entradas soft quantizadas [0..255] onde 128 representa simbolo puncionado/apagamento.
 */
class ViterbiDecoder {
public:
    static constexpr int NUM_STATES = 64;      ///< 2^(K-1) para K=7
    static constexpr int TRACEBACK_DEPTH = 96; ///< Profundidade de traceback (~14 vezes o comprimento de restricao)

    /**
     * @brief Construtor do decodificador Viterbi.
     * @param rate Taxa de codigo inicial (padrao RATE_1_2).
     */
    explicit ViterbiDecoder(CodeRate rate = CodeRate::RATE_1_2);

    /**
     * @brief Define a taxa de codigo e a regra de despuncionamento associada.
     */
    void set_code_rate(CodeRate rate);
    CodeRate code_rate() const { return rate_; }

    /**
     * @brief Reseta as metricas de estado acumuladas na Trellis.
     */
    void reset();

    /**
     * @brief Codifica bits brutos para geracao de vetores de teste e simulacao de transmissao.
     * @param in_bits Bits de entrada [0, 1].
     * @param out_symbols Simbolos codificados e puncionados de saida [0, 1].
     */
    void encode(const std::vector<uint8_t>& in_bits, std::vector<uint8_t>& out_symbols);

    /**
     * @brief Decodifica uma sequencia de simbolos soft-decision para bytes de dados.
     * @param soft_in Metricas soft de entrada (0 = bit 0 seguro, 255 = bit 1 seguro, 128 = puncionado).
     * @param out_bytes Bytes de dados decodificados (MSB first).
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

