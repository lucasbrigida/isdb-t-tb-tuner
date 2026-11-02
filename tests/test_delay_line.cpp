#include <iostream>
#include <complex>
#include <cstdlib>
#include "dsp/delay_line.hpp"
#include "dsp/tmcc_bit_buffer.hpp"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while(0)

void test_fixed_delay_line() {
    isdbt::FixedDelayLine<int, 120> dl;
    dl.init(5, 0);
    TEST_ASSERT(dl.delay() == 5);

    // Initial 5 values should be 0
    for (int i = 0; i < 5; ++i) {
        int out = dl.push_and_get(100 + i);
        TEST_ASSERT(out == 0);
    }

    // Subsequent values should emerge with exactly 5 steps delay
    for (int i = 0; i < 100; ++i) {
        int out = dl.push_and_get(200 + i);
        int expected = (i < 5) ? (100 + i) : (200 + (i - 5));
        TEST_ASSERT(out == expected);
    }

    // Delay 0 should act as direct passthrough
    isdbt::FixedDelayLine<int, 10> dl_zero;
    dl_zero.init(0, 0);
    for (int i = 0; i < 10; ++i) {
        TEST_ASSERT(dl_zero.push_and_get(i) == i);
    }

    // Test reset
    dl.reset(-1);
    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT(dl.push_and_get(500 + i) == -1);
    }

    std::cout << "  [PASS] test_fixed_delay_line passou com sucesso.\n";
}

void test_dynamic_delay_line() {
    isdbt::DynamicDelayLine<std::complex<float>> ddl;
    ddl.init(10, std::complex<float>(0.0f, 0.0f));
    TEST_ASSERT(ddl.delay() == 10);

    for (int i = 0; i < 10; ++i) {
        auto out = ddl.push_and_get(std::complex<float>(static_cast<float>(i + 1), 0.0f));
        TEST_ASSERT(out == std::complex<float>(0.0f, 0.0f));
    }

    for (int i = 0; i < 100; ++i) {
        auto out = ddl.push_and_get(std::complex<float>(static_cast<float>(i + 100), 0.0f));
        float expected_val = (i < 10) ? static_cast<float>(i + 1) : static_cast<float>(100 + (i - 10));
        TEST_ASSERT(out == std::complex<float>(expected_val, 0.0f));
    }

    // Delay 0
    isdbt::DynamicDelayLine<int> ddl_zero;
    ddl_zero.init(0, 0);
    TEST_ASSERT(ddl_zero.push_and_get(42) == 42);

    std::cout << "  [PASS] test_dynamic_delay_line passou com sucesso.\n";
}

void test_tmcc_bit_buffer() {
    isdbt::TmccBitBuffer tb;
    tb.reset(0);
    TEST_ASSERT(tb.size() == 204);
    for (size_t i = 0; i < 204; ++i) {
        TEST_ASSERT(tb[i] == 0);
    }

    // Push 204 distinct values (1 or 0)
    for (int i = 0; i < 204; ++i) {
        tb.push_back(static_cast<uint8_t>(i % 2));
    }
    for (size_t i = 0; i < 204; ++i) {
        TEST_ASSERT(tb[i] == static_cast<uint8_t>(i % 2));
    }

    // Push 50 more bits and verify FIFO alignment
    for (int i = 204; i < 254; ++i) {
        tb.push_back(static_cast<uint8_t>((i * 7) % 2));
    }
    // Element at 0 must be old element 50 (i=50)
    for (size_t i = 0; i < 204; ++i) {
        int original_idx = static_cast<int>(50 + i);
        uint8_t expected = (original_idx < 204)
            ? static_cast<uint8_t>(original_idx % 2)
            : static_cast<uint8_t>((original_idx * 7) % 2);
        TEST_ASSERT(tb[i] == expected);
    }

    std::cout << "  [PASS] test_tmcc_bit_buffer passou com sucesso.\n";
}

int main() {
    std::cout << "[TEST] Iniciando testes unitarios de DelayLine e TmccBitBuffer...\n";
    test_fixed_delay_line();
    test_dynamic_delay_line();
    test_tmcc_bit_buffer();
    std::cout << "[SUCESSO] Todos os testes passaram!\n";
    return 0;
}
