#pragma once

#include <cstdint>
#include <string>

namespace isdbt {

// Converte o nivel dBFS em uma descricao simples e intuitiva de forca de sinal
struct SignalRating {
    std::string text;       // "Alto (Forte)", "Normal (Bom)", "Fraco", etc.
    std::string ansi_color; // Verde, Ciano, Amarelo, Vermelho
    std::string bar;        // "[#####]", "[#### ]", etc.
};

// Frequencia central do canal UHF ISDB-T / ISDB-Tb (Brasil / Internacional / Japao)
// Canal 14 Brasil = 473.142857 MHz (Canal N = 470 + (N-14)*6 + 3 + 1/7 MHz)
uint32_t get_channel_freq(int channel_number);

// Valida se o canal esta na faixa UHF permitida (14 a 69)
bool is_valid_channel(int channel_number);

// Avalia a potencia recebida (RSSI) e retorna descricao amigavel
SignalRating get_signal_rating(float rssi_dbfs);

// Funcao auxiliar para formatar strings com largura fixa no terminal
std::string pad_string(const std::string& str, size_t width, bool left_align = true);

} // namespace isdbt

