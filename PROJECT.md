# Project: isdb_t_isdb_tb_tuner_player (CLI Native)

Universal native CLI decoder and player for ISDB-T / ISDB-Tb (1-Seg) digital TV signals using RTL-SDR dongles, optimized with SIMD VOLK and FFTW3, streaming MPEG-TS via UDP to VLC media player.

## Architecture
- **Language / Standard**: Modern C++17, cross-platform (Windows / Linux).
- **Physical Layer (DSP)**:
  - Precise chip sampling rate: 2,031,746 S/s (512/63/4 MHz).
  - Cyclic Prefix correlation & Fractional Frequency Offset (FFO) estimation via Van de Beek algorithm with VOLK vector math.
  - 2048-point FFT via FFTW3 with continuous fine symbol timing tracking.
  - Integer Frequency Offset (IFO) tracking and Continuous/Scattered Pilots (SP/CP) channel equalization ($X^{11} + X^2 + 1$).
  - TMCC DBPSK decoding with BCH(273, 191) error correction.
  - Frequency, time, and bit deinterleaving matching ISDB-T Mode 3 specification.
- **FEC Pipeline**:
  - Inner FEC: Continuous rate-adaptive Viterbi decoder ($K=7$, polynomials `0x4F` and `0x6D`).
  - Outer FEC: Reed-Solomon $RS(204, 188, t=8)$ with Berlekamp-Massey and Forney algorithm ($FCR=0$).
  - PRBS Energy Dispersal Descrambler ($1 + X^{14} + X^{15}$) synced to OFDM frame pulse.
- **Transport & Output**:
  - Transport Stream: 188-byte MPEG-TS parsing, PSI/SI table extraction (PAT, PMT, SDT), fallback PAT synthesis for 1-Seg mobile stream compatibility.
  - Streaming: Low-overhead UDP datagram transmission (7 TS packets = 1316 bytes per datagram) targeting `udp://@127.0.0.1:1234`.
  - Storage: Channel persistence via JSON (`channels.json`).

## CLI Subcommands
- `probe`: Discover and validate connected RTL-SDR dongles and DSP environment.
- `scan`: Two-pass deep channel scan across UHF frequencies (14 to 69) with automatic SDT station naming.
- `list`: Display saved channels from `channels.json`.
- `play`: Tune to a channel by number or station name and stream live MPEG-TS over UDP.

## Build and Testing
- Build system: CMake 3.20+ with MinGW / GCC.
- Deterministic CTest unit test suite covering Reed-Solomon, Descrambler, Viterbi, FEC pipeline, OFDM synchronization, and End-to-End One-Seg demodulation.
