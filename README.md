# 📡 ISDB-T / ISDB-Tb Universal SDR Receiver & Real-Time Player

<p align="center">
  <img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License: GPLv3" />
  <img src="https://img.shields.io/badge/C%2B%2B-17%20%2F%2020-00599C.svg?logo=c%2B%2B" alt="C++17/20" />
  <img src="https://img.shields.io/badge/Hardware-RTL--SDR%20(RTL2832U)-red.svg" alt="RTL-SDR" />
  <img src="https://img.shields.io/badge/Platform-Windows%20(x64%20%7C%20x86)%20%7C%20Linux-brightgreen.svg" alt="Platforms" />
  <img src="https://img.shields.io/badge/Player-MPV%20Embedded-darkgreen.svg" alt="MPV Embedded" />
  <img src="https://img.shields.io/badge/Tests-22%2F22%20Passed-success.svg" alt="CTest Passing" />
</p>

Receptor e decodificador digital completo de televisão aberta no padrão **ISDB-T / ISDB-Tb / SBTVD** (Brasil, Japão, Filipinas e América Latina) para dispositivos **RTL-SDR** (RTL2832U com tuners R820T/R828D/E4000). Desenvolvido em **C++ nativo de alta performance sem dependências do framework GNU Radio**, com fila de captura **SPSC lock-free** com detecção atômica de descontinuidades, **zero alocações no heap** no processamento de símbolos, **arquitetura dual de sincronismo OFDM** (sincronizador de Van de Beek padrão e port robusto do `gr-isdbt` com rastreamento ativo de timing, CFO/SCO e histerese de re-lock), equalização adaptativa **MMSE regularizada**, ponderação **CSI** sincronizada por linhas de retardo no decodificador Viterbi, multiplicação em $GF(2^8)$ em **$O(1)$** via tabela de 64 KB no Reed-Solomon, varredura inteligente de canais com identificação de nomes via **SDT**, **suporte nativo dual-architecture (64 bits x64 e 32 bits x86)** e **reprodutor MPV embutido com super-resolução e interpolação de movimento em tempo real**.

---

<p align="center">
  <b>Idioma / Language:</b><br/>
  <b>🇧🇷 Português (Brasil)</b> &nbsp;|&nbsp; 
  <a href="README_en.md">🇺🇸 English</a>
</p>

---

## 🎬 Demonstrações em Tempo Real & Inspeção de Hardware

Confira abaixo o receptor em execução em seus diferentes modos operacionais. Clique nas miniaturas para assistir aos vídeos ou visualizar a imagem em resolução original:

|                                                                                                                                                                                              📡 **Varredura Automática (`scan`)**                                                                                                                                                                                              |                                                                                                                                                                                                   📺 **Sintonia e Reprodução (`play`)**                                                                                                                                                                                                   |
| :----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------: | :---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------: |
|   <a href="docs/videos/scan.mp4" title="Clique para assistir ao vídeo de varredura"><img src="docs/videos/scan_poster.png" alt="Varredura de canais scan" width="100%" style="border-radius: 6px; border: 1px solid #333;" /></a><br>▶️ <strong>Vídeo:</strong> <em>Varredura rápida em 2 passos (UHF 14–69) com decodificação SDT de emissoras (<a href="docs/videos/scan.mp4"><code>docs/videos/scan.mp4</code></a>)</em>    |  <a href="docs/videos/play.mp4" title="Clique para assistir ao vídeo de reprodução"><img src="docs/videos/play_poster.png" alt="Sintonia e reprodução play" width="100%" style="border-radius: 6px; border: 1px solid #333;" /></a><br>▶️ <strong>Vídeo:</strong> <em>Sintonia ao vivo com MPV integrado, upscaling EWA-Lanczos e interpolação de movimento (<a href="docs/videos/play.mp4"><code>docs/videos/play.mp4</code></a>)</em>   |
|                                                                                                                                                                                             **🎬 Demonstração Completa (`demo`)**                                                                                                                                                                                              |                                                                                                                                                                                                 **🔍 Diagnóstico de Hardware (`probe`)**                                                                                                                                                                                                  |
| <a href="docs/videos/demo.mp4" title="Clique para assistir à demonstração completa"><img src="docs/videos/demo_poster.png" alt="Demonstração completa em tempo real demo" width="100%" style="border-radius: 6px; border: 1px solid #333;" /></a><br>▶️ <strong>Vídeo:</strong> <em>Demodulação de sinal de RF a 2.0317 MSps e reprodução contínua (<a href="docs/videos/demo.mp4"><code>docs/videos/demo.mp4</code></a>)</em> | <a href="docs/images/probe.png" title="Clique para ampliar a imagem de diagnóstico"><img src="docs/images/probe.png" alt="Diagnóstico de hardware probe" width="100%" style="border-radius: 6px; border: 1px solid #333;" /></a><br>📷 <strong>Imagem:</strong> <em>Inspeção do dongle RTL-SDR, sintonizador R820T, tabela de ganhos e aceleração VOLK SIMD (<a href="docs/images/probe.png"><code>docs/images/probe.png</code></a>)</em> |

---

## 📑 Índice (Sumário)

- [⚡ 1. Guia Rápido para Usuários Finais (Download & Execução)](#-1-guia-rápido-para-usuários-finais-download--execução)
  - [1.1 Download das Releases Autônomas (x64 / x86)](#11-download-das-releases-autônomas-x64--x86)
  - [1.2 Instalação do Driver USB (Zadig em 1 Clique)](#12-instalação-do-driver-usb-zadig-em-1-clique)
  - [1.3 Comandos Essenciais da CLI](#13-comandos-essenciais-da-cli)
  - [1.4 Recursos e Teclas do Player MPV Integrado](#14-recursos-e-teclas-do-player-mpv-integrado)
- [🛠️ 2. Guia para Desenvolvedores (Compilação & Testes)](#️-2-guia-para-desenvolvedores-compilação--testes)
  - [2.1 Pré-requisitos & Toolchain Recomendado (w64devkit)](#21-pré-requisitos--toolchain-recomendado-w64devkit)
  - [2.2 Como Compilar Nativamente para 64 bits e 32 bits](#22-como-compilar-nativamente-para-64-bits-e-32-bits)
  - [2.3 Suíte de Testes Determinísticos (CTest - 20/20 Testes Aprovados)](#23-suíte-de-testes-determinísticos-ctest---2020-testes-aprovados)
  - [2.4 Automação de Releases (`build_release.ps1`)](#24-automação-de-releases-build_releaseps1)
  - [2.5 Ferramentas de Diagnóstico, Benchmark e Amostras (`tools/`, `bench/`, `samples/`)](#25-ferramentas-de-diagnóstico-benchmark-e-amostras-tools-bench-samples)
  - [2.6 Mapa da Arquitetura do Código](#26-mapa-da-arquitetura-do-código)
- [🔬 3. Camada Física (DSP) & Inovações Matemáticas](#-3-camada-física-dsp--inovações-matemáticas)
  - [3.1 Resumo dos Algoritmos de Camada Física](#31-resumo-dos-algoritmos-de-camada-física)
  - [3.2 Documentação Técnica Especializada](#32-documentação-técnica-especializada)
- [🤝 4. Projetos de Referência & Agradecimentos](#-4-projetos-de-referência--agradecimentos)
- [📜 5. Licença](#-5-licença)

---

## ⚡ 1. Guia Rápido para Usuários Finais (Download & Execução)

Se você deseja apenas sintonizar e assistir à TV Digital no seu computador com o RTL-SDR, siga os passos abaixo:

### 1.1 Download das Releases Autônomas (x64 / x86)

Baixe o arquivo `.zip` da release para a arquitetura do seu sistema operacional:

- 📦 **Windows x64 (64 bits):** na aba [Releases do GitHub](../../releases)
  - Contém binários nativos de 64 bits, DLLs do RTL-SDR x64, Zadig e MPV portátil de 64 bits.
- 📦 **Windows x86 (32 bits):** na aba [Releases do GitHub](../../releases)
  - Contém binários nativos de 32 bits, DLLs oficiais de 32 bits, Zadig e MPV oficial de 32 bits (`i686`).
- Descompacte o arquivo `.zip` em qualquer pasta do seu computador. Todos os componentes já estão inclusos e prontos para uso.

### 1.2 Instalação do Driver USB (Zadig em 1 Clique)

Para que o decodificador converse diretamente com o chip RTL2832U:

1. Conecte o dongle RTL-SDR e a antena UHF a uma porta USB do computador.
2. Dê um duplo clique no arquivo **`zadig.exe`** (incluso na pasta descompactada).
3. No menu **Options**, clique em **List All Devices**.
4. No menu suspenso, selecione **`Bulk-In, Interface (Interface 0)`** _(ou `RTL2832U` / `RTL2838UHIDIR`)_.
   > ⚠️ **Atenção:** Se houver `Interface 0` e `Interface 1`, selecione **sempre a `Interface 0`** (o barramento de alta velocidade do sinal de RF; a Interface 1 serve apenas para o controle remoto infravermelho).
5. Certifique-se de que o driver alvo à direita da seta seja **`WinUSB`** e clique em **`Install Driver`** (ou `Replace Driver`).
6. Aguarde a mensagem de confirmação: _"The driver was installed successfully"_. O dispositivo está pronto!

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

# 5. Sintonizar no modo de sincronismo reforçado (--ofdm 2) para sinais fracos ou com reflexos:
.\isdbtb_rx.exe play -c 23 --ofdm 2 --player

# 6. Sintonizar com exibição do status detalhado de sinal em tempo real (-v / --verbose):
.\isdbtb_rx.exe play -c 23 --player -v
```

#### 📶 Escolha do Algoritmo de Sincronismo (`--ofdm <1|2>`):

O subcomando `play` permite escolher o algoritmo de sincronização OFDM através da flag `--ofdm`:

```text
Uso: play [--ofdm <1|2>]

  --ofdm 1   Modo padrão. Mais leve e econômico (Van de Beek ML baseline).
  --ofdm 2   Modo reforçado. Mantém a imagem estável com sinal
             fraco, muito reflexo ou em movimento. Usa mais
             processamento.

Sem a opção, o modo 1 é usado.
Também aceita a sintaxe com sinal de igual (--ofdm=1 ou --ofdm=2).
--ofdm só vale para 'play' e é ignorada nos demais comandos.

Exemplos:
  play -c 23 --player
  play -c 23 --ofdm 2 --player
  play -c 23 --ofdm=2 --player
```

> 💡 **Modo Detalhado (`-v`, `--verbose`):** Por padrão, a interface do comando `play` mantém um visual limpo e estático. Use `-v` ou `--verbose` para exibir a linha de telemetria e status em tempo real (RSSI, lock OFDM/TMCC, SNR, pacotes TS e taxas de correção RS).

> 💡 **Encerramento Sincronizado:** O player MPV é gerenciado por um _Job Object_ do kernel do Windows. Ao fechar o terminal da CLI ou pressionar `Ctrl+C`, o player fecha instantaneamente sem deixar processos órfãos em segundo plano.

### 1.4 Recursos e Teclas do Player MPV Integrado

O reprodutor MPV embutido inclui um pipeline gráfico pré-configurado (`mpv.conf`) especialmente desenhado para transmissões móveis 1-Seg:

- **Super-Resolução Espacial:** Filtro `ewa_lanczossharp` com `sigmoid-upscaling`, gerando bordas limpas e sem dentes de serra ao ampliar para 1080p/4K.
- **Debanding Dinâmico:** Elimina artefatos de compressão e blocos de cor típicos do vídeo H.264 em baixa taxa de bits.
- **Interpolação de Movimento (`display-resample`):** Sincroniza a taxa do vídeo com a frequência de atualização do seu monitor (60Hz, 120Hz, 144Hz ou 240Hz), eliminando qualquer engasgo (_judder_).

| Tecla de Atalho | Ação no Player MPV                                                            |
| :-------------: | :---------------------------------------------------------------------------- |
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

- **O que é o [`w64devkit`](https://github.com/skeeto/w64devkit/releases):** Uma distribuição portátil, autocontida e de inicialização instantânea do compilador GCC/G++ multilib para Windows mantida por Christopher Wellons (_skeeto_). Inclui GDB, GNU Make, BusyBox (com shell POSIX `sh`, `awk`, `sed`, `grep`) e cabeçalhos MinGW-w64 com suporte nativo a C++17 e C++20 com compilação cruzada para 64 bits e 32 bits (`-m32` / `-m64`).
- **Como configurar o w64devkit em 2 minutos:**
  1. Baixe o pacote mais recente (`w64devkit-x.y.z.zip`) na página oficial de releases: [github.com/skeeto/w64devkit/releases](https://github.com/skeeto/w64devkit/releases).
  2. Extraia o conteúdo para uma pasta no seu computador (ex: `C:\w64devkit`).
  3. Adicione o caminho `C:\w64devkit\bin` à variável de ambiente `PATH` do sistema (ou abra o terminal através do atalho `w64devkit.exe`).
- **Sistema de Compilação:** [CMake 3.20+](https://cmake.org/download/) instalado e adicionado ao `PATH`.
- **Bibliotecas de Aceleração e Rádio:**
  - `librtlsdr` e `libusb-1.0` (cabeçalhos e bibliotecas de importação inclusos em `deps/`, DLLs pré-compiladas para x64 e x86 em `tools/rtl-sdr/`).
  - `FFTW3` (transformadas rápidas de Fourier de 2048, 4096 e 8192 pontos).
  - `VOLK` (operações matemáticas vetoriais com suporte a AVX2, FMA e SSE).
- _(Opcional para automação e testes):_ Python 3.8+ e PowerShell 7+.

### 2.2 Como Compilar Nativamente para 64 bits e 32 bits

No terminal do Windows com ambiente MinGW-w64 (ex: `w64devkit`):

#### Compilação para 64 bits (x64):

```powershell
# 1. Configurar o projeto com CMake para 64 bits e sincronizador robusto habilitado:
cmake -S . -B build_x64 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DTARGET_ARCH=x64 -DISDBT_ENABLE_GRISDBT_SYNC=ON

# 2. Compilar executáveis e suíte de testes:
cmake --build build_x64 -j
```

#### Compilação para 32 bits (x86):

```powershell
# 1. Configurar o projeto com CMake para 32 bits com aceleração SSE2:
cmake -S . -B build_x86 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DTARGET_ARCH=x86 -DISDBT_ENABLE_GRISDBT_SYNC=ON

# 2. Compilar executáveis e suíte de testes:
cmake --build build_x86 -j
```

#### Opções de Compilação no CMake

- **`-DTARGET_ARCH=x64` / `-DTARGET_ARCH=x86`** _(Padrão: `auto`)_: Define a arquitetura do binário; GCC/Clang recebem `-m64` ou `-m32 -msse2 -mfpmath=sse`, enquanto MSVC deve ser configurado com `-A x64` ou `-A Win32`.
- **`-DISDBT_ENABLE_GRISDBT_SYNC=ON`** _(Padrão: OFF)_: Habilita o algoritmo de sincronização robusto do `gr-isdbt` (`--ofdm 2` ou `--ofdm=2`).
- **`-DCMAKE_BUILD_TYPE=Release`**: Ativa otimização `-O3 -DNDEBUG` (GCC/Clang) ou `/O2 /DNDEBUG` (MSVC), desativando assertions em runtime.
- **`-DISDBT_ENABLE_FAST_MATH=ON`** _(Padrão: OFF)_: Adiciona `-ffast-math` ou `/fp:fast` para acelerar vetorizações AVX2/FMA no cálculo de métricas e NCO.
- **Interprocedural Optimization (IPO / LTO)**: Detectado e habilitado automaticamente para builds de Release caso suportado pelo compilador (`CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON`).

### 2.3 Suíte de Testes Determinísticos (CTest - 20/20 Testes Aprovados)

O projeto conta com testes unitários matemáticos que rodam em frações de segundo e não dependem de rádio físico conectado:

```powershell
# Executar todos os testes via CTest com saída detalhada em caso de falha:
ctest --test-dir build_x64 --output-on-failure -E BenchSync
```

Os 20 testes determinísticos cobrem:

1. `ReedSolomon`: Correção de até 8 erros por pacote, detecção de falha com >8 erros e multiplicação direta em $GF(2^8)$ em $O(1)$.
2. `Descrambler`: LFSR de dispersão de energia de 15 bits do ISDB-T com avanço contínuo sobre o sync byte `0x47`.
3. `Viterbi`: Decodificador convolucional $K=7$ contínuo com puncionamento (taxas 1/2, 2/3, 3/4, 5/6, 7/8) e anel de histórico estático.
4. `FecPipeline`: Cadeia completa de FEC integrada (Viterbi $\to$ Byte Deinterleaver $\to$ Descrambler $\to$ Reed-Solomon).
5. `OfdmSync`: Estimador ML de Van de Beek para sincronismo de símbolo OFDM e rastreamento de FFO contínuo com SIMD.
6. `OneSegE2E`: Pipeline fim-a-fim simulado com modulação QPSK, taxa 2/3, GI 1/8 e desentrelaçamento temporal $I=4$.
7. `IqStreamBuffer`: Buffer contínuo e dinâmico de amostras I/Q entre blocos de leitura USB.
8. `SpscQueue`: Ring buffer SPSC lock-free (64 slots), concorrência multithread, contagem de drops e gap tracking.
9. `DelayLine`: Linhas de atraso estáticas `FixedDelayLine` e dinâmicas contíguas `DynamicDelayLine` com zero alocações no heap.
10. `ResetRecovery`: Recuperação determinística do pipeline DSP e estabilização de NCO após gaps de sequência e resets coordenados.
11. `SyncStateAndCsi`: Máquina de estados tripartite de sincronismo OFDM (`Acquisition` $\to$ `Tracking` $\to$ `LostLock`) e escala de ponderação CSI centrada em 128.
12. `FrequencyPlan`: Síntese de frequências e cálculo de portadoras para todos os canais UHF 14 a 69.
13. `ChannelManager`: Operações em memória e persistência JSON estritamente local e portátil (`channels.json`).
14. `PsiParser`: Parsers de MPEG-TS PAT, PMT e SDT com resolução de emissoras e síntese de PAT de fallback.
15. `CliOfdmFlag`: Validação de parsing de linha de comando da flag `--ofdm` (`--ofdm 1`, `--ofdm 2`, `--ofdm=1`, `--ofdm=2`), rejeição estrita de valores inválidos e descarte silencioso fora de `play`.
16. `SyncRegressionScan`: Garantia de não-regressão do scanner, operando estritamente sob Van de Beek baseline.
17. `BenchAsciiTable`: Formatador de tabelas ASCII para relatórios de telemetria e benchmark.
18. `RegressionOfdm1`: Teste de regressão do modo Van de Beek baseline ancorado na amostra de referência de TV aberta.
19. `SyncGrisdbtRobust`: Validação do sincronizador robusto baseado em CP (timing, estimativa inicial de CFO, SCO e FSM de re-lock).
20. `RegressionOfdm2`: Teste de regressão do sincronizador robusto ancorado na amostra de referência de TV aberta.

### 2.4 Automação de Releases (`build_release.ps1`)

O repositório fornece um script em PowerShell para geração, compilação, teste, empacotamento e verificação automática de integridade PE das releases:

```powershell
# Gerar pacotes para todas as arquiteturas (x64 e x86):
pwsh -ExecutionPolicy Bypass -File scripts/build_release.ps1 -Platform windows -Arch all

# Gerar release apenas de 64 bits:
pwsh -ExecutionPolicy Bypass -File scripts/build_release.ps1 -Platform windows -Arch x64

# Gerar release apenas de 32 bits:
pwsh -ExecutionPolicy Bypass -File scripts/build_release.ps1 -Platform windows -Arch x86
```

O script monta os arquivos binários, DLLs, Zadig e MPV em `releases/windows/yyyymmdd_v<versao>_win64.zip` e `releases/windows/yyyymmdd_v<versao>_win32.zip`, e **valida rigorosamente** o formato PE (`pei-x86-64` vs. `pei-i386`) de cada executável via `objdump -f`.

### 2.5 Ferramentas de Diagnóstico, Benchmark e Amostras (`tools/`, `bench/`, `samples/`)

O repositório conta com um ecossistema completo de utilitários de diagnóstico de hardware, benchmarking e amostras reais de sinal de RF:

| Utilitário / Diretório                                 | Descrição Técnica & Casos de Uso                                                                                                                                                              |
| :----------------------------------------------------- | :-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **[`bench/`](bench/)**                                 | **Suíte de Benchmark (`bench_sync`):** Executável comparativo (`bench_sync --compare` / `--segment-of 5`) medindo ciclos/símbolo, uso de CPU, RSS e estabilidade entre algoritmos OFDM.       |
| **[`samples/`](samples/)**                             | **Amostras de Referência RF:** Contém a amostra oficial de 10 segundos do Canal 24 (`ch24_10s_iq.cu8`) na taxa exata de 2031746 S/s para testes determinísticos (gerenciada via Git LFS).     |
| **[`tools/zadig.exe`](tools/zadig.exe)**               | Instalador portátil oficial do driver **WinUSB** (v2.9 / libwdi). Vincula o driver à `Interface 0` do RTL2832U em 1 clique.                                                                   |
| **[`tools/rtl-sdr/`](tools/rtl-sdr/)**                 | Conjunto completo de ferramentas e DLLs oficiais do **Osmocom RTL-SDR**:                                                                                                                      |
| ↳ `x64/` e `x86/`                                      | Subdiretórios dedicados com `librtlsdr.dll` e utilitários para cada arquitetura.                                                                                                              |
| ↳ `rtl_test.exe`                                       | **Diagnóstico de Estabilidade USB:** Testa a largura de banda do barramento USB e travamento de PLL do tuner na taxa exata (2031746 S/s):<br>`.\tools\rtl-sdr\x86\rtl_test.exe -s 2031746 -t` |
| ↳ `rtl_sdr.exe`                                        | **Gravador de RF I/Q:** Captura amostras brutas de RF para arquivos binários `.raw`/`.cu8` para simulação offline.                                                                            |
| ↳ `rtl_power.exe`                                      | **Analisador de Espectro FFT:** Mede a densidade espectral e o piso de ruído térmico em toda a faixa de UHF (470 a 806 MHz).                                                                  |
| ↳ `rtl_tcp.exe`                                        | **Servidor de Streaming TCP:** Transmite dados I/Q digitalizados através de sockets de rede para processamento remoto.                                                                        |
| **[`tools/mpv/`](tools/mpv/)**                         | Reprodutor oficial **MPV portátil** segregado simetricamente por arquitetura:                                                                                                                 |
| ↳ `x64/`                                               | Reprodutor MPV portátil de 64 bits integrado com shaders de upscaling em tempo real.                                                                                                          |
| ↳ `x86/`                                               | Reprodutor oficial MPV portátil (i686 32 bits) para máxima compatibilidade com hardware legado.                                                                                               |
| **[`tools/calibrate_ppm.py`](tools/calibrate_ppm.py)** | **Calibração de PPM:** Ferramenta Python para estimativa precisa do erro de oscilador local em 2031746 S/s.                                                                                   |
| **[`tools/quick_rf_scan.py`](tools/quick_rf_scan.py)** | **Varredura Rápida de RF:** Varre rapidamente o espectro UHF (canais 14 a 45) reportando níveis de sinal.                                                                                     |

### 2.6 Mapa da Arquitetura do Código

```text
isdb_t_isdb_tb_tuner_player/
├── CMakeLists.txt              # Configuração CMake (TARGET_ARCH, ISDBT_ENABLE_GRISDBT_SYNC)
├── LICENSE                     # Licença GNU General Public License v3.0 (GPLv3)
├── README.md                   # Documentação em Português do Brasil
├── README_en.md                # Documentação em Inglês
├── AGENTS.md                   # Invariantes arquiteturais estritas para agentes de IA e desenvolvedores
├── PROJECT.md                  # Especificações técnicas e mapa de roadmap
├── include/
│   ├── sync/                   # Interface IOFDMSynchronizer, VanDeBeekSync, RobustGRISDBTSync, SyncFactory
│   ├── dsp/                    # DSP de Camada Física (OfdmSync, OneSegDemod, Viterbi, RS, etc.)
│   │   ├── constants.hpp       # Constantes fundamentais do ISDB-T (Fs = 2031746 S/s, neutro soft 128)
│   │   ├── delay_line.hpp      # FixedDelayLine e DynamicDelayLine (zero alocações no heap)
│   │   ├── spsc_capture_queue.hpp # Fila circular SPSC lock-free (64 slots) com detecção de gaps
│   │   └── dsp_metrics.hpp     # Estrutura de telemetria e métricas em runtime
│   ├── streaming/              # UdpStreamer (datagramas de 1316 bytes para MPV/VLC)
│   ├── mpegts/                 # Parsers de PSI/SI (PAT, PMT, SDT) e gerador de padrão de teste
│   ├── cli/                    # ChannelManager e persistência em JSON
│   └── third_party/            # CLI11, nlohmann/json
├── src/
│   ├── main.cpp                # Ponto de entrada CLI, leitor USB SPSC lock-free, Job Object do MPV
│   ├── sync/                   # Implementações de sincronizadores (VanDeBeekSync, RobustGRISDBTSync)
│   └── ...                     # Implementações C++ de DSP, MPEG-TS e streaming
├── bench/                      # Ferramenta de benchmark bench_sync e gerador de relatórios
├── samples/                    # Amostras de RF de referência (Canal 18, 2031746 S/s, Git LFS)
├── tools/
│   ├── zadig.exe               # Instalador de driver USB (WinUSB v2.9)
│   ├── calibrate_ppm.py        # Utilitário de calibração de PPM de oscilador local a 2031746 S/s
│   ├── quick_rf_scan.py        # Utilitário de varredura rápida de RF dos canais UHF
│   ├── mpv/                    # Reprodutor MPV embutido portátil segregado por arquitetura
│   │   ├── x64/                # Reprodutor oficial MPV portátil (x64) com shaders
│   │   └── x86/                # Reprodutor oficial MPV portátil (i686 32 bits)
│   └── rtl-sdr/                # Ferramentas oficiais e DLLs Osmocom RTL-SDR segregadas
│       ├── x64/                # DLLs e utilitários de 64 bits (rtl_test, rtl_sdr, etc.)
│       └── x86/                # DLLs e utilitários de 32 bits (rtl_test, rtl_sdr, etc.)
├── scripts/
│   ├── build_release.ps1       # Automação de release dual-arch e validação PE
│   ├── play_mpv.bat            # Script auxiliar para reprodução com MPV (auto/x64/x86)
│   ├── play_mpv_x86.bat        # Atalho dedicado para executar o MPV de 32 bits
│   └── play_mpv.ps1            # Script PowerShell de reprodução com MPV
├── releases/                   # Pacotes de release prontos para uso (win64.zip / win32.zip)
├── docs/                       # Registros de decisões de arquitetura (ADR 001 a 025) e especificações
└── tests/                      # 20 casos de testes determinísticos CTest e suítes de regressão
```

---

## 🔬 3. Camada Física (DSP) & Inovações Matemáticas

### 3.1 Resumo dos Algoritmos de Camada Física

- **Taxa de Amostragem Fundamental Exata ($2031746\text{ S/s}$):** Submúltiplo exato de $1/4$ da taxa de chip do ISDB-T $F_{\text{base}} = 512/63\text{ MHz} \approx 8.126984\text{ MHz}$, garantindo $>90\%$ de correlação no prefixo cíclico e caixas de FFT perfeitamente alinhadas sem filtros de reamostragem racional.
- **Arquitetura Dual de Sincronismo OFDM:**
  - _Modo 1 (`VanDeBeekSync`, Baseline):_ Autocorrelação por prefixo cíclico com estimação de desvio fracionário de frequência (FFO) e máquina de estados tripartite (`Acquisition`, `Tracking` $\pm 4$, `LostLock`).
  - _Modo 2 (`RobustGRISDBTSync`, gr-isdbt port):_ Coarse CP acquisition, active Scattered Pilot (SP) timing refinement, integer CFO estimation via TMCC, fractional CFO via SP, sampling clock offset (SCO) phase accumulation, and 3-symbol lock hysteresis.
- **Fila de Captura USB SPSC Lock-Free (`SpscCaptureQueue`):** Ring buffer estático de 64 slots com semântica de memória acquire-release, contadores atômicos de drops e sinalização explícita de descontinuidades de sequência com resets coordenados de NCO.
- **Zero Alocações Dinâmicas no Caminho Crítico:** Eliminação completa de `std::deque` e `std::vector::erase` nos desentrelaçadores Forney, de bits e de tempo, substituídos por `FixedDelayLine`, `DynamicDelayLine` e avanço por ponteiro deslizante (`viterbi_stream_offset_`).
- **Equalização Adaptativa MMSE Regularizada:** Substitui o Zero-Forcing tradicional por MMSE adaptativo ($\sigma^2 = \max(10^{-5}, 0.05 \cdot P_{\text{avg}})$), evitando amplificação de ruído em vales profundos de desvanecimento.
- **Ponderação CSI Centrada em 128 com Linhas Paralelas de Retardo:** Soft-bits euclidianos escalados em torno do valor neutro 128 com linhas de atraso de pesos CSI paralelas aos dados (atraso de 120 símbolos no MSB), preservando os limiares de decisão em 16QAM e gerando ganho de **+2 a 3 dB de SNR** sob desvanecimento Rayleigh.
- **Reed-Solomon com Tabela Direta 2D de 64 KB em $O(1)$:** Multiplicações em $GF(2^8)$ executadas em ciclo único de clock sem branches condicionais, acelerando drasticamente o Berlekamp-Massey, a Busca de Chien e o Algoritmo de Forney.
- **Sincronizador TMCC DBPSK + BCH(273, 191):** Rastreamento contínuo do sincronismo de quadro de 204 símbolos nas subportadoras 101, 131, 286 e 349, reconfigurando dinamicamente os parâmetros da camada física.

### 3.2 Documentação Técnica Especializada

Para formulações matemáticas detalhadas, decisões de projeto e aprofundamento técnico, consulte os documentos na pasta [`docs/`](docs/):

| Documento                                                               | Conteúdo & Finalidade                                                                                                                                                              |
| :---------------------------------------------------------------------- | :--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 🏛️ [`docs/ARCHITECTURE_DECISIONS.md`](docs/ARCHITECTURE_DECISIONS.md)   | **Registros de Decisões de Arquitetura (ADR 001 a 025):** Histórico completo de todos os algoritmos avaliados, introduzidos e substituídos com justificativas matemáticas formais. |
| 📖 [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md)                           | **Guia do Usuário:** Instruções detalhadas de uso, guia de sintonia fina com `--ofdm`, calibração de ganho de RF e solução de problemas.                                           |
| 📐 [`docs/blueprint_isdbt_decoder.md`](docs/blueprint_isdbt_decoder.md) | **Blueprint de Engenharia ISDB-T:** Especificações das normas ABNT NBR 15601/15603 e ARIB STD-B31, diagramas de desentrelaçadores e estruturas de quadro.                          |
| 🤖 [`AGENTS.md`](AGENTS.md)                                             | **Guia de Orientação para Agentes de IA:** Invariantes de código ("Não Quebre"), catálogo histórico de 20 erros corrigidos e diretrizes absolutas de conformidade.                 |
| 📊 [`docs/samples/README.md`](docs/samples/README.md)                   | **Documentação das Amostras RF de Referência:** Metodologia de gravação do Canal 18, parâmetros físicos e hashes de validação.                                                     |

---

## 🤝 4. Projetos de Referência & Agradecimentos

Este decodificador foi construído com base nas contribuições e especificações da comunidade de código aberto de SDR:

- **[`git-artes/gr-isdbt`](https://github.com/git-artes/gr-isdbt):** Implementação acadêmica de referência do padrão ISDB-T para GNU Radio criada por Federico Larroca e Martin Gomez (Universidad de la República, Uruguai).
- **[`librtlsdr`](https://github.com/steve-m/librtlsdr):** Driver de código aberto para dispositivos Realtek RTL2832U mantido por Steve Markgraf e Dimitri Stolnikov.
- **[`MPV Player`](https://github.com/mpv-player/mpv):** Reprodutor de mídia de alta performance que viabiliza a super-resolução em tempo real no nosso projeto.
- **[`Zadig / libwdi`](https://github.com/pbatard/libwdi):** Ferramenta essencial desenvolvida por Pete Batard para instalação limpa do driver WinUSB.
- **[`VOLK`](https://github.com/gnuradio/volk) & [`FFTW3`](https://www.fftw.org/):** Motores de aceleração matemática vetorial e transformadas de Fourier de altíssimo desempenho.
- **[`CLI11`](https://github.com/CLIUtils/CLI11) & [`nlohmann/json`](https://github.com/nlohmann/json):** Bibliotecas modernas de parsing de linha de comando e serialização JSON.

---

## 📜 5. Licença

Este projeto é software livre licenciado sob os termos da **GNU General Public License v3.0 (GPL-3.0-or-later)**.  
Consulte o arquivo [`LICENSE`](LICENSE) para ler a licença na íntegra.
