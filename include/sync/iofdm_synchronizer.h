#pragma once

#include <complex>
#include <vector>
#include <cstddef>
#include "dsp/ofdm_sync.hpp"

namespace isdbt {

/**
 * @brief Interface abstrata comum para sincronizadores OFDM (R4).
 */
class IOFDMSynchronizer {
public:
    virtual ~IOFDMSynchronizer() = default;

    /**
     * @brief Processa um bloco de amostras I/Q de entrada e produz simbolos sincronizados.
     * @param in Ponteiro para amostras complexas I/Q no dominio do tempo.
     * @param n Quantidade de amostras no buffer de entrada.
     * @param out Vetor que recebe as amostras dos simbolos sincronizados no dominio do tempo
     *            (multiplos de fft_size_ amostras uteis alinhadas para o demodulador).
     */
    virtual void process(const std::complex<float>* in, size_t n,
                         std::vector<std::complex<float>>& out) = 0;

    /**
     * @brief Retorna o estado atual da maquina de estados de sincronismo.
     */
    virtual const SyncState& state() const noexcept = 0;

    virtual float peak_correlation() const noexcept = 0;

    /**
     * @brief Reinicia o estado interno, NCO e filtros de sincronismo.
     */
    virtual void reset() noexcept = 0;

    /**
     * @brief Estimativa de Carrier Frequency Offset (CFO) em Hz (DIP).
     */
    virtual float estimated_cfo_hz() const noexcept { return 0.0f; }

    /**
     * @brief Estimativa de Sampling Clock Offset (SCO) em ppm (DIP).
     */
    virtual float estimated_sco_ppm() const noexcept { return 0.0f; }

    /**
     * @brief Representacao textual detalhada do estado da FSM interna.
     */
    virtual const char* detailed_state_str() const noexcept {
        return (state() == SyncState::Tracking) ? "Tracking" : "Acquisition";
    }

    /**
     * @brief Notifica a deteccao confirmada de sincronismo de quadro TMCC (DIP).
     * @param sp_phase Fase dos pilotos dispersos (0..3) esperada para o PROXIMO simbolo a ser
     *        processado pelo sincronizador, i.e. (indice_do_simbolo_no_quadro mod 4).
     * @param ifo_bins Desvio de frequencia inteiro (IFO) em subportadoras detectado pelo demodulador.
     */
    virtual void notify_frame_sync(int sp_phase, int ifo_bins = 0) noexcept {
        (void)sp_phase;
        (void)ifo_bins;
    }

    /**
     * @brief Informa se a fase dos pilotos dispersos esta ancorada no superframe TMCC.
     */
    virtual bool is_tmcc_anchored() const noexcept { return false; }
};

} // namespace isdbt
