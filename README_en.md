# 📡 ISDB-T / ISDB-Tb Universal SDR Receiver & Real-Time Player

<p align="center">
  <img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License: GPLv3" />
  <img src="https://img.shields.io/badge/C%2B%2B-17%20%2F%2020-00599C.svg?logo=c%2B%2B" alt="C++17/20" />
  <img src="https://img.shields.io/badge/Hardware-RTL--SDR%20(RTL2832U)-red.svg" alt="RTL-SDR" />
  <img src="https://img.shields.io/badge/Platform-Windows%20(x64%20%7C%20x86)%20%7C%20Linux-brightgreen.svg" alt="Platforms" />
  <img src="https://img.shields.io/badge/Player-MPV%20Embedded-darkgreen.svg" alt="MPV Embedded" />
  <img src="https://img.shields.io/badge/Tests-20%2F20%20Passed-success.svg" alt="CTest Passing" />
</p>

Complete digital broadcast TV receiver and demodulator for the **ISDB-T / ISDB-Tb / SBTVD** standard (Brazil, Japan, Philippines, and Latin America) powered by **RTL-SDR** hardware (RTL2832U with R820T/R828D/E4000 tuners). Built in **pure, high-performance native C++ without GNU Radio dependencies**, featuring a **lock-free SPSC ring buffer** with sequence tracking and coordinated resets, **zero heap allocations** in symbol processing, a **dual-engine OFDM synchronization architecture** (standard Van de Beek and robust `gr-isdbt` port with active timing, CFO/SCO, and re-sync hysteresis), **Regularized MMSE** channel equalization, **128-centered CSI soft scaling** with synchronized bit delay lines in the continuous Viterbi decoder, **$O(1)$ 64 KB direct Galois field table** in Reed-Solomon, smart two-pass spectrum scan with official station name resolution via **SDT**, **dual-target Windows releases (64-bit x64 & 32-bit x86)**, and an **embedded MPV player with real-time super-resolution upscaling and motion interpolation**.

---

<p align="center">
  <b>Language / Idioma:</b><br/>
  <a href="README.md">🇧🇷 Português (Brasil)</a> &nbsp;|&nbsp; 
  <b>🇺🇸 English</b>
</p>

---

## 🎬 Real-Time Demonstrations & Hardware Inspection

Explore the receiver across its primary operating modes. Click on any thumbnail below to watch the video or view the image in full resolution:

|                                                                                                                                                                                     📡 **Automated Channel Scan (`scan`)**                                                                                                                                                                                     |                                                                                                                                                                                          📺 **Tuning & Playback (`play`)**                                                                                                                                                                                           |
| :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------: | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------: |
|       <a href="docs/videos/scan.mp4" title="Click to watch scan video"><img src="docs/videos/scan_poster.png" alt="Channel scanning scan" width="100%" style="border-radius: 6px; border: 1px solid #333;" /></a><br>▶️ <strong>Video:</strong> <em>Fast two-pass UHF scan (channels 14–69) with deep SDT station resolution (<a href="docs/videos/scan.mp4"><code>docs/videos/scan.mp4</code></a>)</em>       |    <a href="docs/videos/play.mp4" title="Click to watch play video"><img src="docs/videos/play_poster.png" alt="Tuning and playback play" width="100%" style="border-radius: 6px; border: 1px solid #333;" /></a><br>▶️ <strong>Video:</strong> <em>Live playback in embedded MPV with EWA-Lanczos upscaling and motion interpolation (<a href="docs/videos/play.mp4"><code>docs/videos/play.mp4</code></a>)</em>    |
|                                                                                                                                                                                     **🎬 Full Demodulator Demo (`demo`)**                                                                                                                                                                                      |                                                                                                                                                                                         **🔍 Hardware Inspection (`probe`)**                                                                                                                                                                                         |
| <a href="docs/videos/demo.mp4" title="Click to watch full demo video"><img src="docs/videos/demo_poster.png" alt="Full demodulator real-time demo" width="100%" style="border-radius: 6px; border: 1px solid #333;" /></a><br>▶️ <strong>Video:</strong> <em>Continuous RF demodulation at 2.0317 MSps streaming live broadcast TV (<a href="docs/videos/demo.mp4"><code>docs/videos/demo.mp4</code></a>)</em> | <a href="docs/images/probe.png" title="Click to view full probe image"><img src="docs/images/probe.png" alt="Hardware probe inspection" width="100%" style="border-radius: 6px; border: 1px solid #333;" /></a><br>📷 <strong>Image:</strong> <em>RTL-SDR dongle inspection, tuner RF gain table, and VOLK/SIMD acceleration detection (<a href="docs/images/probe.png"><code>docs/images/probe.png</code></a>)</em> |

---

## 📑 Table of Contents

- [⚡ 1. End-User Quickstart (Download & Run)](#-1-end-user-quickstart-download--run)
  - [1.1 Download the Standalone Release (x64 / x86)](#11-download-the-standalone-release-x64--x86)
  - [1.2 USB Driver Installation (Zadig in 1 Click)](#12-usb-driver-installation-zadig-in-1-click)
  - [1.3 Essential CLI Commands](#13-essential-cli-commands)
  - [1.4 Embedded MPV Player Features & Hotkeys](#14-embedded-mpv-player-features--hotkeys)
- [🛠️ 2. Developer Guide (Build & Tests)](#️-2-developer-guide-build--tests)
  - [2.1 Prerequisites & Recommended Toolchain (w64devkit)](#21-prerequisites--recommended-toolchain-w64devkit)
  - [2.2 Building Natively for 64-bit and 32-bit](#22-building-natively-for-64-bit-and-32-bit)
  - [2.3 Deterministic Unit Test Suite (CTest - 20/20 Tests Passed)](#23-deterministic-unit-test-suite-ctest---2020-tests-passed)
  - [2.4 Automated Release Packaging (`build_release.ps1`)](#24-automated-release-packaging-build_releaseps1)
  - [2.5 Developer & Diagnostic Utilities (`tools/`, `bench/`, `samples/`)](#25-developer--diagnostic-utilities-tools-bench-samples)
  - [2.6 Codebase Architecture Map](#26-codebase-architecture-map)
- [🔬 3. Physical Layer (DSP) & Mathematical Innovations](#-3-physical-layer-dsp--mathematical-innovations)
  - [3.1 Physical Layer DSP Summary](#31-physical-layer-dsp-summary)
  - [3.2 Specialized Technical Documentation](#32-specialized-technical-documentation)
- [🤝 4. Reference Projects & Acknowledgments](#-4-reference-projects--acknowledgments)
- [📜 5. License](#-5-license)

---

## ⚡ 1. End-User Quickstart (Download & Run)

If you simply want to tune and watch digital TV on your computer with an RTL-SDR dongle, follow these three steps:

### 1.1 Download the Standalone Release (x64 / x86)

Download the latest portable release archive for your operating system architecture:

- 📦 **Windows x64 (64-bit):** [`releases/windows/20261102_v1.0.0_win64.zip`](releases/windows/20261102_v1.0.0_win64.zip) _(or from [GitHub Releases](../../releases))_
  - Contains 64-bit native binaries, 64-bit RTL-SDR runtime DLLs, Zadig, and 64-bit portable MPV player.
- 📦 **Windows x86 (32-bit):** [`releases/windows/20261102_v1.0.0_win32.zip`](releases/windows/20261102_v1.0.0_win32.zip) _(or from [GitHub Releases](../../releases))_
  - Contains 32-bit native binaries, official 32-bit RTL-SDR DLLs, Zadig, and official 32-bit MPV player (`i686`).
- Extract the `.zip` archive to any folder on your computer. All required binaries, runtime DLLs, the Zadig utility, and the embedded MPV player with custom shaders are included out of the box.

### 1.2 USB Driver Installation (Zadig in 1 Click)

To allow the CLI application to communicate directly with the RTL2832U chip:

1. Plug your RTL-SDR dongle and a UHF antenna into an available USB port.
2. Double-click the **`zadig.exe`** file included in the extracted folder (grant Administrator privileges if prompted).
3. From the **Options** menu, click **List All Devices**.
4. In the main dropdown menu, select **`Bulk-In, Interface (Interface 0)`** _(or `RTL2832U` / `RTL2838UHIDIR`)_.
   > ⚠️ **Important:** If your device lists `Interface 0` and `Interface 1`, **always choose `Interface 0`** (the high-speed RF streaming interface; Interface 1 is only for the infrared remote receiver).
5. Ensure the target driver to the right of the green arrow is set to **`WinUSB`**, then click **`Install Driver`** (or `Replace Driver`).
6. Wait for the confirmation message: _"The driver was installed successfully"_. You are now ready to run!

### 1.3 Essential CLI Commands

Open PowerShell or the Command Prompt (CMD) in the extracted folder:

```powershell
# 1. Probe connected hardware and print supported gain values:
.\isdbtb_rx.exe probe

# 2. Scan over-the-air UHF channels (channels 14 to 69):
#    (The smart two-pass scan automatically resolves station names via the SDT table)
.\isdbtb_rx.exe scan

# 3. List stored channels:
.\isdbtb_rx.exe list

# 4. Tune a channel (e.g., Channel 23) and automatically open the embedded MPV player:
.\isdbtb_rx.exe play -c 23 --player

# 5. Tune with robust synchronization mode (--ofdm=2) for weak or fading signals:
.\isdbtb_rx.exe play -c 23 --ofdm=2 --player

# 6. Tune with detailed real-time telemetry and signal status (-v / --verbose):
.\isdbtb_rx.exe play -c 23 --player -v
```

#### 📶 Choosing the Synchronization Algorithm (`--ofdm=<N>`):
The `play` subcommand supports two OFDM synchronization algorithms via the `--ofdm` flag:

```text
Usage: play [--ofdm=<mode>]

  --ofdm=1   Default mode. Lightweight and efficient (Van de Beek ML baseline).
  --ofdm=2   Robust mode. Keeps video stable under weak signals, heavy
             multipath reflections, or Doppler fading. Uses more CPU.

Without the option, mode 1 is used.
--ofdm only applies to 'play' and is silently ignored in other commands.

Examples:
  play -c 23 --player
  play -c 23 --ofdm=2 --player
```

> 💡 **Verbose Mode (`-v`, `--verbose`):** By default, the `play` command keeps a clean and static display. Pass `-v` or `--verbose` to display real-time signal metrics and DSP status (RSSI, OFDM/TMCC lock, SNR, TS packets, and RS correction stats).

> 💡 **Synchronized Process Lifecycle:** The MPV player process is managed via a Windows kernel _Job Object_. When you exit the CLI (by pressing `Ctrl+C` or closing the terminal window), the player closes instantly without leaving orphan background processes.

### 1.4 Embedded MPV Player Features & Hotkeys

The embedded portable MPV player (`tools/mpv/` or `tools/mpv/x86/`) is pre-configured (`mpv.conf`) with custom graphics pipelines tailored specifically for 1-Seg mobile broadcasts (originally 320x240 or 320x180):

- **Spatial Super-Resolution:** `ewa_lanczossharp` with `sigmoid-upscaling` produces sharp, ringing-free edges when upscaling to 1080p, 1440p, or 4K monitors.
- **Dynamic Debanding:** Smooths out color banding and compression macroblocks typical of low-bitrate H.264 mobile streams.
- **Motion Interpolation (`display-resample`):** Synchronizes video frames with your display's native refresh rate (60Hz, 120Hz, 144Hz, or 240Hz), eliminating motion judder.

| Shortcut Key  | Action in MPV                                                        |
| :-----------: | -------------------------------------------------------------------- |
|    **`f`**    | Toggle **Full Screen**                                               |
|    **`i`**    | Display real-time OSD statistics, active resolution, and frame rates |
|    **`d`**    | Toggle Deband filter On / Off                                        |
|    **`m`**    | Toggle Audio Mute                                                    |
| **`9` / `0`** | Decrease / Increase Volume                                           |
|    **`q`**    | Quit the player                                                      |

---

## 🛠️ 2. Developer Guide (Build & Tests)

### 2.1 Prerequisites & Recommended Toolchain (w64devkit)

For native Windows compilation with maximum speed and without the bloat of heavy environments like MSYS2 or Visual Studio, the project officially adopts and recommends **[`w64devkit`](https://github.com/skeeto/w64devkit/releases)**:

- **What is [`w64devkit`](https://github.com/skeeto/w64devkit/releases):** A portable, self-contained, instant-startup C and C++ multilib development kit for Windows maintained by Christopher Wellons (_skeeto_). It includes GCC, G++, GDB, GNU Make, BusyBox (providing a clean POSIX environment with `sh`, `awk`, `sed`, `grep`), and MinGW-w64 runtime headers supporting C++17 and C++20 with both 64-bit and 32-bit multilib cross-compilation (`-m32` / `-m64`).
- **How to set up w64devkit in 2 minutes:**
  1. Download the latest release (`w64devkit-x.y.z.zip`) from the official release page: [github.com/skeeto/w64devkit/releases](https://github.com/skeeto/w64devkit/releases).
  2. Extract the archive to a local folder (e.g. `C:\w64devkit`).
  3. Add `C:\w64devkit\bin` to your system `PATH` environment variable (or launch a terminal via `w64devkit.exe`).
- **Build System:** [CMake 3.20+](https://cmake.org/download/) installed and available in `PATH`.
- **Radio & Math Acceleration Libraries:**
  - `librtlsdr` and `libusb-1.0` (headers and import libraries included in `deps/`, official pre-built DLLs for x64 and x86 in `tools/rtl-sdr/`).
  - `FFTW3` (fast Fourier transforms of 2048, 4096, and 8192 points).
  - `VOLK` (vectorized floating-point math with AVX2, FMA, and SSE SIMD kernels).
- _(Optional for scripting & testing):_ Python 3.8+ and PowerShell 7+.

### 2.2 Building Natively for 64-bit and 32-bit

In a Windows terminal with MinGW-w64 (e.g. `w64devkit`):

#### 64-bit Build (x64):
```powershell
# 1. Configure CMake for 64-bit Release with robust gr-isdbt sync enabled:
cmake -S . -B build_x64 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DTARGET_ARCH=x64 -DISDBT_ENABLE_GRISDBT_SYNC=ON

# 2. Compile binaries and test suite:
cmake --build build_x64 -j
```

#### 32-bit Build (x86):
```powershell
# 1. Configure CMake for 32-bit Release with SSE2 math acceleration:
cmake -S . -B build_x86 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DTARGET_ARCH=x86 -DISDBT_ENABLE_GRISDBT_SYNC=ON

# 2. Compile binaries and test suite:
cmake --build build_x86 -j
```

#### CMake Build Flags & Optimization Options
- **`-DTARGET_ARCH=x64` / `-DTARGET_ARCH=x86`** _(Default: `auto`)_: Sets target binary architecture (`-m64` or `-m32 -msse2 -mfpmath=sse`).
- **`-DISDBT_ENABLE_GRISDBT_SYNC=ON`** _(Default: OFF)_: Enables compilation of the robust `gr-isdbt` OFDM synchronization engine (`--ofdm=2`).
- **`-DCMAKE_BUILD_TYPE=Release`**: Enforces `-O3 -DNDEBUG` (GCC/Clang) or `/O2 /DNDEBUG` (MSVC), stripping runtime assertions and maximizing loop unrolling.
- **`-DISDBT_ENABLE_FAST_MATH=ON`** _(Default: OFF)_: Injects `-ffast-math` or `/fp:fast` to enable aggressive AVX2/FMA vectorization across MMSE equalization, NCO phase accumulation, and soft metric calculations.
- **Interprocedural Optimization (IPO / LTO)**: Automatically checked and enabled for Release configurations (`CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON`), allowing cross-translation-unit inlining of hot-path functions (`ReedSolomon::gf_mul`, `FixedDelayLine::push_and_get`, `SpscCaptureQueue::try_acquire_read`).

### 2.3 Deterministic Unit Test Suite (CTest - 20/20 Tests Passed)

All core physical layer, FEC, CLI, and synchronization blocks feature deterministic mathematical test suites that run in fractions of a second without requiring physical radio hardware:

```powershell
# Run the entire deterministic test suite via CTest:
ctest --test-dir build_x64 --output-on-failure -E BenchSync
```

The 20 deterministic tests validate:

1. `ReedSolomon`: Corrects up to 8 erroneous bytes per packet and asserts failure detection with >8 errors using Berlekamp-Massey, Chien Search, and $O(1)$ direct $GF(2^8)$ multiplication.
2. `Descrambler`: Verifies the 15-bit ISDB-T energy dispersal LFSR with frame resets and continuous clocking over sync byte `0x47`.
3. `Viterbi`: Continuous $K=7$ convolutional decoding across punctured rates 1/2, 2/3, 3/4, 5/6, and 7/8 with static circular history ring buffer.
4. `FecPipeline`: Full inner-to-outer FEC integration (Viterbi $\to$ Byte Deinterleaver $\to$ Descrambler $\to$ Reed-Solomon).
5. `OfdmSync`: Van de Beek ML cyclic prefix correlation estimator and continuous fractional frequency offset (FFO) tracking with SIMD.
6. `OneSegE2E`: Simulated end-to-end 1-Seg pipeline with QPSK modulation, rate 2/3, guard interval 1/8, and time deinterleaving $I=4$.
7. `IqStreamBuffer`: Validates dynamic continuous I/Q sample buffering across USB reads.
8. `SpscQueue`: Validates lock-free SPSC circular queue (64 slots), thread concurrency, drop accounting, and temporal gap detection.
9. `DelayLine`: Validates `FixedDelayLine` and `DynamicDelayLine` zero-heap memory stability, delay bounds, and deterministic resets.
10. `ResetRecovery`: Verifies coordinated stream reset and NCO phase recovery following simulated USB transfer drops.
11. `SyncStateAndCsi`: Validates the three-state OFDM synchronization engine (`Acquisition` $\to$ `Tracking` $\to$ `LostLock`) and 128-centered CSI soft metric scaling.
12. `FrequencyPlan`: Frequency synthesis and carrier calculations for all 14–69 UHF channels.
13. `ChannelManager`: In-memory operations and JSON persistence (`channels.json`) across platforms.
14. `PsiParser`: MPEG-TS PAT, PMT, and SDT parsing with station name extraction and fallback PAT synthesis.
15. `CliOfdmFlag`: Validates `--ofdm=<1|2>` CLI argument parsing, strict rejection of invalid values, and silent ignoring outside `play`.
16. `SyncRegressionScan`: Ensures channel scanner operates strictly under baseline Van de Beek synchronization with zero regression.
17. `BenchAsciiTable`: Validates ASCII table formatting for benchmark telemetry reports.
18. `RegressionOfdm1`: Baseline Van de Beek regression test anchored to reference broadcast capture.
19. `SyncGrisdbtRobust`: Validates the robust `gr-isdbt` synchronization engine (active SP timing, integer/fractional CFO, SCO, and FSM hysteresis).
20. `RegressionOfdm2`: Robust `gr-isdbt` synchronization regression test anchored to reference broadcast capture.

### 2.4 Automated Release Packaging (`build_release.ps1`)

The repository includes a PowerShell automation script for compiling, testing, packaging, and strictly verifying standalone release archives for both 64-bit and 32-bit architectures:

```powershell
# Generate releases for all architectures (x64 and x86):
pwsh -ExecutionPolicy Bypass -File scripts/build_release.ps1 -Platform windows -Arch all

# Generate release for 64-bit only:
pwsh -ExecutionPolicy Bypass -File scripts/build_release.ps1 -Platform windows -Arch x64

# Generate release for 32-bit only:
pwsh -ExecutionPolicy Bypass -File scripts/build_release.ps1 -Platform windows -Arch x86
```

The script stages the binaries, DLLs, Zadig, architecture-specific embedded MPV, and documentation into `releases/windows/yyyymmdd_v<version>_win64.zip` and `releases/windows/yyyymmdd_v<version>_win32.zip`, and **strictly validates** the PE architecture (`pei-x86-64` vs. `pei-i386`) using `objdump -f`.

### 2.5 Developer & Diagnostic Utilities (`tools/`, `bench/`, `samples/`)

The repository hosts a complete ecosystem of standalone tools and reference utilities for RF bench testing, USB hardware diagnosis, and high-performance playback:

| Utility / Directory | Technical Description & Developer Use Cases |
| :--- | :--- |
| **[`bench/`](bench/)** | **Benchmark Suite (`bench_sync`):** Compares execution time, CPU load, and synchronizer locking stability across OFDM algorithms against reference broadcast captures. |
| **[`samples/`](samples/)** | **RF Reference Captures:** Contains the official 2-minute Channel 18 reference dataset (`ch18_2031746sps_2min.cu8`) sampled at exact 2031746 S/s for deterministic regression testing (managed via Git LFS). |
| **[`tools/zadig.exe`](tools/zadig.exe)** | Official portable **WinUSB** driver installer (v2.9 / libwdi). Links the WinUSB driver to `Interface 0` of the RTL2832U device with 1 click. |
| **[`tools/rtl-sdr/`](tools/rtl-sdr/)** | Complete collection of official **Osmocom RTL-SDR** diagnostic binaries and runtime libraries: |
| ↳ `x64/` & `x86/` | Dedicated architecture folders containing 64-bit and 32-bit `librtlsdr.dll` and diagnostic tools. |
| ↳ `rtl_test.exe` | **USB Stability & Benchmark Tool:** Tests USB bus bandwidth and tuner PLL lock stability at the exact ISDB-T sampling rate (2031746 S/s):<br>`.\tools\rtl-sdr\x86\rtl_test.exe -s 2031746 -t` |
| ↳ `rtl_sdr.exe` | **Raw RF I/Q Recorder:** Captures raw RF samples directly from the air to `.raw` binary files for offline simulation. |
| ↳ `rtl_power.exe` | **FFT Spectrum Analyzer:** Measures broadband RF power spectral density and thermal noise floor across the entire UHF broadcast band (470 to 806 MHz). |
| ↳ `rtl_tcp.exe` | **Network I/Q Streaming Server:** Streams raw digitized I/Q data over TCP/IP sockets for remote SDR processing. |
| **[`tools/mpv/`](tools/mpv/)** | Embedded **MPV Media Player (x64)** distribution with custom GPU scaling shaders. |
| **[`tools/mpv/x86/`](tools/mpv/x86/)** | Official embedded **MPV Media Player (i686 32-bit)** distribution for 32-bit Windows systems. |

### 2.6 Codebase Architecture Map

```text
isdb_t_isdb_tb_tuner_player/
├── CMakeLists.txt              # CMake build configuration (TARGET_ARCH, ISDBT_ENABLE_GRISDBT_SYNC)
├── LICENSE                     # GNU General Public License v3.0 (GPLv3)
├── README.md                   # Brazilian Portuguese documentation
├── README_en.md                # English documentation
├── AGENTS.md                   # Strict architectural invariants for AI & human developers
├── PROJECT.md                  # Project architecture roadmap and specifications
├── include/
│   ├── sync/                   # IOFDMSynchronizer interface, VanDeBeekSync, RobustGRISDBTSync, SyncFactory
│   ├── dsp/                    # Physical layer DSP (OfdmSync, OneSegDemod, Viterbi, RS, etc.)
│   │   ├── constants.hpp       # Fundamental ISDB-T constants (Fs = 2031746 S/s, soft neutral 128)
│   │   ├── delay_line.hpp      # FixedDelayLine and DynamicDelayLine (zero heap allocations)
│   │   ├── spsc_capture_queue.hpp # Lock-free SPSC ring buffer (64 slots) with gap detection
│   │   └── dsp_metrics.hpp     # DSP runtime performance and telemetry struct
│   ├── streaming/              # UdpStreamer (1316-byte datagrams for MPV/VLC)
│   ├── mpegts/                 # PSI/SI parsers (PAT, PMT, SDT) and test pattern generator
│   ├── cli/                    # ChannelManager and JSON persistence
│   └── third_party/            # CLI11, nlohmann/json
├── src/
│   ├── main.cpp                # CLI entry point, lock-free SPSC USB reader, MPV Job Object
│   ├── sync/                   # OFDM synchronizer implementations (VanDeBeekSync, RobustGRISDBTSync)
│   └── ...                     # C++ implementations for DSP, MPEG-TS, and streaming
├── bench/                      # bench_sync benchmark tool and telemetry reporter
├── samples/                    # Reference RF captures (Channel 18, 2031746 S/s, Git LFS)
├── tools/
│   ├── zadig.exe               # Official USB driver installation utility (WinUSB v2.9)
│   ├── mpv/                    # Embedded MPV portable player (x64) with custom scaling shaders
│   │   └── x86/                # Embedded official MPV player (i686 32-bit)
│   └── rtl-sdr/                # Official Osmocom RTL-SDR diagnostic tools & DLLs (x64 / x86)
├── scripts/
│   ├── build_release.ps1       # Dual-architecture release automation and strict PE validation
│   ├── play_mpv.bat            # Helper script for manual MPV playback
│   └── calibrate_ppm.py        # Local oscillator PPM calibration tool running at exact 2031746 S/s
├── releases/                   # Ready-to-use standalone release packages (win64.zip / win32.zip)
├── docs/                       # Technical architecture decisions (ADR 001 to 025) and specifications
└── tests/                      # 20 deterministic CTest test cases and regression suites
```

---

## 🔬 3. Physical Layer (DSP) & Mathematical Innovations

### 3.1 Physical Layer DSP Summary

- **Exact Fundamental Sampling Rate ($2031746\text{ S/s}$):** Exactly $1/4$ of the ISDB-T chip rate $F_{base} = 512/63\text{ MHz} \approx 8.126984\text{ MHz}$, ensuring $>90\%$ cyclic prefix correlation and perfectly aligned FFT bins without rational resampling filters.
- **Dual-Engine OFDM Synchronization:**
  - *Mode 1 (`VanDeBeekSync`, Baseline):* Sliding cyclic prefix autocorrelation with fractional frequency offset (FFO) estimation and three-state FSM (`Acquisition`, `Tracking` $\pm 4$, `LostLock`).
  - *Mode 2 (`RobustGRISDBTSync`, gr-isdbt port):* Coarse CP acquisition, active Scattered Pilot (SP) timing refinement, integer CFO estimation via TMCC, fractional CFO via SP, sampling clock offset (SCO) phase accumulation, and 3-symbol lock hysteresis.
- **Lock-Free SPSC USB Capture Queue (`SpscCaptureQueue`):** Static 64-slot ring buffer with acquire-release memory semantics, atomic drop counters, and explicit sequence gap detection triggering coordinated NCO resets.
- **Zero Dynamic Allocations in Hot Path:** Complete elimination of `std::deque` and `std::vector::erase` across time, bit, and Forney deinterleavers, replaced with `FixedDelayLine`, `DynamicDelayLine`, and offset sliding (`viterbi_stream_offset_`).
- **Regularized MMSE Channel Equalization:** Replaces traditional Zero-Forcing with adaptive MMSE ($\sigma^2 = \max(10^{-5}, 0.05 \cdot P_{\text{avg}})$), preventing severe noise amplification in deep fading nulls.
- **128-Centered CSI Soft Scaling with Parallel Delay Lines:** Euclidean soft-bits scaled around neutral 128 with dedicated delay lines for CSI weights parallel to data (120-symbol delay on MSB), eliminating 16QAM decision distortion and yielding **+2 to 3 dB SNR gain** under Rayleigh fading.
- **Outer FEC Reed-Solomon with $O(1)$ 64 KB Direct Table:** Scalar multiplications in $GF(2^8)$ executed in a single clock cycle without conditional branch mispredictions, drastically accelerating Berlekamp-Massey, Chien Search, and Forney.
- **DBPSK TMCC + BCH(273, 191) Synchronizer:** Continuous tracking of the 204-symbol frame sync across subcarriers 101, 131, 286, and 349, dynamically configuring physical layer modulation and interleaving parameters.

### 3.2 Specialized Technical Documentation

For detailed mathematical formulations, architectural trade-offs, and deep dives, refer to the documents inside the [`docs/`](docs/) directory:

| Document | Content & Purpose |
| :--- | :--- |
| 🏛️ [`docs/ARCHITECTURE_DECISIONS.md`](docs/ARCHITECTURE_DECISIONS.md) | **Architecture Decision Records (ADR 001 to 025):** Complete history of all algorithms used and replaced (IOFDMSynchronizer, Dual-Arch CMake, Lock-Free SPSC, Zero-Allocation DelayLines, 128-Centered CSI, 3-State OFDM Sync, O(1) GF Table, IPO/LTO, MMSE, Two-Pass Scan, MPV Job Object, 1316-byte UDP MTU alignment, and bug fixes against `gr-isdbt`) with their full mathematical justifications. |
| 📖 [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md) | **In-Depth User Guide:** Step-by-step setup instructions, advanced CLI options, `--ofdm` tuning guide, manual RF gain tuning, and troubleshooting FAQ. |
| 📐 [`docs/blueprint_isdbt_decoder.md`](docs/blueprint_isdbt_decoder.md) | **ISDB-T Engineering Blueprint:** Standard specifications (ABNT NBR 15601/15603 and ARIB STD-B31), interleavers, and frame structures. |
| 🤖 [`AGENTS.md`](AGENTS.md) | **AI Agent & Developer Guide:** Non-negotiable architectural invariants ("Do Not Break"), historical bug registry (Bugs 1–20), and absolute compliance directives. |
| 📊 [`docs/samples/README.md`](docs/samples/README.md) | **RF Reference Dataset Documentation:** Channel 18 reference capture parameters, sampling rates, and verification hashes. |

---

## 🤝 4. Reference Projects & Acknowledgments

This project is built upon the invaluable contributions and specifications of the open-source SDR community:

- **[`git-artes/gr-isdbt`](https://github.com/git-artes/gr-isdbt):** Academic reference ISDB-T implementation for GNU Radio by Federico Larroca and Martin Gomez (Universidad de la República, Uruguay). Their interleaving polynomials, PRBS sequences, and trellis definitions served as the foundational model for our native C++ core.
- **[`librtlsdr`](https://github.com/steve-m/librtlsdr):** Open-source driver for Realtek RTL2832U devices, maintained by Steve Markgraf and Dimitri Stolnikov.
- **[`MPV Player`](https://github.com/mpv-player/mpv):** Highly extensible media player powering our integrated real-time video upscaling subsystem.
- **[`Zadig / libwdi`](https://github.com/pbatard/libwdi):** Essential tool developed by Pete Batard for clean WinUSB driver installation on Windows.
- **[`VOLK`](https://github.com/gnuradio/volk) & [`FFTW3`](https://www.fftw.org/):** Vectorized floating-point math acceleration and high-performance Fourier transform engines.
- **[`CLI11`](https://github.com/CLIUtils/CLI11) & [`nlohmann/json`](https://github.com/nlohmann/json):** Modern C++ libraries for CLI parsing and JSON configuration serialization.

---

## 📜 5. License

This project is free software licensed under the terms of the **GNU General Public License v3.0 (GPL-3.0-or-later)**.  
See the [`LICENSE`](LICENSE) file for the full license text.
