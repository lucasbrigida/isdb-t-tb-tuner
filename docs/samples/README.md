# Amostras de Referência RF (IQ)
Amostras I/Q usadas nos testes de regressão e no `bench_sync`.

## Taxa de amostragem
**2031746 S/s** = $F_{base}/4 = 512/(63 \times 4)$ MHz (Regra 1 do [AGENTS.md](../../AGENTS.md); `include/dsp/constants.hpp`: `SAMPLE_RATE = 2031746`, `SAMPLE_RATE_HZ = 2031746.031746f`). Formato `CU8`: `I` e `Q` intercalados, 8 bits sem sinal (0–255).

## Amostras
| Arquivo | Versionado | Conteúdo |
|---|---|---|
| `samples/ch24_10s_iq.cu8` | ✅ (Git LFS) | Canal 24, 533.142857 MHz, 10 s (~38,8 MB). Usada por `RegressionOfdm1`, `RegressionOfdm2` e `bench_sync` (padrão) |
| `samples/ch24_10s_iq.sha256` | ✅ | `ac92101f279a7fd518d3648c8ac2fbef6fbb0acb5df1c5553b174b93e4db4843` |
| `samples/ch18_2min_iq.meta.json` | ✅ | Metadados da gravação do Canal 18 (497.142857 MHz, ganho 28 dB, 120 s, 243 809 520 amostras, SHA-256 `f614eade…1c50`) |
| `samples/ch18_2min_iq.cu8` / `.sha256` | ❌ | Não versionados (~487 MB). Gerados localmente pelos scripts abaixo |

Saídas de referência (golden files) ficam em `tests/golden/` (`ch24_baseline_ofdm1.*`, `ch24_baseline_ofdm2.*`, `ch18_baseline_ofdm*.*`).

## Plano de frequências (ABNT NBR 15601, `src/radio/frequency_plan.cpp`)
$F_c = 470 + (\text{canal} - 14) \times 6 + 3 + 1/7$ MHz. Ex.: canal 18 → 497.142857 MHz; canal 24 → 533.142857 MHz.

## Gravar o Canal 18
```powershell
.\tools\record_channel18.ps1    # Windows
tools/record_channel18.sh       # Linux / macOS / Git Bash
```
Os scripts gravam `samples/ch18_2min_iq.cu8` (120 s, ganho 28 dB) com o `rtl_sdr`, geram o `.sha256` e o `.meta.json`, e não regravam se o arquivo existente bater com o hash (idempotente).

## Armazenamento
`samples/*.cu8` é rastreado por Git LFS (`.gitattributes`). Rode `git lfs pull` para baixar a `ch24_10s_iq.cu8` antes dos testes de regressão.
