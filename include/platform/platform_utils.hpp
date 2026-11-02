#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace isdbt {

extern std::atomic<bool> g_running;

// Exibe o cabecalho oficial em ASCII com URL do repositorio e informacoes de licenca
void print_cli_header();

// Configura o console e os manipuladores de sinais (SIGINT/SIGTERM/Ctrl+C)
void setup_platform();

// Encerra qualquer processo de player (MPV/VLC) associado
void stop_player();

// Dispara o MPV Player integrado configurado com interpolacao de resolucao e movimento em tempo real
bool launch_mpv(uint16_t udp_port);

// Envia comando de reload via IPC para o MPV recarregar o stream UDP sem fechar a janela
bool reload_mpv_player(uint16_t udp_port = 1234);

// Entrada de teclado interativa nao-bloqueante
bool console_kbhit();
int console_getch();

} // namespace isdbt
