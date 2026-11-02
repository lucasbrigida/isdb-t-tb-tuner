#include <iostream>
#include <cstdlib>
#include <cmath>
#include "radio/frequency_plan.hpp"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while(0)

void test_frequency_calculation() {
    std::cout << "[Test] Frequencias dos canais UHF ISDB-T..." << std::endl;

    // Canal 14: 470 + 3 + 1/7 MHz = 473.142857 MHz = 473142857 Hz
    uint32_t f14 = isdbt::get_channel_freq(14);
    TEST_ASSERT(f14 >= 473142800 && f14 <= 473142900);

    // Canal 23: 470 + 9*6 + 3 + 1/7 MHz = 527.142857 MHz
    uint32_t f23 = isdbt::get_channel_freq(23);
    TEST_ASSERT(f23 >= 527142800 && f23 <= 527142900);

    // Canal 24: 533.142857 MHz
    uint32_t f24 = isdbt::get_channel_freq(24);
    TEST_ASSERT(f24 >= 533142800 && f24 <= 533142900);

    // Canal 69: 803.142857 MHz
    uint32_t f69 = isdbt::get_channel_freq(69);
    TEST_ASSERT(f69 >= 803142800 && f69 <= 803142900);

    // Espacamento estrito de 6.0 MHz entre canais adjacentes
    for (int ch = 14; ch < 69; ++ch) {
        uint32_t curr = isdbt::get_channel_freq(ch);
        uint32_t next = isdbt::get_channel_freq(ch + 1);
        TEST_ASSERT(next - curr == 6000000);
    }
}

void test_channel_validity() {
    std::cout << "[Test] Validade de canais..." << std::endl;

    TEST_ASSERT(!isdbt::is_valid_channel(0));
    TEST_ASSERT(!isdbt::is_valid_channel(13));
    TEST_ASSERT(isdbt::is_valid_channel(14));
    TEST_ASSERT(isdbt::is_valid_channel(28));
    TEST_ASSERT(isdbt::is_valid_channel(45));
    TEST_ASSERT(isdbt::is_valid_channel(69));
    TEST_ASSERT(!isdbt::is_valid_channel(70));
    TEST_ASSERT(!isdbt::is_valid_channel(-5));
}

void test_signal_ratings() {
    std::cout << "[Test] Classificacao de potencia RSSI..." << std::endl;

    auto r_alto = isdbt::get_signal_rating(-4.5f);
    TEST_ASSERT(r_alto.text == "Alto (Forte)");
    TEST_ASSERT(r_alto.bar == "[#####]");

    auto r_normal = isdbt::get_signal_rating(-11.0f);
    TEST_ASSERT(r_normal.text == "Normal (Bom)");
    TEST_ASSERT(r_normal.bar == "[#### ]");

    auto r_moderado = isdbt::get_signal_rating(-15.6f);
    TEST_ASSERT(r_moderado.text == "Moderado");
    TEST_ASSERT(r_moderado.bar == "[###  ]");

    auto r_fraco = isdbt::get_signal_rating(-23.1f);
    TEST_ASSERT(r_fraco.text == "Fraco");
    TEST_ASSERT(r_fraco.bar == "[##   ]");

    auto r_semsinal = isdbt::get_signal_rating(-28.7f);
    TEST_ASSERT(r_semsinal.text == "Sem Sinal");
    TEST_ASSERT(r_semsinal.bar == "[     ]");
}

void test_string_padding() {
    std::cout << "[Test] Alinhamento e padding de string..." << std::endl;

    std::string s = "BAND";
    std::string padded_left = isdbt::pad_string(s, 8, true);
    TEST_ASSERT(padded_left.length() == 8);
    TEST_ASSERT(padded_left == "BAND    ");

    std::string padded_right = isdbt::pad_string(s, 8, false);
    TEST_ASSERT(padded_right.length() == 8);
    TEST_ASSERT(padded_right == "    BAND");

    std::string long_str = "REDE BANDEIRANTES HD";
    std::string truncated = isdbt::pad_string(long_str, 7, true);
    TEST_ASSERT(truncated.length() == 7);
    TEST_ASSERT(truncated == "REDE BA");
}

int main() {
    test_frequency_calculation();
    test_channel_validity();
    test_signal_ratings();
    test_string_padding();

    std::cout << "\033[1;32m[PASS] Todos os testes de plano de frequencia passaram com sucesso!\033[0m\n";
    return 0;
}

