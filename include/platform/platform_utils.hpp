#pragma once

#include <atomic>
#include <cstdint>

namespace isdbt {

extern std::atomic<bool> g_running;

// Configura o console (ANSI escape codes no Windows) e os manipuladores de sinais (SIGINT/SIGTERM/Ctrl+C)
void setup_platform();

// Encerra qualquer processo de player (MPV/VLC) associado
void stop_player();

// Dispara o MPV Player integrado configurado com interpolacao de resolucao e movimento em tempo real
bool launch_mpv(uint16_t udp_port);

} // namespace isdbt

