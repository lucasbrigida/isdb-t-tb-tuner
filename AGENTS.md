# 🤖 Guia de Orientação para Multi-Agentes de IA (AGENTS.md)

Este documento foi elaborado especificamente para orientar **agentes de Inteligência Artificial** (e desenvolvedores humanos) que forem continuar, manter ou expandir o desenvolvimento deste decodificador universal ISDB-T/ISDB-Tb.

---

## 🎯 1. Visão Geral e Escopo do Projeto

O objetivo deste projeto é fornecer uma aplicação de linha de comando (CLI) nativa, de alta performance, sem dependências do framework GNU Radio, capaz de:
1. Descobrir e controlar dongles **RTL-SDR** (RTL2832U com tuners R820T/R828D/E4000).
2. Sintonizar e decodificar sinais de TV Digital no padrão **ISDB-T** (Japão, Filipinas, etc.) e **ISDB-Tb / SBTVD** (Brasil e América Latina).
3. Realizar o processamento completo de Camada Física (DSP) e Camada de Aplicação (PSI/SI, gerenciamento de canais e streaming UDP para VLC).

> [!IMPORTANT]
> **Foco de Camada Física:** O projeto tem como escopo principal o modo **1-Seg (One-Seg)** móvel (~428.57 kHz de largura útil). O RTL-SDR possui limite seguro de amostragem de ~2.4 MSps (largura útil ~2.0 MHz), o que inviabiliza amostrar os 5.7 MHz de um sinal Full-Seg (13 segmentos) sem perda drástica de pacotes USB. Se for estender para Full-Seg, utilize hardware SDR de banda larga (HackRF, Airspy, LimeSDR, USRP).

---

## 📐 2. Invariantes Arquiteturais Críticos ("Não Quebre")

Qualquer agente que editar o código-fonte deve respeitar estritamente as seguintes invariantes:

### 1. Taxa de Amostragem Exata (\(2031746\text{ S/s}\))
- A taxa de chip base do padrão ISDB-T é exatamente:
  \[
  F_{base} = \frac{512}{63}\text{ MHz} \approx 8.126984\text{ MHz}
  \]
- Para o 1-Seg com RTL-SDR, operamos no sub-múltiplo exato de \(1/4\):
  \[
  F_s = \frac{F_{base}}{4} = \frac{512}{63 \times 4}\text{ MHz} \approx 2.031746\text{ MSps} = 2031746\text{ Hz}
  \]
- **Regra:** **NUNCA** mude a taxa para \(2.048\text{ MSps}\) fixos sem reamostragem racional. A taxa de \(2031746\text{ S/s}\) alinha perfeitamente o número de pontos de FFT por símbolo (Mode 3 = 2048 pontos, Mode 2 = 1024 pontos, Mode 1 = 512 pontos) e garante que o pico de correlação do prefixo cíclico trave com \(>90\%\) de correlação!

### 2. Pacotes UDP para VLC (\(7 \times 188 = 1316\text{ bytes}\))
- Cada datagrama UDP enviado para `udp://@127.0.0.1:1234` **DEVE** conter exatamente 7 pacotes MPEG-TS completos de 188 bytes (\(7 \times 188 = 1316\text{ bytes}\)).
- **Motivo:** 1316 bytes é o tamanho padrão da indústria de IPTV que cabe no MTU de 1500 bytes da Ethernet sem causar fragmentação de pacotes no kernel IP.
- **Regra:** Nunca envie pacotes soltos de 188 bytes via UDP individualmente (causa sobrecarga severa de interrupções de rede e travamentos no VLC).

### 3. Modo Binário no Windows (`_setmode`)
- No Windows, a saída padrão (`stdout`) e arquivos binários abrem em modo texto por padrão (onde bytes `0x0A` (`\n`) viram `0x0D 0x0A` (`\r\n`)).
- **Regra:** Se for redirecionar pacotes TS para pipes (`stdout`), `Platform::set_binary_stdout()` (`_setmode(_fileno(stdout), _O_BINARY)`) deve permanecer ativo.

### 4. Persistência de Canais em JSON
- Windows: `%APPDATA%\isdbtb_rx\channels.json`
- Linux: `~/.config/isdbtb_rx/channels.json`
- **Regra:** Use sempre a classe `ChannelManager` e `std::filesystem` para manipular caminhos.

---

## 🛠️ 3. Como Compilar e Rodar os Testes

O ambiente no Windows utiliza **GCC 16.2 / G++ 16.2** e **CMake** (`w64devkit`):

```powershell
# Reconfigurar o CMake
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

# Compilar todos os executáveis
cmake --build build -j

# Executar toda a suíte de testes unitários determinísticos
.\build\test_reed_solomon.exe
.\build\test_descrambler.exe
.\build\test_viterbi.exe
.\build\test_fec_pipeline.exe
.\build\test_ofdm_sync.exe
```

---

## 🗺️ 4. Mapa de Módulos do Código

| Módulo | Arquivos | Responsabilidade |
|---|---|---|
| **OFDM Sync** | `include/dsp/ofdm_sync.hpp`<br>`src/dsp/ofdm_sync.cpp` | Autocorrelação de prefixo cíclico (Van de Beek), correção de FFO via SIMD VOLK, FFTW3. |
| **Reed-Solomon** | `include/dsp/reed_solomon.hpp`<br>`src/dsp/reed_solomon.cpp` | Decodificador \(RS(204, 188, t=8)\) com Berlekamp-Massey, Chien Search e Forney. |
| **Descrambler** | `include/dsp/descrambler.hpp`<br>`src/dsp/descrambler.cpp` | LFSR de 15 bits (\(1 + X^{14} + X^{15}\)), reset por sync `0xB8`, descrambler de payload. |
| **Viterbi** | `include/dsp/viterbi_decoder.hpp`<br>`src/dsp/viterbi_decoder.cpp` | Inner FEC convolucional \(K=7\) com matriz Trellis de 64 estados e puncionamento (1/2, 2/3, 3/4). |
| **Streaming UDP** | `include/streaming/udp_streamer.hpp`<br>`src/streaming/udp_streamer.cpp` | Sockets WinSock2/POSIX enviando datagramas de 1316B para VLC. |
| **PSI/SI Parser** | `include/mpegts/psi_parser.hpp`<br>`src/mpegts/psi_parser.cpp` | Parser de PAT (PID 0), PMT e SDT (PID 0x0011) para capturar o nome da emissora. |
| **Channel Manager** | `include/cli/channel_manager.hpp`<br>`src/cli/channel_manager.cpp` | Gerencia `channels.json` e exportação `.m3u`. |
| **Main CLI** | `src/main.cpp` | Interface CLI11 com comandos `probe`, `scan`, `list`, `export` e `play`. |

---

## 🚀 5. Backlog e Sugestões para Futuros Agentes

Se você for um subagente encarregado de continuar o projeto, aqui estão as tarefas prioritárias recomendadas:

### Tarefa A: Mapeador de Constelação & Equalizador Dinâmico (Channel Estimator)
- **Local:** `include/dsp/channel_equalizer.hpp`
- **Objetivo:** Implementar interpolação 2D (tempo/frequência) dos Pilotos Dispersos (Scattered Pilots - SP) e Pilotos Contínuos (Continual Pilots - CP) definidos na norma ABNT NBR 15601.
- **Saída:** Compensação de desvanecimento multipercurso (fading) para alimentar métricas soft LLR precisas ao Viterbi.

### Tarefa B: Decodificador TMCC Avançado (Transmission and Multiplexing Configuration Control)
- **Local:** `include/dsp/tmcc_decoder.hpp`
- **Objetivo:** Decodificar os 204 símbolos do bloco de sinalização TMCC (portadoras BPSK dedicadas) para detectar automaticamente:
  - Modulação do segmento 0 (DQPSK, QPSK, 16QAM).
  - Taxa de código (1/2, 2/3, 3/4, 5/6, 7/8).
  - Profundidade do desentrelaçador de tempo (\(I \in \{0, 1, 2, 4, 8, 16\}\)).

### Tarefa C: Suporte a SDRs de Banda Larga (Full-Seg HDTV)
- **Local:** `include/hardware/sdr_device.hpp`
- **Objetivo:** Criar classe base polimórfica para suportar SoapySDR ou HackRF/Airspy, permitindo amostragem a 8.126984 MSps e decodificação dos 13 segmentos (sinal HDTV 1080i).

