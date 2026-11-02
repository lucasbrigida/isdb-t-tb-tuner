#include "cli/commands.hpp"
#include "cli/channel_manager.hpp"
#include "platform/platform_utils.hpp"
#include "radio/frequency_plan.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>

namespace isdbt {

int cmd_list() {
    print_cli_header();
    ChannelManager mgr;
    const auto& list = mgr.get_channels();

    std::cout << "=== Lista de Canais ISDB-T Armazenados (" << list.size() << ") ===\n\n";
    if (list.empty()) {
        std::cout << "Nenhum canal salvo ainda. Execute 'isdbtb_rx scan' para sintonizar.\n\n";
        return 0;
    }

    std::cout << " Canal |  Frequencia  |    Forca do Sinal    |  Potencia  | Codecs (V/A) | Nome / Emissora\n";
    std::cout << "-------+--------------+----------------------+------------+--------------+----------------\n";
    for (const auto& ch : list) {
        auto rating = get_signal_rating(ch.rssi_dbfs);
        std::string raw_signal = rating.bar + " " + rating.text;
        std::string formatted_signal = pad_string(raw_signal, 20);

        std::stringstream freq_ss;
        freq_ss << std::fixed << std::setprecision(3) << (ch.frequency_hz / 1e6) << " MHz";

        std::stringstream pwr_ss;
        pwr_ss << std::fixed << std::setprecision(1) << ch.rssi_dbfs << " dBFS";

        std::string codecs = ch.video_codec + "/" + ch.audio_codec;

        std::cout << " " << std::setw(5) << ch.channel_number << " | "
                  << std::setw(12) << freq_ss.str() << " | "
                  << formatted_signal << " | "
                  << std::setw(10) << pwr_ss.str() << " | "
                  << std::setw(12) << codecs << " | "
                  << ch.name << "\n";
    }
    std::cout << "\nLegenda de Forca do Sinal: [#####] Alto (Forte) >= -6 dBFS | [#### ] Normal (Bom) >= -12 dBFS | [###  ] Moderado | [##   ] Fraco\n\n";
    return 0;
}

} // namespace isdbt
