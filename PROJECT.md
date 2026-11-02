# Project: isdb_t_isdb_tb_tuner_player (CLI Native)

Universal native CLI decoder and player for ISDB-T / ISDB-Tb (1-Seg) digital TV signals using RTL-SDR dongles, optimized with SIMD VOLK and FFTW3, streaming MPEG-TS via UDP to embedded MPV or VLC media player.

## Architecture
- **Language / Standard**: Modern C++17, cross-platform (Windows / Linux).
- **Physical Layer (DSP)**:
  - Precise chip sampling rate: 2,031,746 S/s (512/63/4 MHz).
  - Lock-free SPSC USB capture ring buffer (`SpscCaptureQueue<64>`) with atomic sequence tracking and coordinated resets.
  - Zero dynamic heap allocations in hot loop using `FixedDelayLine` and `DynamicDelayLine`.
  - Tripartite OFDM synchronization state machine (`Acquisition`, `Tracking` $\pm 4$ samples, `LostLock`).
  - Cyclic Prefix correlation & Fractional Frequency Offset (FFO) estimation via Van de Beek algorithm with VOLK vector math.
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
  - Embedded Player: Portable MPV integration with Win32 Job Object process management and custom GPU upscaling shaders (`tools/mpv/`).
  - Storage: Channel persistence via JSON (`channels.json`).

## CLI Subcommands
- `probe`: Discover and validate connected RTL-SDR dongles and DSP environment.
- `scan`: Two-pass deep channel scan across UHF frequencies (14 to 69) with automatic SDT station naming.
- `list`: Display saved channels from `channels.json`.
- `play`: Tune to a channel by number or station name, stream live MPEG-TS over UDP, and optionally launch embedded MPV (`--player`).

## Build and Testing
- Build system: CMake 3.20+ with MinGW / GCC / MSVC.
- Build configurations:
  - `-DCMAKE_BUILD_TYPE=Release`: `-O3 -DNDEBUG` (GCC) / `/O2 /DNDEBUG` (MSVC).
  - Interprocedural Optimization (IPO / LTO): Automatically detected and enabled for Release.
  - `-DISDBT_ENABLE_FAST_MATH=ON` (Optional): Fast math optimizations (`-ffast-math` / `/fp:fast`).
- Deterministic CTest unit test suite (10/10 Passed):
  1. `ReedSolomon`: $RS(204, 188)$ decoding with $O(1)$ $GF(2^8)$ table.
  2. `Descrambler`: ISDB-T PRBS frame descrambling.
  3. `Viterbi`: Continuous $K=7$ soft Viterbi decoding.
  4. `FecPipeline`: Integrated FEC chain.
  5. `OfdmSync`: Cyclic prefix autocorrelation & FFO.
  6. `OneSegE2E`: End-to-End One-Seg demodulation pipeline.
  7. `SpscQueue`: Lock-free SPSC capture queue concurrency & sequence drops.
  8. `DelayLine`: `FixedDelayLine` & `DynamicDelayLine` zero-allocation delay structures.
  9. `ResetRecovery`: Coordinated reception reset & phase recovery.
  10. `SyncStateAndCsi`: Tripartite OFDM sync state machine & 128-centered CSI soft scaling.
