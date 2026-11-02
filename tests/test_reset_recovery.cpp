#include <iostream>
#include <vector>
#include <complex>
#include <memory>
#include <cstdlib>
#include "dsp/spsc_capture_queue.hpp"
#include "dsp/iq_stream_buffer.hpp"
#include "dsp/dsp_metrics.hpp"
#include "dsp/ofdm_sync.hpp"
#include "dsp/oneseg_demodulator.hpp"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while(0)

// Funcao de reset identica a utilizada em main.cpp
void reset_reception_stream(
    isdbt::OfdmSync& sync,
    isdbt::OneSegDemodulator& demod,
    isdbt::IqStreamBuffer& stream_iq_buf,
    std::vector<std::complex<float>>& iq_buf,
    bool& ofdm_locked,
    float& tracked_ffo_hz,
    float& frac_pos,
    std::size_t& gi_len,
    std::size_t& sym_len,
    int& unlock_counter,
    isdbt::DspMetrics* metrics = nullptr) {

    stream_iq_buf.clear();
    frac_pos = 0.0f;
    ofdm_locked = false;
    tracked_ffo_hz = 0.0f;
    unlock_counter = 0;
    gi_len = 0;
    sym_len = 0;

    (void)iq_buf;
    sync.reset();
    demod.reset();

    if (metrics) {
        metrics->decoder_resets++;
    }
}

void test_discontinuity_detection_and_reset() {
    auto queue = std::make_unique<isdbt::SpscCaptureQueue<16>>();
    isdbt::DspMetrics metrics{};

    isdbt::OfdmSync sync(isdbt::OfdmMode::MODE_1_2K, isdbt::GuardInterval::GI_1_16);
    isdbt::OneSegDemodulator demod(isdbt::OfdmMode::MODE_1_2K, isdbt::GuardInterval::GI_1_16);
    isdbt::IqStreamBuffer stream_iq_buf(500);
    std::vector<std::complex<float>> dummy_iq(500, std::complex<float>(1.0f, 1.0f));
    stream_iq_buf.assign(dummy_iq.data(), dummy_iq.size());
    std::vector<std::complex<float>> iq_buf(100);
    bool ofdm_locked = true;
    float tracked_ffo_hz = 250.0f;
    float frac_pos = 42.0f;
    std::size_t gi_len = 128;
    std::size_t sym_len = 2176;
    int unlock_counter = 5;

    // Sequencia artificial com lacuna: 100, 101, 102, 105, 106
    std::vector<uint64_t> input_seqs = {100, 101, 102, 105, 106};

    for (uint64_t seq : input_seqs) {
        isdbt::SpscCaptureQueue<16>::WriteView wview;
        TEST_ASSERT(queue->try_acquire_write(wview));
        wview.block->sequence = seq;
        wview.block->size = 1000;
        wview.block->discontinuity_before = (seq == 105);
        queue->publish_write(wview);
    }

    uint64_t expected_sequence = 0;
    bool have_expected_sequence = false;
    uint64_t resets_triggered = 0;

    while (!queue->empty()) {
        isdbt::SpscCaptureQueue<16>::ReadView rview;
        TEST_ASSERT(queue->try_acquire_read(rview));
        const auto& block = *rview.block;

        bool discontinuity = block.discontinuity_before;
        if (have_expected_sequence && block.sequence != expected_sequence) {
            discontinuity = true;
            metrics.sequence_gaps++;
        }

        expected_sequence = block.sequence + 1;
        have_expected_sequence = true;
        metrics.input_blocks++;

        if (discontinuity) {
            reset_reception_stream(sync, demod, stream_iq_buf, iq_buf,
                                   ofdm_locked, tracked_ffo_hz, frac_pos,
                                   gi_len, sym_len, unlock_counter, &metrics);
            resets_triggered++;

            // Verifica que o estado foi reiniciado
            TEST_ASSERT(!ofdm_locked);
            TEST_ASSERT(stream_iq_buf.empty());
            TEST_ASSERT(frac_pos == 0.0f);
            TEST_ASSERT(tracked_ffo_hz == 0.0f);
            TEST_ASSERT(unlock_counter == 0);
        }

        queue->release_read(rview);
    }

    TEST_ASSERT(metrics.input_blocks == 5);
    TEST_ASSERT(metrics.sequence_gaps == 1); // Lacuna detectada entre 102 e 105
    TEST_ASSERT(metrics.decoder_resets == 1);
    TEST_ASSERT(resets_triggered == 1);

    std::cout << "  [PASS] test_discontinuity_detection_and_reset passou com sucesso (1 gap, 1 reset coordenado).\n";
}

int main() {
    std::cout << "[TEST] Iniciando teste de deteccao de descontinuidade IQ e recuperacao...\n";
    test_discontinuity_detection_and_reset();
    std::cout << "[SUCESSO] Teste de recuperacao apos descontinuidade passou!\n";
    return 0;
}
