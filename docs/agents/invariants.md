# Invariantes Arquiteturais Críticos
Obrigatórios para qualquer edição de código.

### 1. Taxa de amostragem exata: 2031746 S/s
- $F_{base} = 512/63$ MHz ≈ 8.126984 MHz; 1-Seg/RTL-SDR usa $F_s = F_{base}/4 = 512/(63 \times 4)$ MHz ≈ 2.031746 MSps = 2031746 Hz.
- ⚠ **Nunca** usar 2.048 MSps fixo sem reamostragem racional. 2031746 S/s alinha os pontos de FFT por símbolo (Mode 3 = 2048, Mode 2 = 1024, Mode 1 = 512) e garante pico de correlação de CP > 90%.

### 2. UDP para VLC/MPV: 7 × 188 = 1316 bytes; URL
- Cada datagrama para `udp://@127.0.0.1:1234` **deve** ter exatamente 7 pacotes MPEG-TS de 188 bytes.
- Motivo: 1316 B é padrão IPTV, cabe no MTU Ethernet de 1500 B sem fragmentação IP.
- URL padrão de escuta: `udp://@127.0.0.1:1234`.
- Nunca enviar pacotes de 188 B isolados (sobrecarga de interrupções, travamentos no player).

### 3. Saída binária no Windows
- Windows abre `stdout`/arquivos em modo texto (`0x0A` → `0x0D 0x0A`), o que corrompe MPEG-TS.
- Hoje o TS sai por UDP e por `--output-ts`, aberto com `std::ios::binary` (`cmd_play.cpp`). Não há saída de TS por `stdout`.
- Qualquer nova saída binária (arquivo ou `stdout`) deve abrir em modo binário (`std::ios::binary`; para `stdout`, `_setmode(_fileno(stdout), _O_BINARY)`).

### 4. Persistência de canais estritamente local
- `channels.json` fica **só** no diretório de execução (`./channels.json`).
- Nunca usar `%APPDATA%` ou `~/.config/` (app portátil, sem falhas de permissão, roda de pendrive). Usar `ChannelManager` + `std::filesystem`.

### 5. Fila SPSC lock-free e resets coordenados de NCO
- Leitura USB ↔ DSP **deve** usar `SpscCaptureQueue<64>` (ring estático pré-alocado).
- A fila rastreia sequência monotônica e sinaliza lacunas (`discontinuity_before`).
- Descontinuidade → reset coordenado (`reset_play_reception_stream` em `cmd_play.cpp`, `reset_scan_reception` em `cmd_scan.cpp`): descarta `stream_iq_buf`, reinicia fases do NCO. Nunca correlacionar sobre saltos de amostras.

### 6. Zero alocação no heap no loop quente de DSP
- Retardos: `FixedDelayLine<T, MaxCapacity>` (Forney $12 \times 187$ bytes, bits $4 \times 120$ floats) e `DynamicDelayLine<T>` (pré-alocação única no init; 384 ramos de tempo).
- Buffers temporários de `process_symbol` em `OneSegDemodulator` = membros `std::array`.
- Bytes do Viterbi avançam por `viterbi_stream_offset_`, sem `erase(begin, begin + 204)`.
- Proibido `malloc`, `new`, `std::vector::erase` ou `std::deque` no processamento contínuo de símbolos.

### 7. CSI centrado em 128 com atraso de bits sincronizado
- Demapeamento euclidiano sobre dados equalizados puros (preserva limiares 16QAM).
- Soft-bits 0–255, neutro 128 (`constants::SOFT_NEUTRAL`).
- Pesos $W_l$ passam por linhas de atraso idênticas às dos dados (`time_csi_delay_lines_`, `bit_csi_delay_lines_`; mesmos 120 símbolos no MSB).
- Escala estrita: `128 + clamp(W * (soft - 128), -128, +127)`; $W \to 0$ → exatamente 128 (apagamento na treliça).

### 8. FSM tripartite de sincronismo OFDM
- `OfdmSync`: `Acquisition` (busca exaustiva dos 4 GIs com folding), `Tracking` (janela ±4 amostras em torno de $d_{expected}$), `LostLock` (histerese de 3 falhas consecutivas → `Acquisition`).
- Em tracking, nunca recalcular todos os GIs a cada bloco.

### 9. $GF(2^8)$ em O(1) sem branches no RS
- RS(204,188) usa tabela direta `gf_mul_table_[256][256]` (64 KB).
- Nunca introduzir branches (`if (a == 0 || b == 0)`) ou lookups log/exp indiretos no laço quente de Berlekamp-Massey, Chien e Forney.

### 10. Dual-mode OFDM e baseline intocável (`VanDeBeekSync`)
- Van de Beek (`src/dsp/ofdm_sync.cpp`, `include/dsp/ofdm_sync.hpp`) é o baseline bit-a-bit (**REGRA DE OURO**). Nunca alterar lógica, FSM ou limiares.
- `IOFDMSynchronizer` via fábrica `make_synchronizer`:
  - `--ofdm 1` (`VanDeBeekSync`): adaptador puro sobre o existente.
  - `--ofdm 2` (`SpCirSynchronizer`/`RobustGRISDBTSync`): SP-CIR em dois estágios (Speth & Meyr 1999/2001) + port gr-isdbt (`src/sync/sp_cir_sync.cpp`): refino SP-CIR, First Arrival Path, CFO estável sem deriva em multipercurso urbano, FSM com histerese.
- Sintaxe: `--ofdm 1`/`--ofdm 2` ou `--ofdm=1`/`--ofdm=2`.
- `--ofdm` é exclusiva de `play`; em `scan`, `probe`, `epg`, `list` deve ser ignorada silenciosamente, sem erro.

### 11. Compilação dual-architecture (`TARGET_ARCH=x64` / `x86`)
- 64 bits (`-m64`) e 32 bits (`-m32 -msse2 -mfpmath=sse`).
- Handlers/callbacks Win32 no x86 exigem `WINAPI` (`__stdcall`).
- Binários/DLLs RTL-SDR e MPV isolados por arquitetura: `tools/rtl-sdr/x86/`, `tools/rtl-sdr/x64/`, `tools/mpv/x86/`, `tools/mpv/x64/`.
- Utilitários Python (`calibrate_ppm.py`, `quick_rf_scan.py`) e ferramentas de captura/síntese ficam em `tools/`; `scripts/` = automações operacionais e lançadores de player.

### 12. SP, CIR e primeiro caminho no `SpCirSynchronizer`
- `--ofdm 2`: refino de timing e rastreio de CIR via IDFT de 64 pontos sobre os SP do Segmento 0 (36 subportadoras úteis centradas no bin 808).
- **Coerência PAPR ≥ 12.0:** ruído gaussiano na IDFT 64 pts tem média teórica ≈ 4.73 (P(>10) < $10^{-3}$). PAPR < 12.0 → métrica CIR **deve** retornar 0.0 → fallback transparente para CP, sem perturbar a malha PI de timing/SCO.
- **First Arrival Path:** em $\tau \in [-16 \dots +16]$, **deve** escolher o 1º pico com energia ≥ 25% de $P_{\max}$ (ecos de prédios podem superar a visada direta); evita atração pelo centro de gravidade da CIR e ISI.
- **Estabilidade de CFO (ADR 028):** passa-baixas assintótico, ganho 0.08 ancorado, **zona morta ±0.5 Hz** (`if (sp_tmcc_anchored_ && std::abs(ffo_res) < 0.5f) ffo_res = 0.0f;`). Converge desvios iniciais de até ~120 Hz do oscilador; em regime ($|res| < 0.5$ Hz) congela, eliminando deriva cumulativa por viés de fase do multipercurso (Canal 28).
- **Zero alocação:** planos FFTW3 (`fft_plan_`, `ifft_cir_plan_`) e buffers (`fft_in_`, `fft_out_`, `cir_in_`, `cir_out_`) são membros pré-alocados, destruídos explicitamente no destrutor.
