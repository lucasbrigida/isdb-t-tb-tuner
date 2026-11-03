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
};

} // namespace isdbt
