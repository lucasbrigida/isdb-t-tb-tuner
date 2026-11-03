#pragma once

#include <memory>
#include "sync/iofdm_synchronizer.h"
#include "sync/sync_params.h"

namespace isdbt {

/**
 * @brief Fabrica de sincronizadores OFDM (R8).
 * @param algo Algoritmo de sincronismo (1 = Van de Beek, 2 = Robust GR-ISDBT).
 * @param params Parametros fisicos de sincronizacao (taxa, FFT, GI).
 * @return unique_ptr contendo a instancia do sincronizador.
 */
std::unique_ptr<IOFDMSynchronizer> make_synchronizer(OFDMAlgorithm algo, const SyncParams& params = SyncParams{});

} // namespace isdbt

