// Valida o alias de retrocompatibilidade RobustGRISDBTSync -> SpCirSynchronizer
// e a resolucao de OFDMAlgorithm::RobustGRISDBT pela fabrica make_synchronizer.
#include <iostream>
#include <memory>
#include <type_traits>
#include <cstdlib>
#include "sync/robust_grisdbt_sync.h"
#include "sync/sync_factory.h"
#include "dsp/constants.hpp"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while(0)

int main() {
    std::cout << "=== Teste de Alias RobustGRISDBTSync (compatibilidade legada) ===\n";

    static_assert(std::is_same_v<isdbt::RobustGRISDBTSync, isdbt::SpCirSynchronizer>,
                  "RobustGRISDBTSync deve ser alias exato de SpCirSynchronizer");
    static_assert(std::is_same_v<isdbt::RobustSyncState, isdbt::SpCirSyncState>,
                  "RobustSyncState deve ser alias exato de SpCirSyncState");
    static_assert(static_cast<int>(isdbt::OFDMAlgorithm::RobustGRISDBT) ==
                  static_cast<int>(isdbt::OFDMAlgorithm::SpCir),
                  "OFDMAlgorithm::RobustGRISDBT deve coincidir com SpCir (--ofdm 2)");

    isdbt::SyncParams params;
    params.mode = isdbt::OfdmMode::MODE_1_2K;
    params.gi = isdbt::GuardInterval::GI_1_16;
    params.sample_rate = isdbt::constants::SAMPLE_RATE_HZ;
    params.fft_size = 2048;
    params.gi_size = 128;

    isdbt::RobustGRISDBTSync legacy(params);
    TEST_ASSERT(legacy.robust_state() == isdbt::RobustSyncState::Acquisition);
    TEST_ASSERT(legacy.sp_cir_state() == isdbt::SpCirSyncState::Acquisition);
    TEST_ASSERT(legacy.state() == isdbt::SyncState::Acquisition);

    auto via_legacy_enum = isdbt::make_synchronizer(isdbt::OFDMAlgorithm::RobustGRISDBT, params);
    TEST_ASSERT(via_legacy_enum != nullptr);
    TEST_ASSERT(dynamic_cast<isdbt::SpCirSynchronizer*>(via_legacy_enum.get()) != nullptr);

    auto via_new_enum = isdbt::make_synchronizer(isdbt::OFDMAlgorithm::SpCir, params);
    TEST_ASSERT(via_new_enum != nullptr);
    TEST_ASSERT(dynamic_cast<isdbt::SpCirSynchronizer*>(via_new_enum.get()) != nullptr);

    std::cout << "[OK] Alias RobustGRISDBTSync e OFDMAlgorithm::RobustGRISDBT resolvem para SpCirSynchronizer\n";
    return 0;
}
