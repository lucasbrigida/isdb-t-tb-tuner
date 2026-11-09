# Project: isdb_t_isdb_tb_tuner_player (CLI Native)
Native CLI decoder/player for ISDB-T/ISDB-Tb 1-Seg via RTL-SDR; built-in header-only FFT/SIMD shims exposing the FFTW3/VOLK APIs (`include/third_party/`); MPEG-TS over UDP to embedded MPV or VLC.

## Architecture
- **Language:** C++17; Windows x64/x86, Linux.
- **Physical layer (DSP):**
  - Exact sample rate 2,031,746 S/s (512/63/4 MHz).
  - Lock-free SPSC USB ring (`SpscCaptureQueue<64>`), atomic sequence tracking, coordinated resets.
  - Zero heap allocation in hot loop (`FixedDelayLine`, `DynamicDelayLine`).
  - Dual OFDM sync via `IOFDMSynchronizer`:
    - Mode 1 `VanDeBeekSync` (baseline): tripartite FSM (`Acquisition`, `Tracking` ±4 samples, `LostLock`), CP correlation + FFO via Van de Beek ML, SIMD VOLK.
    - Mode 2 `SpCirSynchronizer` (Speth & Meyr / gr-isdbt port): coarse CP acquisition, SP-CIR timing via 64-pt IDFT, First Arrival Path (≥ 25% $P_{\max}$), integer CFO via TMCC, fractional CFO with asymptotic IIR + ±0.5 Hz deadband (no drift under urban multipath), SCO loop, lock hysteresis.
  - 2048-pt FFT (built-in radix-2 shim with the FFTW3 API), continuous fine symbol timing.
  - IFO tracking; SP/CP pilot equalization ($X^{11} + X^2 + 1$).
  - Regularized MMSE equalization with adaptive noise floor.
  - TMCC DBPSK + BCH(273,191).
  - Frequency/time/bit deinterleaving (ISDB-T Mode 3) with parallel CSI delay lines.
  - 128-centered CSI soft scaling preserving Euclidean thresholds (QPSK/16QAM).
- **FEC:**
  - Inner: continuous rate-adaptive Viterbi (K=7, `0x4F`/`0x6D`), circular survivor buffer.
  - Outer: RS(204,188,t=8), Berlekamp-Massey, Chien, Forney (FCR=0), O(1) 64 KB GF table.
  - PRBS descrambler ($1 + X^{14} + X^{15}$) synced to OFDM frame pulse.
- **Transport/output:**
  - 188-B MPEG-TS; PSI/SI (PAT, PMT, SDT); fallback PAT synthesis for 1-Seg.
  - UDP: 7 TS = 1316 B/datagram to `udp://@127.0.0.1:1234`, `flush_if_stale(80)`.
  - Embedded portable MPV with Win32 Job Object (native x64 and official i686) + custom GPU upscaling shaders.
  - Channels persisted in `channels.json`.

## CLI
- `probe`: detect/validate RTL-SDR and DSP environment.
- `scan`: two-pass scan of UHF channels (default 14–45, `-s`/`-e` to change) with SDT station naming.
- `list`: show `channels.json`.
- `play`: tune by number or station name, stream UDP, optional MPV (`--player`).
  - `--ofdm 1` (default): Van de Beek baseline. `--ofdm 2`: robust SP-CIR for weak/multipath/Doppler.
  - Accepts `--ofdm 1` and `--ofdm=1`; only for `play`, silently ignored elsewhere.

## Build & tests
- CMake 3.20+, MinGW-w64 (`w64devkit`)/GCC/MSVC.
- `-DTARGET_ARCH=x64|x86`: GCC/Clang `-m64` or `-m32 -msse2 -mfpmath=sse`; MSVC `-A x64` or `-A Win32`.
- `-DISDBT_ENABLE_SP_CIR_SYNC=ON` (optional, enables `--ofdm=2`; alias `-DISDBT_ENABLE_GRISDBT_SYNC=ON`).
- `-DCMAKE_BUILD_TYPE=Release`: `-O3 -DNDEBUG` (GCC) / `/O2 /DNDEBUG` (MSVC); IPO/LTO auto-enabled.
- `-DISDBT_ENABLE_FAST_MATH=ON` (optional): `-ffast-math` / `/fp:fast`.
- CTest 27/27 passed: ReedSolomon, Descrambler, Viterbi, FecPipeline, OfdmSync, OneSegE2E, IqStreamBuffer, SpscQueue, DelayLine, ResetRecovery, SyncStateAndCsi, BitDeinterleaverQam16, VolkCpuFeatures, CliSimdIntegration, FrequencyPlan, ChannelManager, PsiParser, AribB24, EpgManager, UdpStreamer, CliOfdmFlag, SyncRegressionScan, BenchAsciiTable, RegressionOfdm1, SyncSpCir, SyncGrisdbtRobust, RegressionOfdm2. Details: [docs/specs/test_suite.md](docs/specs/test_suite.md).
