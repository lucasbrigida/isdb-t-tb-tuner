# 📡 ISDB-T / ISDB-Tb Universal SDR Receiver & Player

Decodificador universal de TV digital **ISDB-T / ISDB-Tb (1-Seg)** em C++17 multiplataforma (Windows 10+ e Linux), com aceleração matemática vetorial (SIMD **VOLK** + **FFTW3**), captura direta via **RTL-SDR**, varredura profunda de canais e streaming em tempo real via **UDP** para o **VLC Media Player**, 100% compatível com a implementação de referência [`git-artes/gr-isdbt`](https://github.com/git-artes/gr-isdbt).

---

## 🚀 Funcionalidades

- **Descoberta Automática de RTL-SDR:** Detecta dongles conectados (`RTL2832U`, `R820T/R828D`), seriais e ganhos suportados.
- **Captura Assíncrona Multithread:** Leitura de alta prioridade via thread dedicada com fila concorrente protegida por mutex/condition variable, eliminando perda de pacotes USB.
- **Camada Física Completa (Physical Layer DSP - 1-Seg):**
  - **Sincronismo de Símbolo OFDM Contínuo:** Autocorrelação de Prefixo Cíclico deslizante (estimador ML Van de Beek) e rastreamento fino de borda de símbolo.
  - **Correção de Frequência Fina (FFO) e Inteira (IFO):** Desrotacionamento de portadora com fase NCO contínua via SIMD VOLK e busca de IFO com histerese e rejeição de lóbulos secundários.
  - **FFT e Equalização de Canal:** Modos 1 (2K), 2 (4K) e 3 (8K) com FFTW3 e interpolação linear sobre Pilotos Dispersos (SP) e Piloto Contínuo (CP) com PRBS \(X^{11} + X^2 + 1\).
  - **Decodificador TMCC DBPSK + BCH(273, 191):** Detecção de palavras de sincronismo par/ímpar (`frame_begin` a cada 204 símbolos) e configuração automática de modulação (`DQPSK`, `QPSK`, `16QAM`), taxa de código e entrelaçamento temporal (\(I \in \{0, 1, 2, 4, 8, 16\}\)).
  - **Desentrelaçadores ISDB-T:** Frequência (`FREQ_PERM_MODE3` de 384 portadoras), Tempo (FIFOs \(I \times (95 - (5c \bmod 96))\)), Bits (atraso de 120 no MSB QPSK) e Bytes Convolucional de Forney (\(I=12, M=17\)).
  - **Decodificador Viterbi Contínuo (Inner FEC):** Convolucional \(K=7\) (`POLYA=0x4F`, `POLYB=0x6D`), Trellis contínua de 64 estados com suporte às taxas puncionadas `1/2`, `2/3`, `3/4`, `5/6` e `7/8`.
  - **Descrambler de Dispersão de Energia ISDB-T:** PRBS \(1 + X^{14} + X^{15}\) sincronizado a cada quadro OFDM de 204 símbolos.
  - **Decodificador Reed-Solomon (Outer FEC):** \(RS(204, 188, t=8, \text{FCR}=0)\) com Berlekamp-Massey, Chien Search e Forney corrigindo até 8 bytes por pacote.
- **Camada de Aplicação:**
  - **Varredura Profunda de Frequências (`scan`):** Varre a faixa UHF (canais 14 a 45/69) e salva os canais em `%APPDATA%\isdbtb_rx\channels.json` (Windows) ou `~/.config/isdbtb_rx/channels.json` (Linux).
  - **Listagem Formatada (`list`):** Tabela ANSI com canal, frequência, RSSI e codecs.
  - **Streaming Nativo para o VLC (`play`):** Empacotamento broadcast padrão de 7 pacotes MPEG-TS (188B) por datagrama UDP (1316 bytes) para `udp://@127.0.0.1:1234` com latência ultrabaixa e parser PSI/SI (`PAT`, `PMT`, `SDT`).

---

## 🛠️ Compilação

O projeto compila nativamente com **CMake** e **MinGW-w64** (ou GCC/Clang no Linux):

```powershell
# Configurar e gerar makefiles
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

# Compilar todos os alvos e testes
cmake --build build -j
```

---

## 💻 Guia de Comandos (CLI)

Exibir menu geral de ajuda:

```powershell
.\build\isdbtb_rx.exe --help
```

### 1. Inspecionar hardware e aceleração DSP (`probe`)

```powershell
.\build\isdbtb_rx.exe probe
```

### 2. Varrer o espectro UHF em busca de canais (`scan`)

```powershell
# Varrer canais UHF 14 ao 45 (padrão Brasil/América Latina)
.\build\isdbtb_rx.exe scan

# Varrer faixa específica e escolher índice do dongle RTL-SDR (-d)
.\build\isdbtb_rx.exe scan -s 20 -e 25 -d 0
```

### 3. Listar canais encontrados (`list`)

```powershell
.\build\isdbtb_rx.exe list
```

### 4. Sintonizar e transmitir via UDP (`play`)

```powershell
# Sintonizar por número de canal (-c / --channel)
.\build\isdbtb_rx.exe play -c 23

# Sintonizar por nome da emissora (-n / --name)
.\build\isdbtb_rx.exe play -n BAND

# Sintonizar ajustando ganho de RF em dB (-g / --gain), porta UDP (-p / --port) e dispositivo (-d / --device)
.\build\isdbtb_rx.exe play -c 23 -g 28.0 -p 1234 -d 0

# Transmitir padrão de teste sintético para diagnóstico de rede (-t / --test-pattern)
.\build\isdbtb_rx.exe play -c 23 -t
```

> [!TIP]
> **Como assistir no seu player (VLC, ffplay, etc.):**
> Abra o VLC Media Player, acesse o menu **Mídia > Abrir Fluxo de Rede** (ou pressione `Ctrl+N`) e informe a URL:
>
> ```text
> udp://@127.0.0.1:1234
> ```
> Ou via terminal:
> ```powershell
> vlc udp://@127.0.0.1:1234
> ```

---

## 🧪 Suíte de Testes Unitários e E2E

Todos os blocos do DSP possuem testes determinísticos que podem ser executados individualmente ou via `ctest`:

```powershell
# Executar toda a suíte via CTest
ctest --test-dir build --output-on-failure

# 1. Teste do decodificador Reed-Solomon (204, 188) com injeção de erros
.\build\test_reed_solomon.exe

# 2. Teste do Descrambler (PRBS de dispersão de energia)
.\build\test_descrambler.exe

# 3. Teste do Decodificador Viterbi (Inner FEC K=7)
.\build\test_viterbi.exe

# 4. Teste da cadeia completa de FEC integrada (Viterbi + Descrambler + RS)
.\build\test_fec_pipeline.exe

# 5. Teste do sincronismo OFDM (Van de Beek ML CP correlation)
.\build\test_ofdm_sync.exe

# 6. Teste Fim-a-Fim (E2E) do Demodulador 1-Seg completo compatível com gr-isdbt
.\build\test_oneseg_e2e.exe
```

---

## 📊 Estrutura do Projeto

```text
isdb_t_isdb_tb_tuner_player/
├── AGENTS.md                   # Regras arquiteturais e histórico de erros/acertos para agentes de IA
├── CMakeLists.txt              # Sistema de compilação C++17
├── include/
│   ├── dsp/                    # OfdmSync, OneSegDemodulator, Viterbi, Descrambler, ReedSolomon
│   ├── streaming/              # UdpStreamer (1316 bytes MTU = 7 x 188B para udp://@127.0.0.1:1234)
│   ├── mpegts/                 # Parser de tabelas PSI/SI (PAT, PMT, SDT) e TsGenerator
│   ├── cli/                    # ChannelManager e persistência JSON
│   └── third_party/            # CLI11, nlohmann/json e headers de portabilidade DSP/SDR
├── src/
│   ├── main.cpp                # Ponto de entrada CLI e captura assíncrona multithread SDR
│   └── ...                     # Implementações dos módulos DSP, MPEG-TS, CLI e Streaming
├── scripts/
│   ├── calibrate_ppm.py        # Medição de DC offset e calibração de rádio
│   └── quick_rf_scan.py        # Varredura rápida de espectro RF
└── tests/                      # Testes unitários determinísticos e teste integrado 1-Seg E2E
```
