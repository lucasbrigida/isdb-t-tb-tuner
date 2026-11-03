#pragma once

#include <string>
#include <cstdint>

namespace isdbt {

// 1. Subcomando PROBE: Inspeciona os dongles RTL-SDR conectados e o ambiente DSP
int cmd_probe();

// 2. Subcomando SCAN: Executa a varredura profunda dos canais UHF globais
int cmd_scan(int start_ch, int end_ch, int dev_idx);

// 3. Subcomando LIST: Lista os canais sintonizados armazenados em channels.json
int cmd_list();

// 4. Subcomando PLAY: Sintoniza um canal e transmite via UDP para o player
int cmd_play(int channel_number, const std::string& name_query, uint16_t udp_port,
             int dev_idx, bool test_pattern = false, double gain_db = 28.0, bool auto_player = false,
             bool verbose = false, int ofdm_mode = 1, const std::string& input_file = "",
             const std::string& output_ts_file = "", bool headless = false);

} // namespace isdbt
