# Índice Mestre da Documentação
Mapa de navegação para devs e agentes de IA: leia só o necessário.

## Guia rápido
| Se você precisa... | Consulte |
|---|---|
| Regras invioláveis e limites físicos | [agents/invariants.md](agents/invariants.md) |
| Evitar erros já corrigidos (Viterbi, Forney, PRBS, IFO) | [agents/error_history.md](agents/error_history.md) |
| Proibições, flags CLI, convenções | [agents/guidelines.md](agents/guidelines.md) |
| ADRs 001–028 | [adr/README.md](adr/README.md) |
| Matemática do DSP (FFT, Van de Beek, SP-CIR, CSI, TMCC) | [specs/dsp_pipeline.md](specs/dsp_pipeline.md) |
| Classes e arquivos C++ | [specs/code_map.md](specs/code_map.md) |
| Testes unitários/integração | [specs/test_suite.md](specs/test_suite.md) |
| Compilar (x86/x64, MinGW, MSVC, dependências) | [user/installation.md](user/installation.md) |
| Sintaxe da CLI (`scan`, `play`, `probe`...) | [user/cli_reference.md](user/cli_reference.md) |
| Guias de usuário e release | [USER_GUIDE.md](USER_GUIDE.md), [RELEASE_README.md](RELEASE_README.md) |

## Estrutura
- **`agents/`** (regras para IA)
  - [invariants.md](agents/invariants.md): 12 invariantes (2031746 S/s, UDP 1316 B, zero heap no DSP, CSI centrado em 128, `./channels.json`...).
  - [error_history.md](agents/error_history.md): 29 erros corrigidos no alinhamento com gr-isdbt (Viterbi, Chien/Forney, descrambler sem `0xB8`, PLL R820T...).
  - [guidelines.md](agents/guidelines.md): 21 proibições (regressões, remoção de logs, mudar taxa, GNU Radio...).
- **`adr/`**: [README.md](adr/README.md) com os 28 ADRs (contexto, decisão, consequências) em arquivo único.
- **`specs/`**
  - [dsp_pipeline.md](specs/dsp_pipeline.md): cadeia 1-Seg (amostragem, Van de Beek vs SP-CIR, FFT, MMSE, demapeamento, CSI, Forney, RS).
  - [code_map.md](specs/code_map.md): módulos C++ (`src/dsp/`, `src/sync/`, `src/mpegts/`, `src/streaming/`, `src/cli/`...).
  - [test_suite.md](specs/test_suite.md): testes unitários, bitstream sintético, I/Q real.
  - [blueprint_isdbt_decoder.md](blueprint_isdbt_decoder.md): blueprint histórico de engenharia/planejamento.
- **`user/`**
  - [cli_reference.md](user/cli_reference.md): todas as opções de `probe`, `scan`, `list`, `play`, `epg` (`--ofdm`, `--gain`, `--player`...).
    - [installation.md](user/installation.md): toolchain, flags CMake, CTest, release, `bench_sync`, ferramentas.
    - [USER_GUIDE.md](USER_GUIDE.md): uso detalhado e solução de problemas.
    - [RELEASE_README.md](RELEASE_README.md): README que vai dentro do zip da release.
