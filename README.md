# 📡 ISDB-T / ISDB-Tb Universal SDR Receiver & Real-Time Player

<p align="center">
  <img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License: GPLv3" />
  <img src="https://img.shields.io/badge/C%2B%2B-17%20%2F%2020-00599C.svg?logo=c%2B%2B" alt="C++17/20" />
  <img src="https://img.shields.io/badge/Hardware-RTL--SDR%20(RTL2832U)-red.svg" alt="RTL-SDR" />
  <img src="https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-brightgreen.svg" alt="Platforms" />
  <img src="https://img.shields.io/badge/Player-MPV%20Embedded-darkgreen.svg" alt="MPV Embedded" />
  <img src="https://img.shields.io/badge/Tests-6%2F6%20Passed-success.svg" alt="CTest Passing" />
  <img src="https://img.shields.io/badge/Tests-10%2F10%20Passed-success.svg" alt="CTest Passing" />
</p>

Receptor e decodificador digital completo de televisão aberta no padrão **ISDB-T / ISDB-Tb / SBTVD** (Brasil, Japão, Filipinas e América Latina) para dispositivos **RTL-SDR** (RTL2832U com tuners R820T/R828D/E4000). Desenvolvido em **C++ nativo de alta performance sem dependências do framework GNU Radio**, com equalização adaptativa **MMSE**, ponderação **CSI** no decodificador Viterbi, varredura inteligente de canais com identificação de nomes via **SDT** e **reprodutor MPV embutido com super-resolução e interpolação de movimento em tempo real**.
Receptor e decodificador digital completo de televisão aberta no padrão **ISDB-T / ISDB-Tb / SBTVD** (Brasil, Japão, Filipinas e América Latina) para dispositivos **RTL-SDR** (RTL2832U com tuners R820T/R828D/E4000). Desenvolvido em **C++ nativo de alta performance sem dependências do framework GNU Radio**, com fila de captura **SPSC lock-free** com detecção atômica de descontinuidades, **zero alocações no heap** no processamento de símbolos, máquina de estados de sincronismo OFDM tripartite (**Acquisition / Tracking / LostLock**), equalização adaptativa **MMSE regularizada**, ponderação **CSI** sincronizada por linhas de retardo no decodificador Viterbi, multiplicação em $GF(2^8)$ em **$O(1)$** via tabela de 64 KB no Reed-Solomon, varredura inteligente de canais com identificação de nomes via **SDT** e **reprodutor MPV embutido com super-resolução e interpolação de movimento em tempo real**.

---

<p align="center">
  <b>Idioma / Language:</b><br/>
  <b>🇧🇷 Português (Brasil)</b> &nbsp;|&nbsp; 
  <a href="README_en.md">🇺🇸 English</a>
</p>

---

## 🎬 Demonstrações em Tempo Real & Inspeção de Hardware

Confira abaixo o receptor em execução em seus diferentes modos operacionais. Clique nas miniaturas para assistir aos vídeos ou visualizar a imagem em resolução original:

|                                                                                                                                                                                                   📡 **Varredura Automática (`scan`)**                                                                                                                                                                                                    |                                                                                                                                                                                                   📺 **Sintonia e Reprodução (`play`)**                                                                                                                                                                                                   |
| :---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------: | :---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------: |
|         <a href="docs/videos/scan.mp4" title="Clique para assistir ao vídeo de varredura"><img src="docs/videos/scan_poster.png" alt="Varredura de canais scan" width="100%" style="border-radius: 6px; border: 1px solid #333;" /></a><br>▶️ <strong>Vídeo:</strong> <em>Varredura rápida em 2 passos (UHF 14–69) com decodificação SDT de emissoras (<a href="docs/videos/scan.mp4"><code>docs/videos/scan.mp4</code></a>)</em>         |  <a href="docs/videos/play.mp4" title="Clique para assistir ao vídeo de reprodução"><img src="docs/videos/play_poster.png" alt="Sintonia e reprodução play" width="100%" style="border-radius: 6px; border: 1px solid #333;" /></a><br>▶️ <strong>Vídeo:</strong> <em>Sintonia ao vivo com MPV integrado, upscaling EWA-Lanczos e interpolação de movimento (<a href="docs/videos/play.mp4"><code>docs/videos/play.mp4</code></a>)</em>   |
|                                                                                                                                                                                                   **🎬 Demonstração Completa (`demo`)**                                                                                                                                                                                                   |                                                                                                                                                                                                 **🔍 Diagnóstico de Hardware (`probe`)**                                                                                                                                                                                                  |
| <a href="docs/videos/demo.mp4" title="Clique para assistir à demonstração completa"><img src="docs/videos/demo_poster.png" alt="Demonstração completa em tempo real demo" width="100%" style="border-radius: 6px; border: 1px solid #333;" /></a><br>▶️ <strong>Vídeo:</strong> <em>Demodulação de sinal de RF a 2.0317 MSps e reprodução contínua da BAND HD (<a href="docs/videos/demo.mp4"><code>docs/videos/demo.mp4</code></a>)</em> | <a href="docs/images/probe.png" title="Clique para ampliar a imagem de diagnóstico"><img src="docs/images/probe.png" alt="Diagnóstico de hardware probe" width="100%" style="border-radius: 6px; border: 1px solid #333;" /></a><br>📷 <strong>Imagem:</strong> <em>Inspeção do dongle RTL-SDR, sintonizador R820T, tabela de ganhos e aceleração VOLK SIMD (<a href="docs/images/probe.png"><code>docs/images/probe.png</code></a>)</em> |

---

## 📑 Índice (Sumário)

- [⚡ 1. Guia Rápido para Usuários Finais (Download & Execução)](#-1-guia-rápido-para-usuários-finais-download--execução)
  - [1.1 Download da Release Autônoma](#11-download-da-release-autônoma)
  - [1.2 Instalação do Driver USB (Zadig em 1 Clique)](#12-instalação-do-driver-usb-zadig-em-1-clique)
  - [1.3 Comandos Essenciais da CLI](#13-comandos-essenciais-da-cli)
  - [1.4 Recursos e Teclas do Player MPV Integrado](#14-recursos-e-teclas-do-player-mpv-integrado)
- [🛠️ 2. Guia para Desenvolvedores (Compilação & Testes)](#️-2-guia-para-desenvolvedores-compilação--testes)
  - [2.1 Pré-requisitos & Toolchain Recomendado (w64devkit)](#21-pré-requisitos--toolchain-recomendado-w64devkit)
  - [2.2 Como Compilar Nativamente](#22-como-compilar-nativamente)
  - [2.3 Suíte de Testes Determinísticos (CTest)](#23-suíte-de-testes-determinísticos-ctest)
  - [2.4 Automação de Releases (`build_release.ps1`)](#24-automação-de-releases-build_releaseps1)
  - [2.5 Ferramentas de Desenvolvimento e Diagnóstico (`tools/`)](#25-ferramentas-de-desenvolvimento-e-diagnóstico-tools)
  - [2.6 Mapa da Arquitetura do Código](#26-mapa-da-arquitetura-do-código)
- [🔬 3. Camada Física (DSP) & Inovações Matemáticas](#-3-camada-física-dsp--inovações-matemáticas)
  - [3.1 Resumo dos Algoritmos de Camada Física](#31-resumo-dos-algoritmos-de-camada-física)
  - [3.2 Documentação Técnica Especializada](#32-documentação-técnica-especializada)
- [🤝 4. Projetos de Referência & Agradecimentos](#-4-projetos-de-referência--agradecimentos)
- [📜 5. Licença](#-5-licença)

---

## ⚡ 1. Guia Rápido para Usuários Finais (Download & Execução)

Se você deseja apenas sintonizar e assistir à TV Digital no seu computador com o RTL-SDR, siga os passos abaixo:

### 1.1 Download da Release Autônoma

Baixe o arquivo `.zip` da versão estável mais recente na pasta de lançamentos:

- 📦 **Windows x64:** [`releases/windows/20261101_v1.0.0.zip`](releases/windows/20261101_v1.0.0.zip) _(ou na aba [Releases do GitHub](../../releases))_
- Descompacte o arquivo `.zip` em qualquer pasta do seu computador. Todos os componentes (executável, bibliotecas DLL, utilitário Zadig e player MPV com shaders) já estão inclusos.

### 1.2 Instalação do Driver USB (Zadig em 1 Clique)

Para que o decodificador converse diretamente com o chip RTL2832U:

1. Conecte o dongle RTL-SDR e a antena UHF a uma porta USB do computador.
2. Dê um duplo clique no arquivo **`zadig.exe`** (incluso na pasta descompactada).
3. No menu **Options**, clique em **List All Devices**.
4. No menu suspenso, selecione **`Bulk-In, Interface (Interface 0)`** _(ou `RTL2832U` / `RTL2838UHIDIR`)_.
   > ⚠️ **Atenção:** Se houver `Interface 0` e `Interface 1`, selecione **sempre a `Interface 0`** (o barramento de alta velocidade do sinal de RF).
5. Certifique-se de que o driver alvo à direita da seta seja **`WinUSB`** e clique em **`Install Driver`** (ou `Replace Driver`).

### 1.3 Comandos Essenciais da CLI

Abra o PowerShell ou Prompt de Comando (CMD) na pasta descompactada:

```powershell
# 1. Inspecionar se o dongle RTL-SDR foi reconhecido e exibir ganhos:
.\isdbtb_rx.exe probe

# 2. Varrer os canais do ar na faixa UHF (canais 14 a 69):
#    (A varredura inteligente em dois passos identifica o nome oficial da emissora via SDT)
.\isdbtb_rx.exe scan

# 3. Listar a grade de canais salvos:
.\isdbtb_rx.exe list

# 4. Sintonizar um canal (ex: Canal 23) e abrir automaticamente o player MPV integrado:
.\isdbtb_rx.exe play -c 23 --player

# 5. Sintonizar com exibição do status detalhado de sinal em tempo real (-v / --verbose):
.\isdbtb_rx.exe play -c 23 --player -v
```

> 💡 **Modo Detalhado (`-v`, `--verbose`):** Por padrão, a interface do comando `play` mantém um visual limpo e estático. Use `-v` ou `--verbose` para exibir a linha de telemetria e status em tempo real (RSSI, lock OFDM/TMCC, SNR, pacotes TS e taxas de correção RS).

> 💡 **Encerramento Sincronizado:** O player MPV é gerenciado por um _Job Object_ do kernel do Windows. Ao fechar o terminal da CLI ou pressionar `Ctrl+C`, o player fecha instantaneamente sem deixar processos órfãos em segundo plano.

### 1.4 Recursos e Teclas do Player MPV Integrado

O reprodutor MPV embutido inclui um pipeline gráfico pré-configurado (`tools/mpv/mpv.conf`) especialmente desenhado para transmissões móveis 1-Seg:

- **Super-Resolução Espacial:** Filtro `ewa_lanczossharp` com `sigmoid-upscaling`, gerando bordas limpas e sem dentes de serra ao ampliar para 1080p/4K.
- **Debanding Dinâmico:** Elimina artefatos de compressão e blocos de cor típicos do vídeo H.264 em baixa taxa de bits.
- **Interpolação de Movimento (`display-resample`):** Sincroniza a taxa do vídeo com a frequência de atualização do seu monitor (60Hz, 120Hz, 144Hz ou 240Hz), eliminando qualquer engasgo (_judder_).

| Tecla de Atalho | Ação no Player MPV                                                            |
| :-------------: | ----------------------------------------------------------------------------- |
|     **`f`**     | Alternar modo **Tela Cheia** (_Fullscreen_)                                   |
|     **`i`**     | Exibir painel OSD com estatísticas em tempo real, resolução e taxa de quadros |
|     **`d`**     | Ativar / Desativar o filtro de Deband                                         |
|     **`m`**     | Ativar / Desativar mudo (_Mute_)                                              |
|  **`9` / `0`**  | Diminuir / Aumentar volume                                                    |
|     **`q`**     | Fechar o player                                                               |

---

## 🛠️ 2. Guia para Desenvolvedores (Compilação & Testes)

### 2.1 Pré-requisitos & Toolchain Recomendado (w64devkit)

Para compilação nativa no Windows com máxima velocidade e sem a sobrecarga de ambientes pesados como MSYS2 ou Visual Studio, o projeto adota e recomenda oficialmente o **[`w64devkit`](https://github.com/skeeto/w64devkit/releases)**:

- **O que é o [`w64devkit`](https://github.com/skeeto/w64devkit/releases):** Uma distribuição portátil, autocontida e de inicialização instantânea do compilador GCC/G++ para Windows x64 mantida por Christopher Wellons (_skeeto_). Inclui GDB, GNU Make, BusyBox (com shell POSIX `sh`, `awk`, `sed`, `grep`) e cabeçalhos MinGW-w64 com suporte nativo a C++17 e C++20.
- **Como configurar o w64devkit em 2 minutos:**
  1. Baixe o pacote mais recente (`w64devkit-x.y.z.zip`) na página oficial de releases: [github.com/skeeto/w64devkit/releases](https://github.com/skeeto/w64devkit/releases).
  2. Extraia o conteúdo para uma pasta no seu computador (ex: `C:\w64devkit`).
  3. Adicione o caminho `C:\w64devkit\bin` à variável de ambiente `PATH` do sistema (ou abra o terminal através do atalho `w64devkit.exe`).
- **Sistema de Compilação:** [CMake 3.20+](https://cmake.org/download/) instalado e adicionado ao `PATH`.
- **Bibliotecas de Aceleração e Rádio:**
  - `librtlsdr` e `libusb-1.0` (cabeçalhos e bibliotecas de importação inclusos no repositório em `deps/`).
  - `FFTW3` (transformadas rápidas de Fourier de 2048, 4096 e 8192 pontos).
  - `VOLK` (operações matemáticas vetoriais com suporte a AVX2, FMA e SSE).
- _(Opcional para automação e testes):_ Python 3.8+ e PowerShell 7+.

### 2.2 Como Compilar Nativamente

### 2.2 Como Compilar Nativamente & Opções de Otimização

No terminal do Windows com ambiente MinGW-w64 (ex: `w64devkit`):

```powershell
# 1. Configurar o projeto com CMake em modo Release (-O3, -DNDEBUG e IPO/LTO habilitados automaticamente)
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

# 2. Compilar o executável principal e a suíte de testes unitários
# Opcional: habilitar otimização de ponto flutuante rápido (-ffast-math / /fp:fast)
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DISDBT_ENABLE_FAST_MATH=ON

# 3. Compilar o executável principal e a suíte completa de 10 testes unitários
cmake --build build -j
```

### 2.3 Suíte de Testes Determinísticos (CTest)

#### Opções de Compilação no CMake

- **`-DCMAKE_BUILD_TYPE=Release`**: Ativa otimização `-O3 -DNDEBUG` (GCC/Clang) ou `/O2 /DNDEBUG` (MSVC), desativando assertions em runtime.
- **`-DISDBT_ENABLE_FAST_MATH=ON`** _(Padrão: OFF)_: Adiciona `-ffast-math` ou `/fp:fast` para acelerar vetorizações AVX2/FMA no cálculo de métricas e NCO.
- **Interprocedural Optimization (IPO / LTO)**: Detectado e habilitado automaticamente para builds de Release caso suportado pelo compilador (`CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON`).

### 2.3 Suíte de Testes Determinísticos (CTest - 14/14 Testes)

O projeto conta com testes unitários matemáticos que rodam em frações de segundo e não dependem de rádio físico conectado:

```powershell
# Executar todos os testes via CTest com saída detalhada em caso de falha:
# Executar todos os 10 testes via CTest com saída detalhada em caso de falha:
ctest --test-dir build --output-on-failure
```

Os testes cobrem:

1. `test_reed_solomon`: Correção de até 8 erros por pacote e detecção de falha via Berlekamp-Massey e Forney.
2. `test_descrambler`: LFSR de dispersão de energia de 15 bits do ISDB-T com avanço sobre o sync byte `0x47`.
3. `test_viterbi`: Decodificador convolucional $K=7$ contínuo com puncionamento (taxas 1/2, 2/3, 3/4, 5/6, 7/8).
4. `test_reed_solomon`: Correção de até 8 erros por pacote, detecção de falha com >8 erros e multiplicação direta em $GF(2^8)$ em $O(1)$.
5. `test_descrambler`: LFSR de dispersão de energia de 15 bits do ISDB-T com reinício de quadro e avanço sobre o sync byte `0x47`.
6. `test_viterbi`: Decodificador convolucional $K=7$ contínuo com puncionamento (taxas 1/2, 2/3, 3/4, 5/6, 7/8) e anel de histórico estático.
7. `test_fec_pipeline`: Cadeia completa de FEC integrada (Viterbi $\to$ Byte Deinterleaver $\to$ Descrambler $\to$ Reed-Solomon).
8. `test_ofdm_sync`: Estimador ML de Van de Beek para sincronismo de símbolo OFDM e rastreamento de FFO.
9. `test_ofdm_sync`: Estimador ML de Van de Beek para sincronismo de símbolo OFDM e rastreamento de FFO contínuo com SIMD.
10. `test_oneseg_e2e`: Pipeline fim-a-fim simulado com modulação QPSK, taxa 2/3, GI 1/8 e desentrelaçamento temporal $I=4$.
11. `test_spsc_queue`: Ring buffer SPSC lock-free (64 slots), concorrência multithread, contagem de drops e gap tracking.
12. `test_delay_line`: Linhas de atraso estáticas `FixedDelayLine` e dinâmicas contíguas `DynamicDelayLine` com zero alocações no heap.
13. `test_reset_recovery`: Recuperação determinística do pipeline DSP e estabilização de NCO após gaps de sequência e resets coordenados.
14. `test_sync_state_and_csi`: Máquina de estados tripartite de sincronismo OFDM (`Acquisition` $\to$ `Tracking` $\to$ `LostLock`) e escala de ponderação CSI centrada em 128.

### 2.4 Automação de Releases (`build_release.ps1`)

O repositório fornece um script em PowerShell para geração e verificação automática de pacotes de release:

```powershell
# Gera a release completa do Windows (compila, testa, empacota e valida):
.\scripts\build_release.ps1 -Platform windows

# Pular compilação caso o binário já esteja pronto:
.\scripts\build_release.ps1 -Platform windows -SkipBuild
```

O script empacota os binários, DLLs, utilitário Zadig, MPV e licença em `releases/<plataforma>/yyyymmdd_v<versao>.zip` e **valida automaticamente** a integridade e os tamanhos de todos os arquivos internos via API nativa `.NET ZipFile`, garantindo 100% de compatibilidade com o Windows Explorer.

### 2.5 Ferramentas de Desenvolvimento e Diagnóstico (`tools/`)

O repositório disponibiliza no diretório [`tools/`](tools/) um ecossistema completo de utilitários prontos para uso em bancada de testes de RF, depuração de hardware USB e reprodução de alta performance:

| Ferramenta / Diretório                   | Função & Descrição Técnica                                                                                                                                                                                                                  |
| ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **[`tools/zadig.exe`](tools/zadig.exe)** | Instalador portátil do driver **WinUSB** (v2.9 oficial / libwdi). Permite associar o driver WinUSB à `Interface 0` do RTL2832U, liberando as transferências bulk I/Q de alta velocidade necessárias para demodulação OFDM.                  |
| **[`tools/rtl-sdr/`](tools/rtl-sdr/)**   | Coleção completa de binários oficiais da **Osmocom RTL-SDR** e bibliotecas de runtime:                                                                                                                                                      |
| ↳ `rtl_test.exe`                         | **Teste de Estabilidade e Perda de Amostras:** Testa a taxa de transferência USB e estabilidade do PLL sob a taxa exata do ISDB-T (2031746 S/s):<br>`.\tools\rtl-sdr\rtl_test.exe -s 2031746 -t`                                            |
| ↳ `rtl_sdr.exe`                          | **Gravador de Amostras I/Q Brutas:** Captura amostras de RF diretamente do ar para arquivos binários (`.raw`), permitindo testes offline determinísticos:<br>`.\tools\rtl-sdr\rtl_sdr.exe -f 527143000 -s 2031746 -n 8000000 live_ch23.raw` |
| ↳ `rtl_power.exe`                        | **Analisador de Espectro FFT:** Mede a densidade de potência espectral e o piso de ruído térmico em varreduras de banda larga na faixa de UHF (470 a 806 MHz).                                                                              |
| ↳ `rtl_tcp.exe`                          | **Servidor de Streaming I/Q em Rede:** Transmite amostras I/Q digitalizadas via rede TCP/IP para clientes SDR remotos.                                                                                                                      |
| ↳ `rtl_fm.exe` / `rtl_adsb.exe`          | Demodulador FM/AM em linha de comando e decodificador de telemetria aeronáutica ADS-B (1090 MHz).                                                                                                                                           |
| ↳ `rtl_eeprom.exe`                       | Leitura e reprogramação da memória EEPROM do RTL2832U (número de série, Vendor ID e Product ID).                                                                                                                                            |
| ↳ `librtlsdr.dll` / `libusb-1.0.dll`     | DLLs de runtime do driver RTL-SDR e da biblioteca de abstração USB de alta velocidade.                                                                                                                                                      |
| **[`tools/mpv/`](tools/mpv/)**           | Reprodutor multimídia **MPV (x64 portátil)** integrado:                                                                                                                                                                                     |
| ↳ `mpv.exe` / `mpv.com`                  | Binários portáteis disparados diretamente pelo decodificador via `.\isdbtb_rx.exe play -c <canal> --player`.                                                                                                                                |
| ↳ `mpv.conf`                             | Configurações de GPU otimizadas para 1-Seg móvel: upscaling espacial **EWA-Lanczos Sharp**, interpolação de movimento temporal **`display-resample`**, debanding dinâmico e buffers de latência ultrabaixa.                                 |
| ↳ `doc/`                                 | Manual oficial completo do MPV (`manual.pdf`) e mapa visual de atalhos de teclado (`mpbindings.png`).                                                                                                                                       |
| ↳ `installer/`                           | Scripts de integração e atualização automática (`updater.ps1`, `mpv-install.bat`).                                                                                                                                                          |

### 2.6 Mapa da Arquitetura do Código

```text
isdb_t_isdb_tb_tuner_player/
├── CMakeLists.txt              # Script de configuração do CMake
├── LICENSE                     # GNU General Public License v3.0 (GPLv3)
├── README.md                   # Documentação principal em português
├── README_en.md                # Documentação técnica em inglês
├── AGENTS.md                   # Invariantes arquiteturais rígidas para IA e desenvolvedores
├── include/
│   ├── dsp/                    # Algoritmos de Camada Física (OfdmSync, OneSegDemod, Viterbi, RS, etc.)
│   │   ├── constants.hpp       # Constantes fundamentais ISDB-T (Fs = 2031746 S/s, soft neutral 128)
│   │   ├── delay_line.hpp      # FixedDelayLine e DynamicDelayLine (zero alocações no heap)
│   │   ├── spsc_capture_queue.hpp # Fila SPSC lock-free (64 slots) com detecção de descontinuidades
│   │   └── dsp_metrics.hpp     # Estrutura de telemetria de runtime de DSP
│   ├── streaming/              # UdpStreamer (transmissão de 1316 bytes para MPV/VLC)
│   ├── mpegts/                 # Parser de tabelas PSI/SI (PAT, PMT, SDT) e gerador TS
│   ├── cli/                    # ChannelManager e persistência JSON
│   └── third_party/            # CLI11, nlohmann/json
├── src/
│   ├── main.cpp                # Ponto de entrada CLI, captura assíncrona USB e Job Object do MPV
│   ├── main.cpp                # Ponto de entrada CLI, captura assíncrona USB SPSC e Job Object do MPV
│   └── ...                     # Implementações C++ dos módulos de DSP, MPEG-TS e streaming
├── tools/
│   ├── zadig.exe               # Utilitário oficial de instalação de drivers WinUSB (v2.9)
│   ├── mpv/                    # Reprodutor MPV portátil x64 com pipeline de upscaling GPU
│   │   ├── mpv.exe / mpv.conf  # Executável e perfil de renderização de vídeo e deband
│   │   ├── doc/                # Manual oficial do MPV (PDF) e mapa de atalhos
│   │   └── installer/          # Scripts de atualização e integração ao sistema
│   └── rtl-sdr/                # Utilitários Osmocom RTL-SDR de diagnóstico e DLLs
│       ├── rtl_test.exe        # Teste de estabilidade de barramento USB e perda de amostras
│       ├── rtl_sdr.exe         # Gravador de amostras I/Q brutas de RF para testes offline
│       ├── rtl_tcp.exe / fm    # Streaming I/Q em rede e receptor FM de teste
│       ├── rtl_power.exe       # Analisador de espectro de RF e potência UHF
│       └── librtlsdr.dll, etc. # DLLs de runtime do driver RTL-SDR e libusb
├── scripts/
│   ├── build_release.ps1       # Automação de build, empacotamento e validação de releases
│   ├── play_mpv.bat            # Script auxiliar de disparo manual do player MPV
│   └── calibrate_ppm.py        # Utilitário de calibração de oscilador local
│   ├── quick_rf_scan.py        # Varredura rápida de potência RF na taxa exata de 2031746 S/s
│   └── calibrate_ppm.py        # Utilitário de calibração de oscilador local na taxa exata de 2031746 S/s
├── releases/                   # Pacotes de release prontos para uso (windows/, linux/, macos/)
├── docs/                       # Documentação técnica detalhada e decisões arquiteturais (ADR)
└── tests/                      # Casos de teste determinísticos (CTest)
├── docs/                       # Documentação técnica detalhada e decisões arquiteturais (ADR 001 a 023)
└── tests/                      # 10 casos de teste determinísticos (CTest)
```

---

## 🔬 3. Camada Física (DSP) & Inovações Matemáticas

### 3.1 Resumo dos Algoritmos de Camada Física

- **Taxa de Amostragem Fundamental ($2031746\text{ S/s}$):** Submúltiplo exato de $1/4$ da taxa de chip $F_{base} = 512/63\text{ MHz} \approx 8.126984\text{ MHz}$, garantindo correlação de prefixo cíclico superior a $90\%$ e FFT alinhada sem necessidade de reamostragem fracionária.
- **Sincronismo de Símbolo OFDM & FFO (Van de Beek ML):** Autocorrelação de prefixo cíclico deslizante com compensação contínua de fase NCO acelerada por SIMD VOLK.
- **Fila SPSC Lock-Free de Captura USB (`SpscCaptureQueue`):** Ring buffer estático de 64 blocos com semântica acquire-release, contadores atômicos de drops e sinalização explícita de gaps temporais para resets coordenados de NCO.
- **Zero Alocações Dinâmicas no Caminho Crítico:** Eliminação completa de `std::deque` e `std::vector::erase` nos desentrelaçadores no tempo, bits e Forney, substituídos por `FixedDelayLine`, `DynamicDelayLine` e consumo por offset deslizante (`viterbi_stream_offset_`).
- **Máquina de Estados de Sincronismo OFDM Tripartite (`SyncState`):** Alterna inteligentemente entre `Acquisition` (busca exaustiva de GI), `Tracking` (rastreamento estreito em janela de $\pm 4$ amostras) e `LostLock` (recuperação com histerese), acelerando o sincronismo em mais de $5\times$.
- **Equalização MMSE Regularizada:** Substituição do Zero-Forcing clássico por MMSE adaptativo ($\sigma^2 = \max(10^{-5}, 0.05 \cdot P_{\text{avg}})$), evitando explosão de ruído em nulos profundos de canais seletivos em frequência.
- **Ponderação CSI (Channel State Information) no Viterbi:** Fator de confiabilidade $W_l = \text{clamp}(|H_l|^2 / P_{\text{avg}}, 0, 2)$ atenuando métricas de subportadoras degradadas em direção ao erasure (128), proporcionando ganho de sensibilidade de **+2 a 3 dB** sob desvanecimento Rayleigh.
- **Decodificador TMCC DBPSK + BCH(273, 191):** Rastreamento de sincronismo de quadro (204 símbolos) nas portadoras 101, 131, 286 e 349, configurando modulação e desentrelaçador temporal de forma dinâmica.
- **Desentrelaçadores ISDB-T:** Frequência (`FREQ_PERM_MODE3` de 384 portadoras), Tempo convolucional ($I \times (95 - (5c \bmod 96))$), Bits e Forney ($I=12, M=17$).
- **Outer FEC Reed-Solomon:** Decodificador $RS(204, 188, t=8)$ com Berlekamp-Massey, Chien Search e Forney com $FCR=0$.
- **Ponderação CSI Centrada em 128 com Sincronismo de Atraso:** Soft-bits normalizados centrados em 128 com linhas de retardo de canal dedicadas idênticas às de dados (120 símbolos no MSB), eliminando distorção geométrica em 16QAM e proporcionando ganho de sensibilidade de **+2 a 3 dB** sob desvanecimento Rayleigh.
- **Outer FEC Reed-Solomon com Tabela Direta de 64 KB em $O(1)$:** Multiplicações em $GF(2^8)$ executadas em um único ciclo de clock sem desvios condicionais (_branch-free_), acelerando Berlekamp-Massey, Chien Search e Forney.
- **Decodificador TMCC DBPSK + BCH(273, 191):** Rastreamento contínuo de sincronismo de quadro (204 símbolos) nas portadoras 101, 131, 286 e 349, reconfigurando dinamicamente parâmetros de camada física em tempo real.

### 3.2 Documentação Técnica Especializada

Para consultar aprofundamentos matemáticos, histórico de evolução e guias avançados, consulte os documentos na pasta [`docs/`](docs/):
Para consultar aprofundamentos matemáticos, formulações teóricas e histórico completo de engenharia, consulte os documentos na pasta [`docs/`](docs/):

| Documento                                                               | Conteúdo e Finalidade                                                                                                                                                                                                                                                                                                                                    |
| ----------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 🏛️ [`docs/ARCHITECTURE_DECISIONS.md`](docs/ARCHITECTURE_DECISIONS.md)   | **Registros de Decisões de Arquitetura (ADR 001 a 017):** Histórico completo de todos os algoritmos utilizados e substituídos (Zero-Forcing $\to$ MMSE, CSI Weighting, Two-Pass Scan, MPV Job Object, alinhamento MTU 1316B, correções contra `gr-isdbt`) e suas justificativas matemáticas.                                                             |
| 🏛️ [`docs/ARCHITECTURE_DECISIONS.md`](docs/ARCHITECTURE_DECISIONS.md)   | **Registros de Decisões de Arquitetura (ADR 001 a 023):** Histórico completo de todos os algoritmos utilizados e substituídos (SPSC Lock-Free, Zero-Allocation DelayLines, CSI Soft Centered, 3-State OFDM Sync, O(1) GF Table, LTO/IPO, MMSE, Two-Pass Scan, MPV Job Object, MTU 1316B, correções contra `gr-isdbt`) e suas justificativas matemáticas. |
| 📖 [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md)                           | **Manual Detalhado do Usuário:** Instruções passo a passo de configuração, comandos avançados, sintonia fina de ganho de RF e seção de solução de problemas.                                                                                                                                                                                             |
| 📐 [`docs/blueprint_isdbt_decoder.md`](docs/blueprint_isdbt_decoder.md) | **Especificação Técnica do ISDB-T:** Parâmetros das normas ABNT NBR 15601 / 15603 e ARIB STD-B31, tabelas de entrelaçamento e taxas de transmissão.                                                                                                                                                                                                      |
| 🤖 [`AGENTS.md`](AGENTS.md)                                             | **Guia de Orientação para Agentes de IA e Engenheiros:** Regras críticas de integridade ("Não Quebre"), lista de erros históricos a não repetir e diretivas de conformidade.                                                                                                                                                                             |
| 🤖 [`AGENTS.md`](AGENTS.md)                                             | **Guia de Orientação para Agentes de IA e Engenheiros:** Regras críticas de integridade ("Não Quebre"), lista de 20 erros históricos a não repetir e 12 diretivas absolutas de conformidade.                                                                                                                                                             |

---

## 🤝 4. Projetos de Referência & Agradecimentos

Este projeto foi construído apoiando-se no trabalho e nas especificações de excelentes projetos de código aberto:

- **[`git-artes/gr-isdbt`](https://github.com/git-artes/gr-isdbt):** Implementação acadêmica de referência do padrão ISDB-T para GNU Radio desenvolvida por Federico Larroca e Martin Gomez (Universidad de la República, Uruguai). Os polinômios de entrelaçamento, PRBS e matrizes de trellis serviram de base para nosso núcleo nativo C++.
- **[`librtlsdr`](https://github.com/steve-m/librtlsdr):** Driver de código aberto para receptores baseados no Realtek RTL2832U, mantido por Steve Markgraf e Dimitri Stolnikov.
- **[`MPV Player`](https://github.com/mpv-player/mpv):** Reprodutor de mídia leve e altamente customizável que alimenta o subsistema de vídeo integrado com shaders de upscaling.
- **[`Zadig / libwdi`](https://github.com/pbatard/libwdi):** Ferramenta essencial desenvolvida por Pete Batard para instalação limpa de drivers WinUSB no Windows.
- **[`VOLK`](https://github.com/gnuradio/volk) & [`FFTW3`](https://www.fftw.org/):** Bibliotecas de cálculo vetorial de ponto flutuante e transformadas rápidas de Fourier de altíssimo desempenho.
- **[`CLI11`](https://github.com/CLIUtils/CLI11) & [`nlohmann/json`](https://github.com/nlohmann/json):** Utilitários modernos em C++ para interfaces de linha de comando e serialização de dados.

---

## 📜 5. Licença

Este projeto é um software livre distribuído sob os termos da **GNU General Public License v3.0 (GPL-3.0-or-later)**.  
Consulte o arquivo [`LICENSE`](LICENSE) para obter o texto integral da licença.
