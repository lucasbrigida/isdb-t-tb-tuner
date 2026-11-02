#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace isdbt {

/**
 * @brief Decodificador Reed-Solomon (204, 188, t=8) para ISDB-T / DVB
 * 
 * Campo de Galois GF(2^8) com polinomio primitivo:
 * p(x) = x^8 + x^4 + x^3 + x^2 + 1 (0x11D / 285)
 * Polinomio gerador: g(x) = prod_{i=0}^{15} (x - alpha^i)
 * Corrige ate t=8 bytes corrompidos por pacote de 204 bytes.
 */
class ReedSolomon {
public:
    ReedSolomon();

    /**
     * @brief Decodifica um bloco de 204 bytes (188 bytes de dados + 16 bytes de paridade).
     * @param block204 Ponteiro para o buffer de 204 bytes (modificado in-place se corrigido).
     * @return int Numero de erros corrigidos (0 a 8), ou -1 se o erro for incorrigivel.
     */
    int decode(uint8_t* block204);

    /**
     * @brief Codifica 188 bytes gerando os 16 bytes de paridade RS (totalizando 204 bytes).
     * Utilizado para testes unitarios e validacao de vetores.
     */
    void encode(const uint8_t* data188, uint8_t* parity16);

private:
    void init_galois_field();
    void init_generator_poly();

    inline uint8_t gf_add(uint8_t a, uint8_t b) const noexcept { return a ^ b; }
    inline uint8_t gf_mul(uint8_t a, uint8_t b) const noexcept { return gf_mul_table_[a][b]; }
    uint8_t gf_div(uint8_t a, uint8_t b) const noexcept;
    inline uint8_t gf_exp(uint8_t power) const noexcept { return exp_table_[power]; }

    alignas(64) uint8_t gf_mul_table_[256][256]; // Tabela direta 64KB de multiplicacao GF(2^8)
    uint8_t exp_table_[512]; // Tabela exponencial (duplicada para evitar modulo)
    uint8_t log_table_[256]; // Tabela de logaritmos
    uint8_t gen_poly_[17];   // Coeficientes de g(x)
};

} // namespace isdbt
