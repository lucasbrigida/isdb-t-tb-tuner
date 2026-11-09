# Compilação, Testes e Release
Para desenvolvedores. Para quem só quer usar o programa, basta a release pronta: veja o [README](../../README.md).

## 1. Pré-requisitos
- **Toolchain:** [w64devkit](https://github.com/skeeto/w64devkit) (GCC + Make) ou MSYS2 MinGW-w64 (`x86_64-w64-mingw32` / `i686-w64-mingw32`), CMake 3.20+. MSVC também é suportado (`-A x64` / `-A Win32`).
- **Bibliotecas:** `librtlsdr`, `fftw3f` (float), VOLK e OpenSSL (Crypto). Binários RTL-SDR por arquitetura ficam em `tools/rtl-sdr/x64/` e `tools/rtl-sdr/x86/`.
- **Hardware/driver:** dongle RTL-SDR com WinUSB via Zadig (veja o [Guia do Usuário](../USER_GUIDE.md#1-hardware-e-driver)).

## 2. Compilar
```powershell
# 64 bits
cmake -S . -B build_x64 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DTARGET_ARCH=x64 -DISDBT_ENABLE_SP_CIR_SYNC=ON
cmake --build build_x64 -j

# 32 bits (SSE2)
cmake -S . -B build_x86 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DTARGET_ARCH=x86 -DISDBT_ENABLE_SP_CIR_SYNC=ON
cmake --build build_x86 -j
```
O executável gerado é `build_<arch>\isdbtb_rx.exe`.

| Opção CMake | Padrão | Descrição |
|---|---|---|
| `CMAKE_BUILD_TYPE=Release` | — | `-O3 -DNDEBUG` (GCC) / `/O2 /DNDEBUG` (MSVC); IPO/LTO automático |
| `TARGET_ARCH` | `auto` | `x64` (`-m64`) ou `x86` (`-m32 -msse2 -mfpmath=sse`) |
| `ISDBT_ENABLE_SP_CIR_SYNC` | OFF | Compila o sincronizador SP-CIR (`--ofdm 2`); alias legado `ISDBT_ENABLE_GRISDBT_SYNC` |
| `ISDBT_ENABLE_FAST_MATH` | OFF | `-ffast-math` / `/fp:fast` |

## 3. Testes (CTest, 27 testes)
```powershell
ctest --test-dir build_x64 --output-on-failure -E BenchSync
```
Detalhes de cada teste: [test_suite.md](../specs/test_suite.md). Ao mudar testes, atualize o badge `Tests-<ok>%2F<total>` em `README.md` e `README_en.md`.

## 4. Release
```powershell
.\scripts\build_release.ps1              # x64 e x86
.\scripts\build_release.ps1 -Arch x64    # só uma arquitetura (x64 | x86)
.\scripts\build_release.ps1 -SkipTests   # também: -SkipBuild, -Clean
```
Compila, roda o CTest e gera `releases\windows\yyyymmdd_v<versão>_win64.zip` / `_win32.zip`, com `isdbtb_rx.exe`, as DLLs, `zadig.exe`, o MPV em `tools\mpv\`, o `LICENSE`, o [RELEASE_README.md](../RELEASE_README.md) (como `README.md`) e o `README_en.md`. Também confere o cabeçalho PE (`objdump -f`) e roda um smoke test do MPV.

## 5. Benchmark (`bench_sync`)
Compara `--ofdm 1` (Van de Beek) e `--ofdm 2` (SP-CIR) sobre uma captura real (padrão `samples/ch24_10s_iq.cu8`):
```powershell
.\build_x64\bench_sync.exe --compare                    # tabela comparativa
.\build_x64\bench_sync.exe --compare --segment-of 5     # janelas de 5 s
.\build_x64\bench_sync.exe --compare --repeat 3         # média de 3 execuções
.\build_x64\bench_sync.exe --compare -i samples/minha_captura.cu8
.\build_x64\bench_sync.exe --ofdm 2                     # um modo só
```
Métricas: `wall_time_s`, `cpu_user_s`, `cycles_per_symbol`, `peak_rss_mb`, `heap_allocs_hot`, `locked_time_ratio`, `sync_loss_count`, `resync_count`, `avg_lock_recovery`, `snr_mean_db`, `valid_ts_packets`.

## 6. Ferramentas (`tools/`, `scripts/`)
| Arquivo | Uso |
|---|---|
| `tools/calibrate_ppm.py` | Estima PPM e offset DC do dongle (`pyrtlsdr`) |
| `tools/quick_rf_scan.py` | Varredura rápida de RF |
| `tools/record_channel18.ps1` / `.sh` | Grava amostras IQ de referência ([docs/samples](../samples/README.md)) |
| `tools/gen_synthetic_iq.py` / `.cpp` | Gera IQ sintético determinístico |
| `tools/analyze_ts_stream.py`, `tools/inspect_tables.py` | Analisam o TS e as tabelas PSI/SI |
| `tools/run_15min_eval.ps1`, `tools/analyze_*_metrics.py`, `tools/compare_15min_eval.py` | Avaliação longa com `--metrics-csv` |
| `tools/freeze_baseline_ofdm1.cpp` | Gera o golden file de regressão do `--ofdm 1` |
| `scripts/play_mpv.bat` / `play_mpv_x86.bat` / `play_mpv.ps1` | Abrem o MPV manualmente em `udp://127.0.0.1:1234` |
