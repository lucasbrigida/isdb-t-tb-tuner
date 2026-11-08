# Project: isdb_t_isdb_tb_tuner_player (CLI Native)

Universal native CLI decoder and player for ISDB-T / ISDB-Tb (1-Seg) digital TV signals using RTL-SDR dongles, optimized with SIMD VOLK and FFTW3, streaming MPEG-TS via UDP to embedded MPV or VLC media player.

## Architecture
- **Language / Standard**: Modern C++17, cross-platform (Windows x64 / x86, Linux).
- **Physical Layer (DSP)**:
  - Precise chip sampling rate: 2,031,746 S/s (512/63/4 MHz).
  - Lock-free SPSC USB capture ring buffer (`SpscCaptureQueue<64>`) with atomic sequence tracking and coordinated resets.
  - Zero dynamic heap allocations in hot loop using `FixedDelayLine` and `DynamicDelayLine`.
  - **Dual-Engine OFDM Synchronization** via `IOFDMSynchronizer`:
    - **Mode 1 (`VanDeBeekSync`, Baseline):** Tripartite OFDM synchronization state machine (`Acquisition`, `Tracking` $\pm 4$ samples, `LostLock`), cyclic prefix correlation & Fractional Frequency Offset (FFO) estimation via Van de Beek ML algorithm with SIMD VOLK vector math.
    - **Mode 2 (`SpCirSynchronizer`, Speth & Meyr / gr-isdbt port):** Coarse CP acquisition, active Scattered Pilot (SP-CIR) timing refinement via 64-point IDFT, First Arrival Path detection ($\ge 25\% P_{\max}$), integer CFO estimation via TMCC, fractional CFO tracking with asymptotic IIR and $\pm 0.5\text{ Hz}$ deadband preventing drift under urban multipath, SCO tracking loop, and lock hysteresis.
  - 2048-point FFT via FFTW3 with continuous fine symbol timing tracking.
  - Integer Frequency Offset (IFO) tracking and Continuous/Scattered Pilots (SP/CP) channel equalization ($X^{11} + X^2 + 1$).
  - Regularized MMSE channel equalization with adaptive noise floor.
  - TMCC DBPSK decoding with BCH(273, 191) error correction.
  - Frequency, time, and bit deinterleaving matching ISDB-T Mode 3 specification with synchronized parallel delay lines for CSI weights.
  - 128-centered CSI soft scaling preserving Euclidean decision thresholds in QPSK and 16QAM.
- **FEC Pipeline**:
  - Inner FEC: Continuous rate-adaptive Viterbi decoder ($K=7$, polynomials `0x4F` and `0x6D`) with continuous circular survivor buffer.
  - Outer FEC: Reed-Solomon $RS(204, 188, t=8)$ with Berlekamp-Massey, Chien Search, and Forney algorithm ($FCR=0$) accelerated by $O(1)$ 64 KB direct Galois field table.
  - PRBS Energy Dispersal Descrambler ($1 + X^{14} + X^{15}$) synced to OFDM frame pulse.
- **Transport & Output**:
  - Transport Stream: 188-byte MPEG-TS parsing, PSI/SI table extraction (PAT, PMT, SDT), fallback PAT synthesis for 1-Seg mobile stream compatibility.
  - Streaming: Low-overhead UDP datagram transmission (7 TS packets = 1316 bytes per datagram) targeting `udp://@127.0.0.1:1234` with `flush_if_stale(80)`.
  - Embedded Player: Portable MPV integration with Win32 Job Object process management (native x64 and official i686 32-bit) and custom GPU upscaling shaders.
  - Storage: Channel persistence via JSON (`channels.json`).

## CLI Subcommands
- `probe`: Discover and validate connected RTL-SDR dongles and DSP environment.
- `scan`: Two-pass deep channel scan across UHF frequencies (14 to 69) with automatic SDT station naming.
- `list`: Display saved channels from `channels.json`.
- `play`: Tune to a channel by number or station name, stream live MPEG-TS over UDP, and optionally launch embedded MPV (`--player`).
  - `--ofdm 1` (Default): Lightweight Van de Beek baseline.
  - `--ofdm 2`: Robust SP-CIR mode (Speth & Meyr / gr-isdbt port) for weak, multi-path, or Doppler signals.
  - Note: Accepts both space (`--ofdm 1` / `--ofdm 2`) and equals (`--ofdm=1` / `--ofdm=2`) syntax. `--ofdm` only applies to `play` and is silently ignored on other subcommands.

## Build and Testing
- Build system: CMake 3.20+ with MinGW-w64 (`w64devkit`) / GCC / MSVC.
- Build configurations:
  - `-DTARGET_ARCH=x64` / `-DTARGET_ARCH=x86`: Target architecture; GCC/Clang use `-m64` or `-m32 -msse2 -mfpmath=sse`, while MSVC uses the generator platform `-A x64` or `-A Win32`.
  - `-DISDBT_ENABLE_SP_CIR_SYNC=ON` (Optional): Compiles the SP-CIR synchronizer (`--ofdm=2`). Also accepts `-DISDBT_ENABLE_GRISDBT_SYNC=ON`.
  - `-DCMAKE_BUILD_TYPE=Release`: `-O3 -DNDEBUG` (GCC) / `/O2 /DNDEBUG` (MSVC).
  - Interprocedural Optimization (IPO / LTO): Automatically detected and enabled for Release.
  - `-DISDBT_ENABLE_FAST_MATH=ON` (Optional): Fast math optimizations (`-ffast-math` / `/fp:fast`).
- Deterministic CTest unit test suite (27/27 Passed):
  1. `ReedSolomon`: $RS(204, 188)$ decoding with $O(1)$ $GF(2^8)$ table.
  2. `Descrambler`: ISDB-T PRBS frame descrambling.
  3. `Viterbi`: Continuous $K=7$ soft Viterbi decoding.
  4. `FecPipeline`: Integrated FEC chain.
  5. `OfdmSync`: Cyclic prefix autocorrelation & FFO.
  6. `OneSegE2E`: End-to-End One-Seg demodulation pipeline.
  7. `IqStreamBuffer`: Dynamic continuous I/Q sample buffering across USB reads.
  8. `SpscQueue`: Lock-free SPSC capture queue concurrency & sequence drops.
  9. `DelayLine`: `FixedDelayLine` & `DynamicDelayLine` zero-allocation delay structures.
  10. `ResetRecovery`: Coordinated reception reset & phase recovery.
  11. `SyncStateAndCsi`: Tripartite OFDM sync state machine & 128-centered CSI soft scaling.
  12. `BitDeinterleaverQam16`: 16QAM bit deinterleaver with $0, 40, 80, 120$ symbol delays and CSI soft scaling.
  13. `VolkCpuFeatures`: Dynamic CPU SIMD feature detection (AVX2, AVX, SSE2).
  14. `CliSimdIntegration`: CLI runtime SIMD VOLK vector dispatch.
  15. `FrequencyPlan`: Frequency synthesis and carrier calculations for all 14–69 UHF channels.
  16. `ChannelManager`: In-memory operations and JSON persistence (`channels.json`).
  17. `PsiParser`: MPEG-TS PAT, PMT, and SDT parsing with station name extraction.
  18. `AribB24`: Native ARIB STD-B24 text decoding (Kanji JIS X 0208, Hiragana, Katakana, Gaiji).
  19. `EpgManager`: EPG data management and JSON persistence (`epg.json`).
  20. `UdpStreamer`: IPTV 1316-byte UDP packet framing and transmission.
  21. `CliOfdmFlag`: CLI parsing of `--ofdm` flag (`--ofdm 1`, `--ofdm 2`, `--ofdm=1`, `--ofdm=2`) and silent ignoring outside `play`.
  22. `SyncRegressionScan`: Non-regression verification of channel scanner under Van de Beek baseline.
  23. `BenchAsciiTable`: ASCII table formatting for benchmark telemetry reports.
  24. `RegressionOfdm1`: Van de Beek baseline regression test on reference broadcast dataset.
  25. `SyncSpCir`: Math and FSM verification of canonical SP-CIR synchronizer.
  26. `SyncGrisdbtRobust`: Backward-compatibility alias verification for `RobustGRISDBTSync`.
  27. `RegressionOfdm2`: SP-CIR synchronizer regression test on reference broadcast dataset (`ch24_10s_iq.cu8`), ensuring 2 valid TS packets.
