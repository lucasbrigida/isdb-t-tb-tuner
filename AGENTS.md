# AGENTS.md
Decodificador ISDB-T/ISDB-Tb. Consulte a doc detalhada só quando necessário (economia de contexto).

## 1. Visão geral
- CLI C++17 de alta performance, TV Digital ISDB-T/ISDB-Tb 1-Seg (móvel).
- Sem GNU Radio nem libs externas de DSP: FFT e SIMD próprios em shims header-only com a API de FFTW3/VOLK (`include/third_party/`).
- Hardware: RTL-SDR (RTL2832U + R820T/R828D/E4000).
- Saída: MPEG-TS via UDP (`udp://@127.0.0.1:1234`) para MPV/VLC.

## 2. Mapa de documentação (sob demanda)
| Necessidade | Documento |
|---|---|
| Invariantes críticas | [docs/agents/invariants.md](docs/agents/invariants.md) |
| 29 erros corrigidos | [docs/agents/error_history.md](docs/agents/error_history.md) |
| Diretrizes/proibições | [docs/agents/guidelines.md](docs/agents/guidelines.md) |
| ADRs 001–028 | [docs/adr/README.md](docs/adr/README.md) |
| Cadeia DSP | [docs/specs/dsp_pipeline.md](docs/specs/dsp_pipeline.md) |
| Arquivos e classes | [docs/specs/code_map.md](docs/specs/code_map.md) |
| Testes | [docs/specs/test_suite.md](docs/specs/test_suite.md) |
| Índice mestre | [docs/INDEX.md](docs/INDEX.md) |

## 3. Regras de ouro
1. **Taxa exata:** 2031746 S/s (512/(63×4) MHz). Nunca 2.048 MSps.
2. **UDP:** datagrama = 7 pacotes TS (7×188 = 1316 B) para `udp://@127.0.0.1:1234`.
3. **Persistência local:** só `./channels.json`, nunca `%APPDATA%`.
4. **Zero alocação no loop quente:** proibido `malloc`, `new`, `std::vector::push_back`, `.erase()` na demodulação de símbolos.
5. **CSI centrado em 128:** `128 + clamp(W * (soft - 128), -128, +127)`.
6. **GF(2^8) em O(1):** RS usa tabela estática `gf_mul_table_[256][256]`.
7. **Dual-mode OFDM:** `--ofdm 1` (Van de Beek, baseline intocável), `--ofdm 2` (SpCirSynchronizer). Flag exclusiva de `play`.

> [!WARNING]
> Antes de mexer na demodulação (Viterbi, Forney, PRBS, Reed-Solomon, IFO), **leia** [docs/agents/error_history.md](docs/agents/error_history.md).
