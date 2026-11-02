#include <iostream>
#include <complex>
#include <cstdlib>
#include "dsp/iq_stream_buffer.hpp"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while(0)

void test_basic_assign_and_append() {
    isdbt::IqStreamBuffer buf(1024);
    TEST_ASSERT(buf.available() == 0);
    TEST_ASSERT(buf.offset() == 0);

    std::vector<std::complex<float>> init_data(100);
    for (size_t i = 0; i < 100; ++i) {
        init_data[i] = std::complex<float>(static_cast<float>(i), -static_cast<float>(i));
    }

    buf.assign(init_data.data(), init_data.size());
    TEST_ASSERT(buf.available() == 100);
    TEST_ASSERT(buf.offset() == 0);
    TEST_ASSERT(buf[0] == std::complex<float>(0.0f, -0.0f));
    TEST_ASSERT(buf[99] == std::complex<float>(99.0f, -99.0f));

    std::vector<std::complex<float>> append_data(50);
    for (size_t i = 0; i < 50; ++i) {
        append_data[i] = std::complex<float>(static_cast<float>(100 + i), -static_cast<float>(100 + i));
    }
    buf.append(append_data.data(), append_data.size());
    TEST_ASSERT(buf.available() == 150);
    TEST_ASSERT(buf[149] == std::complex<float>(149.0f, -149.0f));

    std::cout << "  [PASS] test_basic_assign_and_append passou com sucesso.\n";
}

void test_consume_and_offset() {
    isdbt::IqStreamBuffer buf(1024);
    std::vector<std::complex<float>> data(200);
    for (size_t i = 0; i < 200; ++i) {
        data[i] = std::complex<float>(static_cast<float>(i), 1.0f);
    }
    buf.assign(data.data(), data.size());

    buf.consume(30);
    TEST_ASSERT(buf.available() == 170);
    TEST_ASSERT(buf.offset() == 30);
    TEST_ASSERT(buf[0] == std::complex<float>(30.0f, 1.0f));
    TEST_ASSERT(buf[169] == std::complex<float>(199.0f, 1.0f));
    TEST_ASSERT(buf.data_at(10) == &buf[10]);

    buf.consume(50);
    TEST_ASSERT(buf.available() == 120);
    TEST_ASSERT(buf.offset() == 80);
    TEST_ASSERT(buf[0] == std::complex<float>(80.0f, 1.0f));

    buf.clear();
    TEST_ASSERT(buf.available() == 0);
    TEST_ASSERT(buf.offset() == 0);

    std::cout << "  [PASS] test_consume_and_offset passou com sucesso.\n";
}

void test_compaction_threshold() {
    isdbt::IqStreamBuffer buf(131072);
    // Cria 50000 amostras
    std::vector<std::complex<float>> chunk1(50000);
    for (size_t i = 0; i < 50000; ++i) {
        chunk1[i] = std::complex<float>(static_cast<float>(i), 2.0f);
    }
    buf.assign(chunk1.data(), chunk1.size());

    // Consome 35000 amostras (offset > 32768 E offset > 50000/2)
    // Isso deve acionar a compactação via memmove!
    buf.consume(35000);
    TEST_ASSERT(buf.offset() == 0); // Resetado para 0 após compactação
    TEST_ASSERT(buf.available() == 15000);
    TEST_ASSERT(buf[0] == std::complex<float>(35000.0f, 2.0f));
    TEST_ASSERT(buf[14999] == std::complex<float>(49999.0f, 2.0f));

    std::cout << "  [PASS] test_compaction_threshold passou com sucesso.\n";
}

int main() {
    std::cout << "[TEST] Iniciando testes unitarios de IqStreamBuffer...\n";
    test_basic_assign_and_append();
    test_consume_and_offset();
    test_compaction_threshold();
    std::cout << "[SUCESSO] Todos os testes de IqStreamBuffer passaram!\n";
    return 0;
}

