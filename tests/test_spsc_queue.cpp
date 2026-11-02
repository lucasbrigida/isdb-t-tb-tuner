#include <iostream>
#include <thread>
#include <atomic>
#include <cstdlib>
#include <vector>
#include <memory>
#include "dsp/spsc_capture_queue.hpp"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while(0)

void test_empty_and_full() {
    auto q = std::make_unique<isdbt::SpscCaptureQueue<4>>();
    TEST_ASSERT(q->empty());
    TEST_ASSERT(q->size() == 0);

    isdbt::SpscCaptureQueue<4>::ReadView rview;
    TEST_ASSERT(!q->try_acquire_read(rview));

    // Fill all 4 slots
    for (uint64_t i = 0; i < 4; ++i) {
        isdbt::SpscCaptureQueue<4>::WriteView wview;
        TEST_ASSERT(q->try_acquire_write(wview));
        TEST_ASSERT(wview.block != nullptr);
        wview.block->sequence = i;
        wview.block->size = 1000 + i;
        q->publish_write(wview);
    }

    TEST_ASSERT(q->size() == 4);
    TEST_ASSERT(!q->empty());

    // 5th attempt must fail because queue is full
    isdbt::SpscCaptureQueue<4>::WriteView wview_overflow;
    TEST_ASSERT(!q->try_acquire_write(wview_overflow));
    q->record_drop();
    TEST_ASSERT(q->dropped_blocks() == 1);

    // Read all 4 slots in order
    for (uint64_t i = 0; i < 4; ++i) {
        TEST_ASSERT(q->try_acquire_read(rview));
        TEST_ASSERT(rview.block->sequence == i);
        TEST_ASSERT(rview.block->size == 1000 + i);
        q->release_read(rview);
    }

    TEST_ASSERT(q->empty());
    TEST_ASSERT(!q->try_acquire_read(rview));
    std::cout << "  [PASS] test_empty_and_full passou com sucesso.\n";
}

void test_wraparound() {
    auto q = std::make_unique<isdbt::SpscCaptureQueue<4>>();
    // Cycle through 100 slots to test wrap-around logic
    for (uint64_t i = 0; i < 100; ++i) {
        isdbt::SpscCaptureQueue<4>::WriteView wview;
        TEST_ASSERT(q->try_acquire_write(wview));
        wview.block->sequence = i;
        q->publish_write(wview);

        isdbt::SpscCaptureQueue<4>::ReadView rview;
        TEST_ASSERT(q->try_acquire_read(rview));
        TEST_ASSERT(rview.block->sequence == i);
        q->release_read(rview);
    }
    TEST_ASSERT(q->empty());
    std::cout << "  [PASS] test_wraparound passou com sucesso.\n";
}

void test_concurrent_producer_consumer() {
    auto q = std::make_unique<isdbt::SpscCaptureQueue<16>>();
    const uint64_t TOTAL_ITEMS = 50000;
    std::atomic<bool> producer_done{false};

    std::thread producer([&]() {
        uint64_t seq = 0;
        while (seq < TOTAL_ITEMS) {
            isdbt::SpscCaptureQueue<16>::WriteView wview;
            if (q->try_acquire_write(wview)) {
                wview.block->sequence = seq;
                wview.block->size = static_cast<size_t>(seq % 100);
                q->publish_write(wview);
                seq++;
            } else {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    uint64_t consumed = 0;
    std::thread consumer([&]() {
        while (!producer_done.load(std::memory_order_acquire) || !q->empty()) {
            isdbt::SpscCaptureQueue<16>::ReadView rview;
            if (q->try_acquire_read(rview)) {
                TEST_ASSERT(rview.block->sequence == consumed);
                TEST_ASSERT(rview.block->size == (consumed % 100));
                q->release_read(rview);
                consumed++;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    TEST_ASSERT(consumed == TOTAL_ITEMS);
    TEST_ASSERT(q->empty());
    TEST_ASSERT(q->dropped_blocks() == 0);
    std::cout << "  [PASS] test_concurrent_producer_consumer (" << TOTAL_ITEMS << " itens) passou com sucesso.\n";
}

int main() {
    std::cout << "[TEST] Iniciando testes unitarios da Fila SPSC Lock-Free...\n";
    test_empty_and_full();
    test_wraparound();
    test_concurrent_producer_consumer();
    std::cout << "[SUCESSO] Todos os testes da Fila SPSC passaram!\n";
    return 0;
}
