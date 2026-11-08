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

    // 4. Testes de UTF-8 e Larguras Fixas (113 caracteres)
    std::cout << "  Testando utf8_display_width e tabelas com largura fixa 113...\n";
    TEST_ASSERT(isdbt::AsciiTable::utf8_display_width("ASCII") == 5);
    TEST_ASSERT(isdbt::AsciiTable::utf8_display_width("São Paulo") == 9);
    TEST_ASSERT(isdbt::AsciiTable::utf8_display_width("Você") == 4);
    TEST_ASSERT(isdbt::AsciiTable::utf8_display_width("Graça") == 5);
    TEST_ASSERT(isdbt::AsciiTable::utf8_display_width("\033[30;47mTexto\033[0m") == 5);

    isdbt::AsciiTable epg_single({"Status", "Horario", "Duracao", "Classif.", "Genero", "Programa"});
    epg_single.set_alignments({isdbt::Alignment::Center, isdbt::Alignment::Center, isdbt::Alignment::Center, isdbt::Alignment::Center, isdbt::Alignment::Left, isdbt::Alignment::Left});
    epg_single.set_column_widths({9, 13, 7, 8, 12, 45});
    TEST_ASSERT(epg_single.total_width() == 113);

    epg_single.add_row({"[AGORA]", "03:00 - 05:30", "2h30m", "--", "--", "Bom Dia São Paulo"});
    epg_single.add_row({"[SEGUIR]", "05:30 - 06:30", "1h00m", "--", "--", "Bom Dia Brasil"});
    epg_single.add_row({"AGENDA", "07:35 - 08:45", "1h10m", "--", "Variedades", "Mais Você"});

    std::string epg_rendered = epg_single.render();
    std::stringstream epg_ss(epg_rendered);
    while (std::getline(epg_ss, line)) {
        if (line.empty()) continue;
        TEST_ASSERT(isdbt::AsciiTable::utf8_display_width(line) == 113);
    }

    // 5. Testes de Linha Selecionada com Inversao ANSI e Janela Deslizante
    std::cout << "  Testando selecao de linha com cores invertidas e janela deslizante...\n";
    epg_single.set_selected_row(1);
    std::string sel_rendered = epg_single.render();
    TEST_ASSERT(sel_rendered.find("\033[30;47m|") != std::string::npos);
    TEST_ASSERT(sel_rendered.find("Bom Dia Brasil") != std::string::npos);

    std::stringstream sel_ss(sel_rendered);
    while (std::getline(sel_ss, line)) {
        if (line.empty()) continue;
        TEST_ASSERT(isdbt::AsciiTable::utf8_display_width(line) == 113);
    }

    epg_single.set_visible_range(1, 1);
    std::string win_rendered = epg_single.render();
    TEST_ASSERT(win_rendered.find("Bom Dia Brasil") != std::string::npos);
    TEST_ASSERT(win_rendered.find("Bom Dia São Paulo") == std::string::npos);
    TEST_ASSERT(win_rendered.find("Mais Você") == std::string::npos);

    isdbt::AsciiTable epg_all({"Canal", "Emissora", "Horario", "Classif.", "Genero", "Programa Atual (Agora)"});
    epg_all.set_alignments({isdbt::Alignment::Center, isdbt::Alignment::Left, isdbt::Alignment::Center, isdbt::Alignment::Center, isdbt::Alignment::Center, isdbt::Alignment::Left});
    epg_all.set_column_widths({5, 18, 15, 8, 12, 36});
    TEST_ASSERT(epg_all.total_width() == 113);

    epg_all.add_row({"18", "Globo HD", "03:00 - 05:30", "--", "--", "Bom Dia São Paulo"});
    epg_all.add_row({"40", "RIT HD", "04:00 - 05:20", "--", "--", "Igreja da Graça no seu Lar"});

    std::string all_rendered = epg_all.render();
    std::stringstream all_ss(all_rendered);
    while (std::getline(all_ss, line)) {
        if (line.empty()) continue;
        TEST_ASSERT(isdbt::AsciiTable::utf8_display_width(line) == 113);
    }

    std::cout << "[PASS] Formatador AsciiTable aprovado com sucesso.\n";
    return 0;
}
