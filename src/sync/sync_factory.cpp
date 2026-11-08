#include "sync/sync_factory.h"
#include "sync/van_de_beek_sync.h"
#include "sync/sp_cir_sync.h"
#include <iostream>
#include <stdexcept>

namespace isdbt {

std::unique_ptr<IOFDMSynchronizer> make_synchronizer(OFDMAlgorithm algo, const SyncParams& params) {
    if (algo == OFDMAlgorithm::VanDeBeek) {
        return std::make_unique<VanDeBeekSync>(params);
    } else if (algo == OFDMAlgorithm::SpCir || algo == OFDMAlgorithm::RobustGRISDBT) {
#if defined(ISDBT_ENABLE_SP_CIR_SYNC) || defined(ISDBT_ENABLE_GRISDBT_SYNC)
        return std::make_unique<SpCirSynchronizer>(params);
#else
        std::cerr << "[ERRO] Suporte ao algoritmo --ofdm=2 (SP-CIR Synchronizer) nao foi habilitado na compilacao.\n"
                  << "       Recompile com a opcao CMake -DISDBT_ENABLE_SP_CIR_SYNC=ON (ou -DISDBT_ENABLE_GRISDBT_SYNC=ON).\n";
        return nullptr;
#endif
    }
    throw std::invalid_argument("Algoritmo de sincronismo OFDM invalido");
}

} // namespace isdbt
