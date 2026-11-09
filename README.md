# ISDB-T / ISDB-Tb Tuner Player

<p align="center">
  <img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License: GPLv3" />
  <img src="https://img.shields.io/badge/Windows-x64%20%7C%20x86-brightgreen.svg" alt="Windows x64 | x86" />
  <img src="https://img.shields.io/badge/Hardware-RTL--SDR-red.svg" alt="RTL-SDR" />
  <img src="https://img.shields.io/badge/Tests-27%2F27%20Passed-success.svg" alt="CTest Passing" />
</p>

<p align="center"><b>🇧🇷 Português</b> | <a href="README_en.md">🇺🇸 English</a></p>

Assista TV Digital aberta (ISDB-T / ISDB-Tb / SBTVD, sinal **1-Seg**) no computador com um dongle USB **RTL-SDR** barato. Programa portátil: baixe, descompacte e use, com player MPV já incluso.

## 1. Download
Baixe o `.zip` mais recente na aba **[Releases](https://github.com/lucasbrigida/isdb_t_isdb_tb_tuner_player/releases)**:

| Arquivo | Para |
|---|---|
| `*_win64.zip` | Windows 64 bits (a maioria dos PCs) |
| `*_win32.zip` | Windows 32 bits (PCs antigos) |

Vem tudo dentro: `isdbtb_rx.exe`, DLLs do RTL-SDR, `zadig.exe` (driver USB) e o player MPV em `tools\mpv\`. Não precisa instalar nada.

## 2. O que você precisa
- Dongle RTL-SDR (RTL2832U com sintonizador R820T, R828D ou E4000).
- Antena UHF (de preferência perto da janela ou externa).
- Windows 10 (1607+) ou 11. O MPV incluso não roda em Windows 7/8.

## 3. Primeiro uso
1. **Instale o driver (só na primeira vez):** conecte o dongle, abra `zadig.exe` → **Options → List All Devices** → escolha **`Bulk-In, Interface (Interface 0)`** (ou `RTL2838UHIDIR`) → driver **WinUSB** → **Install/Replace Driver**.
   > ⚠ Se aparecerem Interface 0 e Interface 1, use sempre a **Interface 0**.
2. **Procure os canais:** abra o PowerShell na pasta descompactada e rode:
   ```powershell
   .\isdbtb_rx.exe scan
   ```
3. **Assista:**
   ```powershell
   .\isdbtb_rx.exe play -c 23 --player
   ```
   Troque `23` por um canal encontrado no `scan` (veja com `list`).

## 4. Comandos
| Comando | Para que serve |
|---|---|
| `probe` | Verifica se o dongle foi reconhecido |
| `scan` | Procura emissoras e salva em `channels.json`, com o nome de cada uma |
| `list` | Mostra os canais salvos e a força do sinal |
| `play` | Sintoniza um canal e abre o vídeo |
| `epg` | Mostra a programação das emissoras |

```powershell
.\isdbtb_rx.exe probe                          # o dongle está funcionando?
.\isdbtb_rx.exe scan                           # procurar canais (UHF 14 a 45)
.\isdbtb_rx.exe scan -e 69                     # procurar até o canal 69
.\isdbtb_rx.exe list                           # ver canais salvos
.\isdbtb_rx.exe play -c 23 --player            # assistir ao canal 23
.\isdbtb_rx.exe play -n GLOBO --player         # assistir pelo nome
.\isdbtb_rx.exe play -c 23 --ofdm 2 --player   # sinal fraco ou com chuvisco
.\isdbtb_rx.exe play -c 23 --player -v         # mostrar qualidade do sinal
.\isdbtb_rx.exe epg                            # programação de todos os canais
.\isdbtb_rx.exe epg --view                     # programação salva, sem usar o rádio
```

Opções principais do `play`:

| Opção | Efeito |
|---|---|
| `-c <n>` / `-n <nome>` | Canal por número ou nome (sem nenhum: primeiro canal salvo) |
| `--player` | Abre o MPV integrado |
| `--ofdm 2` | Modo reforçado para sinal fraco, reflexos ou em movimento (usa mais CPU) |
| `-g <dB>` | Ganho da antena (padrão 28; longe da torre 38–49.6; perto 20) |
| `-v` | Mostra RSSI, SNR e pacotes recebidos em tempo real |
| `-p <porta>` | Porta UDP (padrão 1234) |

Todas as opções: [Referência da CLI](docs/user/cli_reference.md).

## 5. Teclas
| Onde | Tecla | Ação |
|---|---|---|
| Terminal (`play`) | `▲`/`▼`, `Enter` | Escolher e sintonizar outro canal salvo |
| Terminal | `q` ou `Ctrl+C` | Sair (o MPV fecha junto) |
| MPV | `f` | Tela cheia |
| MPV | `i` | Estatísticas do vídeo |
| MPV | `m` / `9` / `0` | Mudo / volume − / volume + |
| MPV | `b` | Liga/desliga o filtro anti-blocos (deband) |

Prefere VLC? Rode o `play` sem `--player` e abra **Mídia → Abrir Fluxo de Rede** → `udp://@127.0.0.1:1234`.

## 6. Problemas comuns
| Sintoma | Solução |
|---|---|
| `probe` não encontra o dongle | Refaça o Zadig (Interface 0 + WinUSB); use uma porta USB traseira, sem hub |
| `scan` não acha canais | Antena perto da janela; tente `scan -e 69`; confira os canais da sua cidade |
| Imagem preta ou travando | Aguarde `TMCC: LOCK` (1–2 s); tente `--ofdm 2` ou ajuste `-g` |
| `[R82XX] PLL not locked!` | Reconecte o dongle direto na placa-mãe; verifique o cabo da antena |

Mais detalhes: [Guia do Usuário](docs/USER_GUIDE.md).

## 7. Demonstração
| `scan` | `play` |
|:---:|:---:|
| <img src="docs/videos/scan_poster.png" alt="scan" width="100%" /><br>Varredura com nome das emissoras | <img src="docs/videos/play_poster.png" alt="play" width="100%" /><br>Canal ao vivo no MPV integrado |
| **demonstração** | **`probe`** |
| <img src="docs/videos/demo_poster.png" alt="demo" width="100%" /><br>Recepção e reprodução contínuas | <img src="docs/images/probe.png" alt="probe" width="100%" /><br>Diagnóstico do dongle |

## 8. Para desenvolvedores
Decodificador nativo em C++17, sem GNU Radio e sem bibliotecas externas de DSP (FFT e SIMD próprios, com a API de FFTW3/VOLK). Tem dois sincronizadores OFDM (Van de Beek e SP-CIR), equalização MMSE, Viterbi com ponderação CSI, Reed-Solomon com tabela GF(2^8) O(1), fila de captura SPSC lock-free e zero alocação no laço de DSP. Compila para x64 e x86.

- [Compilação, testes e release](docs/user/installation.md)
- [Índice da documentação](docs/INDEX.md): DSP, mapa do código, testes
- [Decisões de arquitetura (ADRs)](docs/adr/README.md)
- [Regras para agentes de IA](AGENTS.md)

## Créditos
[gr-isdbt](https://github.com/git-artes/gr-isdbt) (referência ISDB-T), [librtlsdr](https://github.com/steve-m/librtlsdr), [MPV](https://github.com/mpv-player/mpv), [Zadig/libwdi](https://github.com/pbatard/libwdi), [VOLK](https://github.com/gnuradio/volk), [FFTW3](https://www.fftw.org/), [CLI11](https://github.com/CLIUtils/CLI11), [nlohmann/json](https://github.com/nlohmann/json).

## Licença
[GPL-3.0-or-later](LICENSE). Componentes de terceiros mantêm suas licenças.
