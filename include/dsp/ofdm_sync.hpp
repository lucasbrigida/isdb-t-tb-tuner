#pragma once

#include <complex>
#include <vector>
#include <cstdint>
#include <fftw3.h>
#include <volk/volk.h>

namespace isdbt {

/**
 * @brief Modos de transmissao OFDM definidos na norma ABNT NBR 15601 / ARIB STD-B31.
 * O valor numerico corresponde ao tamanho total da FFT para um canal de 8.126984 MHz.
 */
enum class OfdmMode {
    MODE_1_2K = 2048, ///< Modo 1: 2048 pontos FFT, Tu = 252 us, delta_f = 3.968 kHz
    MODE_2_4K = 4096, ///< Modo 2: 4096 pontos FFT, Tu = 504 us, delta_f = 1.984 kHz
    MODE_3_8K = 8192  ///< Modo 3: 8192 pontos FFT, Tu = 1008 us, delta_f = 0.992 kHz (Padrao Brasil/Japao)
};

/**
 * @brief Razao do Intervalo de Guarda (Prefixo Ciclico) sobre a parte util do simbolo Tu.
 */
enum class GuardInterval {
    GI_1_4  = 4,  ///< Prefixo Ciclico = N_fft / 4 (maior protecao contra ecos longos)
    GI_1_8  = 8,  ///< Prefixo Ciclico = N_fft / 8
    GI_1_16 = 16, ///< Prefixo Ciclico = N_fft / 16 (mais comum no Brasil)
    GI_1_32 = 32  ///< Prefixo Ciclico = N_fft / 32
};

/**
 * @brief Estrutura que encapsula as metricas de sincronizacao de um simbolo OFDM.
 */
struct SyncResult {
    bool locked = false;          ///< True se a correlacao de pico ultrapassar o limiar de deteccao (> 0.20)
    int symbol_offset = 0;        ///< Indice da primeira amostra util do simbolo (pos-prefixo ciclico)
    float ffo_hz = 0.0f;          ///< Desvio Fracionario de Frequencia (FFO) estimado em Hertz
    float peak_correlation = 0.0f;///< Coeficiente de correlacao normalizado [0.0 .. 1.0]
    float snr_est_db = 0.0f;      ///< Relacao Sinal-Ruido estimada baseada no pico de correlacao (dB)
};

/**
 * @brief Sincronizador de Simbolo OFDM e Estimador de FFO (Van de Beek ML Estimator)
 * 
 * Implementa a correlacao de prefixo ciclico no dominio do tempo:
 *   Gamma(d) = sum_{m=0}^{N_gi - 1} r(d + m) * conj(r(d + m + N_fft))
 *   Phi(d)   = 0.5 * sum_{m=0}^{N_gi - 1} (|r(d + m)|^2 + |r(d + m + N_fft)|^2)
 * 
 * A fase do pico Gamma(d) fornece diretamente o desvio de frequencia da portadora (FFO):
 *   delta_f = -angle(Gamma) / (2 * pi * Tu)
 */
class OfdmSync {
public:
    /**
     * @brief Construtor do sincronizador OFDM.
     * @param mode Modo de operacao (Mode 1, Mode 2 ou Mode 3).
     * @param gi Razao do intervalo de guarda (1/4, 1/8, 1/16, 1/32).
     * @param sample_rate Taxa de amostragem em Hz (padrao 2031746 S/s).
     */
    OfdmSync(OfdmMode mode = OfdmMode::MODE_3_8K, GuardInterval gi = GuardInterval::GI_1_16, float sample_rate = 2031746.0f);
    ~OfdmSync();

    /**
     * @brief Altera a configuracao de modo e intervalo de guarda.
     */
    void set_mode(OfdmMode mode, GuardInterval gi);

    /**
     * @brief Realiza a busca deslizante de borda de simbolo e estimativa de FFO.
     * @param samples Buffer de amostras I/Q complexas no dominio do tempo.
     * @param num_samples Quantidade de amostras no buffer (minimo 2 simbolos completos).
     * @return SyncResult contendo estado de lock, offset e desvio de frequencia.
     */
    SyncResult find_symbol_boundary(const std::complex<float>* samples, size_t num_samples);

    /**
     * @brief Corrige o desvio fracionario de frequencia (FFO) desrotacionando o sinal I/Q.
     * Utiliza instrucoes SIMD AVX2/SSE atraves do VOLK para acelerar a multiplicacao complexa vetorial:
     *   samples[n] = samples[n] * exp(-j * 2 * pi * ffo * n / Fs)
     * @param samples Buffer I/Q modificado in-place.
     * @param num_samples Quantidade de amostras a desrotacionar.
     * @param ffo_hz Desvio de frequencia em Hz estimado por find_symbol_boundary.
     */
    void correct_frequency_offset(std::complex<float>* samples, size_t num_samples, float ffo_hz);

    /**
     * @brief Executa a FFT no simbolo alinhado e extrai as subportadoras do dominio da frequencia.
     * Aplica normalizacao de energia 1/sqrt(N_fft) e fftshift centralizando DC.
     * @param symbol_in Amostras no tempo da parte util do simbolo (comprimento fft_size_).
     * @param carriers_out Subportadoras no dominio da frequencia (comprimento fft_size_).
     */
    void process_symbol_fft(const std::complex<float>* symbol_in, std::complex<float>* carriers_out);

private:
    OfdmMode mode_;
    GuardInterval gi_;
    float sample_rate_;
    int fft_size_;
    int gi_size_;

    fftwf_complex* fft_in_;
    fftwf_complex* fft_out_;
    fftwf_plan fft_plan_;
};

} // namespace isdbt

