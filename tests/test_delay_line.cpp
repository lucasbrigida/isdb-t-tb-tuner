#include <iostream>
#include <complex>
#include <cstdlib>
#include "dsp/delay_line.hpp"

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

int main() {
    std::cout << "[TEST] Iniciando testes unitarios de DelayLine...\n";
    test_fixed_delay_line();
    test_dynamic_delay_line();
    std::cout << "[SUCESSO] Todos os testes de DelayLine passaram!\n";
    return 0;
}
