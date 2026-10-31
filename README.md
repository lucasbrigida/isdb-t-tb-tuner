# 📡 ISDB-T / ISDB-Tb Universal SDR Receiver & Player

Decodificador universal de TV digital **ISDB-T / ISDB-Tb (1-Seg)** em C++17 multiplataforma (Windows 10+ e Linux), com aceleração matemática vetorial (SIMD **VOLK** + **FFTW3**), captura direta via **RTL-SDR**, varredura profunda de canais e streaming em tempo real via **UDP** para o **VLC Media Player**.

---

## 🚀 Funcionalidades

- **Descoberta Automática de RTL-SDR:** Detecta dongles conectados (`RTL2832U`, `R820T/R828D`), seriais e ganhos suportados.
- **Camada Física Completa (Physical Layer DSP):**
  - **Sincronismo de Símbolo OFDM:** Autocorrelação de Prefixo Cíclico deslizante (estimador ML Van de Beek).
  - **Correção de Frequência Fina (FFO):** Desrotacionamento de portadora acelerado via SIMD AVX2/FMA com VOLK.
  - **FFT:** Modos 1 (2K), 2 (4K) e 3 (8K) com a biblioteca FFTW3.
  - **Decodificador Viterbi (Inner FEC):** Convolucional \(K=7\), geradores \(171_8\) e \(133_8\), com suporte a taxas puncionadas (1/2, 2/3, 3/4...).
  - **Descrambler de Dispersão de Energia:** PRBS \(1 + X^{14} + X^{15}\) sincronizado pelo sync byte invertido `0xB8`.
  - **Decodificador Reed-Solomon (Outer FEC):** \(RS(204, 188, t=8)\) com correção de até 8 bytes corrompidos por pacote.
- **Camada de Aplicação:**
  - **Varredura Profunda de Frequências (`scan`):** Varre a faixa UHF (canais 14 a 51/69) e salva os canais em `%APPDATA%\isdbtb_rx\channels.json` (Windows) ou `~/.config/isdbtb_rx/channels.json` (Linux).
  - **Listagem Formatada (`list`):** Tabela ANSI com canal, frequência, RSSI e codecs.
  - **Streaming Nativo para o VLC (`play`):** Empacotamento broadcast padrão de 7 pacotes MPEG-TS (188B) por datagrama UDP (1316 bytes) para `udp://@127.0.0.1:1234` com latência ultrabaixa.
  - **Exportação de Playlist (`export`):** Gera arquivo `.m3u` com todos os canais sintonizados.

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

### 1. Inspecionar hardware e aceleração DSP
```powershell
.\build\isdbtb_rx.exe probe
```

### 2. Varrer o espectro UHF em busca de canais
```powershell
# Varrer canais UHF 14 ao 45 (padrão Brasil/Internacional)
.\build\isdbtb_rx.exe scan

# Ou varrer faixa específica
.\build\isdbtb_rx.exe scan --start 28 --end 32
```

### 3. Listar canais encontrados
```powershell
.\build\isdbtb_rx.exe list
```

### 4. Sintonizar e transmitir para o VLC
```powershell
# Sintonizar por número de canal (ex: canal 30)
.\build\isdbtb_rx.exe play -c 30

# Ou abrir o VLC automaticamente
.\build\isdbtb_rx.exe play -c 30 --vlc
```
No VLC aberto manualmente: vá em **Mídia > Abrir Fluxo de Rede** e insira a URL:
```
udp://@127.0.0.1:1234
```

### 5. Exportar Playlist M3U
```powershell
.\build\isdbtb_rx.exe export -o channels.m3u
```
Para assistir: basta abrir `channels.m3u` com o VLC.

---

## 🧪 Suíte de Testes Unitários

Todos os blocos do DSP possuem testes unitários determinísticos:

```powershell
# Teste do decodificador Reed-Solomon (204, 188) com injeção de erros
.\build\test_reed_solomon.exe

# Teste do Descrambler (PRBS de dispersão de energia)
.\build\test_descrambler.exe

# Teste do Decodificador Viterbi (Inner FEC K=7)
.\build\test_viterbi.exe

# Teste da cadeia completa de FEC integrada
.\build\test_fec_pipeline.exe

# Teste do sincronismo OFDM contra amostra real de sinal do ar
.\build\test_ofdm_sync.exe
```

---

## 📊 Estrutura do Projeto

```
isdb_t_isdb_tb_tuner_player/
├── CMakeLists.txt              # Sistema de compilação C++17
├── include/
│   ├── dsp/                    # Reed-Solomon, Viterbi, Descrambler, OFDM Sync
│   ├── streaming/              # UdpStreamer (1316 bytes MTU)
│   ├── mpegts/                 # Parser de tabelas PSI/SI (PAT, PMT, SDT)
│   ├── cli/                    # ChannelManager e persistência JSON
│   └── third_party/            # CLI11 e nlohmann/json
├── src/
│   ├── main.cpp                # Ponto de entrada CLI e orquestrador
│   └── ...                     # Implementações dos módulos
├── scripts/
│   ├── calibrate_ppm.py        # Medição de DC offset e calibração de rádio
│   └── quick_rf_scan.py        # Varredura rápida de espectro RF
└── tests/                      # Testes unitários determinísticos e amostras IQ
```

