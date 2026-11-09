# Blueprint: Decodificador ISDB-T/ISDB-Tb (1-Seg)
Decisões da sessão de alinhamento (`/grill-me`): arquitetura, pipeline DSP, fluxo de dados e fases. C++17, Windows 10+ e Linux.
⚠ Documento histórico de planejamento. Onde divergir do código atual, valem [error_history](agents/error_history.md) (ex.: Viterbi `0x4F`/`0x6D`, não 171/133 — Erro 2; ISDB-T não usa `0xB8` — Erro 4) e os [ADRs](adr/README.md).

## 1. Decisões
1. **Escopo RF:** só 1-Seg. RTL-SDR estável até ~2.4 MSps (~2 MHz úteis); 1-Seg ocupa o segmento central 0 (≈ 428.57 kHz) → viável sem saturar USB.
2. **Build:** Windows MinGW-w64 (GCC/G++ 16.2) + CMake via `w64devkit`; Linux GCC/Clang + CMake. Dependências VOLK + FFTW3 + `librtlsdr` compiladas localmente.
3. **Calibração:** híbrida; `tools/calibrate_ppm.py` (`pyrtlsdr`) estima PPM/DC offset, integrado à CLI.
4. **Scanner:** varredura sequencial profunda (máxima sensibilidade): lock OFDM + PSI/SI (SDT/NIT) por canal (UHF Brasil 14–51 / Japão 13–52).
5. **Saída:** UDP nativo, 7 × 188 = 1316 B/datagrama para `udp://@127.0.0.1:1234`; `--vlc` opcional abre o VLC.
6. **Validação:** gravar IQ real, testar cada bloco DSP (unitário/integração com `--file capture.raw`) e validar ao vivo (`--device 0`).

## 2. Arquitetura
- Hardware: RTL-SDR (RTL2832U + R820T) → WinUSB/librtlsdr → amostras IQ.
- Pré-voo: `calibrate_ppm.py` → PPM/DC offset → correção no driver.
- DSP (C++17 + VOLK + FFTW3): Sync OFDM (símbolo + FFO via CP) → FFT (Mode 1: 2K, Mode 2: 4K, Mode 3: 8K) → equalização (SP/CP) → {TMCC (modulação, taxa, interleaving) → parâmetros p/ demap e deinterleave; demapeador QPSK/16QAM} → deinterleaver tempo/freq → Viterbi (K=7) → descrambler PRBS → RS(204,188,t=8).
- Aplicação: TS 188 B → parser PSI/SI (PAT, PMT, SDT → nome) → `channels.json`; TS → UDP 1316 B → VLC (`udp://@127.0.0.1:1234`).

## 3. Estrutura proposta (original)
```
CMakeLists.txt, README.md
scripts/   install_deps_windows.ps1 (librtlsdr, fftw3, volk), calibrate_ppm.py
include/hardware/  rtlsdr_device (descoberta, ganho/AGC, leitura async), iq_file_reader (IQ raw p/ testes)
include/dsp/  ofdm_sync (freq. fina, timing), fft_processor (FFTW3 2K/4K/8K), channel_equalizer,
              tmcc_decoder, deinterleaver (freq/tempo), viterbi_decoder (soft/hard pontuado),
              descrambler (X^15 + X^14 + 1), reed_solomon (RS(204,188))
include/mpegts/  ts_packet (sync 0x47), psi_parser (PAT, PMT, SDT)
include/streaming/  udp_streamer (7x188)
include/cli/  channel_manager (channels.json), scanner
include/utils/  platform (stdout binário, ANSI, sinais), metrics (SNR, RSSI, BER)
src/main.cpp (CLI11) + implementações
tests/  test_reed_solomon, test_descrambler, test_viterbi, test_tmcc, test_dsp_pipeline (IQ gravado)
```

## 4. Fases
1. **Ambiente:** `librtlsdr`, `fftw3`, `volk` com MinGW-w64; `CMakeLists.txt` + "Hello RTL-SDR"; dongle `VID_0BDA&PID_2838`.
2. **IQ + FEC unitário:** gravar IQ de canal 1-Seg; RS(204,188), descrambler, Viterbi (1/2, 2/3, 3/4...), deinterleavers.
3. **OFDM/TMCC:** autocorrelação CP (FFO, borda), FFT Modes 3/2/1, equalizador SP/CP, TMCC (lock de quadro).
4. **TS/PSI/scanner:** pacotes 188 B, SDT (nome), scanner UHF → `channels.json`.
5. **UDP/CLI:** 1316 B para `127.0.0.1:1234`, `--vlc`, subcomandos `probe`, `scan`, `list`, `play`; validação ao vivo.
