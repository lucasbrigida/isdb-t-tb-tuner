#include "dsp/reed_solomon.hpp"
#include <cstring>
#include <algorithm>

namespace isdbt {

constexpr int NN = 255;
constexpr int KK = 239;
constexpr int NROOTS = 16;
constexpr int FCR = 0;
constexpr int PRIM = 1;
constexpr int IPRIM = 1;

ReedSolomon::ReedSolomon() {
    init_galois_field();
    init_generator_poly();
}

void ReedSolomon::init_galois_field() {
    // Polinomio primitivo 0x11D = x^8 + x^4 + x^3 + x^2 + 1
    int sr = 1;
    log_table_[0] = 0; // log(0) eh indefinido, mas inicializado como 0
    for (int i = 0; i < 255; ++i) {
        exp_table_[i] = static_cast<uint8_t>(sr);
        exp_table_[i + 255] = static_cast<uint8_t>(sr);
        log_table_[sr] = static_cast<uint8_t>(i);
        sr <<= 1;
        if (sr & 0x100) {
            sr ^= 0x11D;
        }
    }
    // exp_table_ duplicada ate 511
    for (int i = 255 * 2; i < 512; ++i) {
        exp_table_[i] = exp_table_[i - 255];
    }
}

uint8_t ReedSolomon::gf_mul(uint8_t a, uint8_t b) const {
    if (a == 0 || b == 0) return 0;
    return exp_table_[log_table_[a] + log_table_[b]];
}

uint8_t ReedSolomon::gf_div(uint8_t a, uint8_t b) const {
    if (a == 0) return 0;
    if (b == 0) return 0; // divisao por zero
    return exp_table_[log_table_[a] + 255 - log_table_[b]];
}

void ReedSolomon::init_generator_poly() {
    // g(x) = (x - alpha^0)(x - alpha^1)...(x - alpha^15)
    std::memset(gen_poly_, 0, sizeof(gen_poly_));
    gen_poly_[0] = 1;
    for (int i = 0; i < NROOTS; ++i) {
        uint8_t root = exp_table_[i];
        gen_poly_[i + 1] = 1;
        for (int j = i; j > 0; --j) {
            gen_poly_[j] = gen_poly_[j - 1] ^ gf_mul(gen_poly_[j], root);
        }
        gen_poly_[0] = gf_mul(gen_poly_[0], root);
    }
}

void ReedSolomon::encode(const uint8_t* data188, uint8_t* parity16) {
    uint8_t p[NROOTS] = {0};

    // ISDB-T encurta RS(255, 239) para RS(204, 188) preenchendo os primeiros 51 bytes com 0
    for (int i = 0; i < 188; ++i) {
        uint8_t feedback = data188[i] ^ p[NROOTS - 1];
        if (feedback != 0) {
            for (int j = NROOTS - 1; j > 0; --j) {
                p[j] = p[j - 1] ^ gf_mul(gen_poly_[j], feedback);
            }
            p[0] = gf_mul(gen_poly_[0], feedback);
        } else {
            for (int j = NROOTS - 1; j > 0; --j) {
                p[j] = p[j - 1];
            }
            p[0] = 0;
        }
    }

    // Paridade na ordem inversa de divisao
    for (int i = 0; i < NROOTS; ++i) {
        parity16[i] = p[NROOTS - 1 - i];
    }
}

int ReedSolomon::decode(uint8_t* block204) {
    // 1. Calculo das 16 sindromes S_0 a S_15
    // O bloco de 204 bytes corresponde a posicoes [51..254] do codigo original (255)
    uint8_t s[NROOTS];
    bool has_error = false;

    for (int i = 0; i < NROOTS; ++i) {
        uint8_t alpha_i = exp_table_[i];
        uint8_t sum = 0;
        for (int j = 0; j < 204; ++j) {
            sum = block204[j] ^ gf_mul(sum, alpha_i);
        }
        s[i] = sum;
        if (sum != 0) has_error = true;
    }

    if (!has_error) {
        return 0; // Nenhum erro detectado
    }

    // 2. Algoritmo de Berlekamp-Massey para o polinomio localizador de erros Lambda(x)
    uint8_t lambda[NROOTS + 1] = {0};
    uint8_t b[NROOTS + 1] = {0};
    lambda[0] = 1;
    b[0] = 1;

    int L = 0;
    int m = 1;
    uint8_t b_star = 1;

    for (int n = 0; n < NROOTS; ++n) {
        uint8_t d = s[n];
        for (int i = 1; i <= L; ++i) {
            d ^= gf_mul(lambda[i], s[n - i]);
        }

        if (d == 0) {
            m++;
        } else if (2 * L <= n) {
            uint8_t t[NROOTS + 1];
            std::memcpy(t, lambda, sizeof(lambda));

            uint8_t scale = gf_div(d, b_star);
            for (int i = 0; i <= NROOTS; ++i) {
                if (i + m <= NROOTS) {
                    lambda[i + m] ^= gf_mul(scale, b[i]);
                }
            }

            L = n + 1 - L;
            std::memcpy(b, t, sizeof(t));
            b_star = d;
            m = 1;
        } else {
            uint8_t scale = gf_div(d, b_star);
            for (int i = 0; i <= NROOTS; ++i) {
                if (i + m <= NROOTS) {
                    lambda[i + m] ^= gf_mul(scale, b[i]);
                }
            }
            m++;
        }
    }

    if (L > 8) {
        return -1; // Mais erros do que a capacidade de correcao t=8
    }

    // 3. Busca de Chien para encontrar as raizes X_k^-1 do polinomio localizador
    int err_pos[NROOTS];
    int num_errors = 0;

    for (int i = 0; i < 204; ++i) {
        // Potencia correspondente no polinomio (203 - i)
        int p_idx = 203 - i;
        uint8_t x_inv = exp_table_[(255 - p_idx) % 255]; // X_k^-1 = alpha^(-p_idx)
        uint8_t sum = lambda[0];
        uint8_t term = 1;
        for (int j = 1; j <= L; ++j) {
            term = gf_mul(term, x_inv);
            sum ^= gf_mul(lambda[j], term);
        }
        if (sum == 0) {
            err_pos[num_errors++] = i;
        }
    }

    if (num_errors != L) {
        return -1; // Numero de raizes nao bate com o grau do localizador: erro incorrigivel
    }

    // 4. Algoritmo de Forney para avaliar o valor do erro
    // Polinomio avaliador de erro Omega(x) = [S(x) * Lambda(x)] mod x^16
    uint8_t omega[NROOTS] = {0};
    for (int i = 0; i < NROOTS; ++i) {
        for (int j = 0; j <= i && j <= L; ++j) {
            omega[i] ^= gf_mul(lambda[j], s[i - j]);
        }
    }

    // Corrigir cada erro: para FCR = 0, e_k = X_k * Omega(X_k^-1) / Lambda'(X_k^-1)
    for (int i = 0; i < num_errors; ++i) {
        int pos = err_pos[i];
        int p_idx = 203 - pos;
        uint8_t x_k = exp_table_[p_idx];
        uint8_t x_inv = exp_table_[(255 - p_idx) % 255];

        // Omega(X_k^-1)
        uint8_t num = 0;
        uint8_t term = 1;
        for (int j = 0; j < NROOTS; ++j) {
            num ^= gf_mul(omega[j], term);
            term = gf_mul(term, x_inv);
        }

        // Derivada formal Lambda'(X_k^-1)
        uint8_t den = 0;
        term = 1;
        for (int j = 1; j <= L; j += 2) {
            den ^= gf_mul(lambda[j], term);
            term = gf_mul(term, gf_mul(x_inv, x_inv));
        }

        if (den == 0) {
            return -1;
        }

        uint8_t err_val = gf_mul(x_k, gf_div(num, den));
        block204[pos] ^= err_val;
    }

    return num_errors;
}

} // namespace isdbt
