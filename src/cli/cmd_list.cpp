#include "cli/commands.hpp"
#include "cli/channel_manager.hpp"
#include "radio/frequency_plan.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>

namespace isdbt {

int cmd_list() {
    ChannelManager mgr;
    const auto& list = mgr.get_channels();

    std::cout << "\033[1;36m=== Lista de Canais ISDB-T Armazenados (" << list.size() << ") ===\033[0m\n\n";
    if (list.empty()) {
        std::cout << "Nenhum canal salvo ainda. Execute 'isdbtb_rx scan' para sintonizar.\n\n";
        return 0;
    }

    std::cout << " Canal |  Frequencia  |    Forca do Sinal    |  Potencia  | Codecs (V/A) | Nome / Emissora\n";
    std::cout << "-------+--------------+----------------------+------------+--------------+----------------\n";
    for (const auto& ch : list) {
        auto rating = get_signal_rating(ch.rssi_dbfs);
        std::string raw_signal = rating.bar + " " + rating.text;
        std::string colored_signal = rating.ansi_color + pad_string(raw_signal, 20) + "\033[0m";

        std::stringstream freq_ss;
        freq_ss << std::fixed << std::setprecision(3) << (ch.frequency_hz / 1e6) << " MHz";

        std::stringstream pwr_ss;
        pwr_ss << std::fixed << std::setprecision(1) << ch.rssi_dbfs << " dBFS";

        std::string codecs = ch.video_codec + "/" + ch.audio_codec;

        std::cout << " " << std::setw(5) << ch.channel_number << " | "
                  << std::setw(12) << freq_ss.str() << " | "
                  << colored_signal << " | "
                  << std::setw(10) << pwr_ss.str() << " | "
                  << std::setw(12) << codecs << " | \033[1;32m"
                  << ch.name << "\033[0m\n";
    }
    std::cout << "\n\033[1;30mLegenda de Forca do Sinal: [#####] Alto (Forte) >= -6 dBFS | [#### ] Normal (Bom) >= -12 dBFS | [###  ] Moderado | [##   ] Fraco\033[0m\n\n";
    return 0;
}

} // namespace isdbt

