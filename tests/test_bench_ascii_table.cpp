#include <iostream>
#include <sstream>
#include <cassert>
#include <string>
#include "utils/ascii_table.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while(0)

int main() {
    std::cout << "=== Teste de Formatacao de Tabela ASCII +---+ (R20, R20.1, N7) ===\n";

    isdbt::AsciiTable table;
    table.set_headers({"Metrica", "ofdm=1", "ofdm=2", "Melhor"});
    table.set_alignments({isdbt::Alignment::Left, isdbt::Alignment::Right, isdbt::Alignment::Right, isdbt::Alignment::Left});

    table.add_row({"wall_time_s", "12.340 *", "18.760", "ofdm=1"});
    table.add_row({"cycles_per_symbol", "1204 *", "2098", "ofdm=1"});
    table.add_row({"heap_allocs_hot", "0", "0", "empate"});
    table.add_row({"locked_time_ratio", "0.873", "0.952 *", "ofdm=2"});
    table.add_row({"sync_loss_count", "14", "4 *", "ofdm=2"});

    std::string rendered = table.render(100);
    std::cout << rendered;

    // 1. Verifica presenca das bordas +---+
    TEST_ASSERT(rendered.find("+---+") != std::string::npos || rendered.find("+---") != std::string::npos);
    TEST_ASSERT(rendered.find("| Metrica") != std::string::npos);

    // 2. Verifica que nenhum caractere eh maior que 127 (puro ASCII para LANG=C)
    for (char c : rendered) {
        TEST_ASSERT(static_cast<unsigned char>(c) < 128);
    }

    // 3. Verifica limite de colunas por linha (<= 100 colunas)
    std::stringstream ss(rendered);
    std::string line;
    while (std::getline(ss, line)) {
        TEST_ASSERT(line.length() <= 100);
    }

    isdbt::AsciiTable wide_table({"very long first heading", "very long second heading"});
    wide_table.add_row({"a cell with a lot of text", "another very long cell"});
    std::string bounded = wide_table.render(20);
    std::stringstream bounded_stream(bounded);
    while (std::getline(bounded_stream, line)) {
        TEST_ASSERT(line.length() <= 20);
    }
    TEST_ASSERT(wide_table.render(8).empty());

    std::cout << "[PASS] Formatador AsciiTable aprovado com sucesso.\n";
    return 0;
}
