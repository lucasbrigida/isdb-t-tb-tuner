# Mapa de Módulos

| Módulo | Arquivos | Responsabilidade |
|---|---|---|
| Abstração de sync | `include/sync/iofdm_synchronizer.h`, `include/sync/sync_factory.h`, `src/sync/sync_factory.cpp` | Interface `IOFDMSynchronizer` + fábrica polimórfica `--ofdm=1`/`--ofdm=2` |
| Van de Beek | `include/sync/van_de_beek_sync.h`, `src/sync/van_de_beek_sync.cpp` | Adaptador puro sobre `OfdmSync` baseline (bit-a-bit inalterado) |
| SP-CIR (Speth & Meyr) | `include/sync/sp_cir_sync.h`, `src/sync/sp_cir_sync.cpp` | 2 estágios (Speth & Meyr 1999/2001; Larroca et al. 2014): timing fino SP-CIR, First Arrival Path, CFO estável, sem deriva em multipercurso |
| Constantes | `include/dsp/constants.hpp` | Taxa 2031746 S/s, soft neutro 128, tamanhos TS/RS, subportadoras |
| Linhas de atraso | `include/dsp/delay_line.hpp` | `FixedDelayLine`/`DynamicDelayLine` (Forney, bits, tempo), zero heap |
| Fila SPSC | `include/dsp/spsc_capture_queue.hpp` | Lock-free 64 slots, sequência, detecção de gaps |
| Métricas DSP | `include/dsp/dsp_metrics.hpp` | Telemetria: blocos processados/descartados, resets, TS/RS, SNR, EVM |
| OFDM Sync (baseline) | `include/dsp/ofdm_sync.hpp`, `src/dsp/ofdm_sync.cpp` | FSM (`Acquisition`/`Tracking`/`LostLock`), Van de Beek, tracking ±4, FFO contínuo SIMD VOLK, FFTW3 |
| Demodulador 1-Seg | `include/dsp/oneseg_demodulator.hpp`, `src/dsp/oneseg_demodulator.cpp` | IFO, pilotos SP/CP ($X^{11}+X^2+1$), TMCC DBPSK BCH(273,191), deinterleavers freq/tempo/bit/Forney com linhas CSI paralelas, offset sliding, FEC |
| Reed-Solomon | `include/dsp/reed_solomon.hpp`, `src/dsp/reed_solomon.cpp` | RS(204,188,t=8): Berlekamp-Massey, Chien, tabela 2D $GF(2^8)$ O(1) |
| Descrambler | `include/dsp/descrambler.hpp`, `src/dsp/descrambler.cpp` | LFSR 15 bits ($1 + X^{14} + X^{15}$), modo ISDB-T (por quadro OFDM) e superframe |
| Viterbi | `include/dsp/viterbi_decoder.hpp`, `src/dsp/viterbi_decoder.cpp` | Inner FEC contínuo K=7 (`0x4F`/`0x6D`), sobreviventes em buffer circular, zero alocação |
| Streaming UDP | `include/streaming/udp_streamer.hpp`, `src/streaming/udp_streamer.cpp` | WinSock2/POSIX, datagramas de 1316 B para `udp://@127.0.0.1:1234` |
| PSI/SI | `include/mpegts/psi_parser.hpp`, `src/mpegts/psi_parser.cpp` | PAT (PID 0), PMT, SDT (PID 0x0011) → nome da emissora; PAT de fallback |
| Channel Manager | `include/cli/channel_manager.hpp`, `src/cli/channel_manager.cpp` | `channels.json` |
| Benchmark | `bench/bench_sync_main.cpp`, `bench/bench_metrics.cpp`, `bench/bench_report.cpp` | `bench_sync`: throughput, CPU, estabilidade de lock |
| Main CLI | `src/main.cpp` | CLI11, captura USB SPSC, `--ofdm`, telemetria, Job Object do MPV |
