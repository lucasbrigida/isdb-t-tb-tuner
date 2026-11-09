# Camada Física (DSP) ISDB-T/ISDB-Tb

## Algoritmos
- **Taxa exata 2031746 S/s:** 1/4 do chip rate $F_{base} = 512/63$ MHz ≈ 8.126984 MHz → CP > 90% de correlação, bins FFT alinhados sem reamostragem.
- **Sincronismo OFDM dual:**
  - Modo 1 `VanDeBeekSync` (baseline): autocorrelação de CP, FFO, FSM tripartite (`Acquisition`, `Tracking` ±4, `LostLock`).
  - Modo 2 `SpCirSynchronizer` (Speth & Meyr / gr-isdbt): 2 estágios — aquisição por CP + rastreio fino SP-CIR (IDFT 64 pts), First Arrival Path, CFO estável sem deriva em multipercurso urbano, FSM com histerese.
- **Fila USB SPSC lock-free (`SpscCaptureQueue`):** ring estático 64 slots, acquire-release, drops atômicos, descontinuidades sinalizadas → reset coordenado de NCO.
- **Zero alocação no caminho crítico:** sem `std::deque`/`std::vector::erase` nos desentrelaçadores Forney/bits/tempo → `FixedDelayLine`, `DynamicDelayLine`, offset deslizante (`viterbi_stream_offset_`).
- **Equalização MMSE regularizada adaptativa** (no lugar de ZF): $\sigma^2 = \max(10^{-5}, 0.05 \cdot P_{avg})$; evita amplificar ruído em vales de desvanecimento.
- **CSI centrado em 128** com linhas de retardo de pesos paralelas aos dados (120 símbolos no MSB): preserva limiares 16QAM, **+2 a 3 dB de SNR** em Rayleigh.
- **RS com tabela direta 2D de 64 KB, O(1):** $GF(2^8)$ sem branches; acelera Berlekamp-Massey, Chien e Forney.
- **TMCC DBPSK + BCH(273,191):** sincronismo de quadro de 204 símbolos nas subportadoras 101, 131, 286, 349; reconfigura a camada física dinamicamente.

## Documentação relacionada
| Documento | Conteúdo |
|---|---|
| [ADRs](../adr/README.md) | ADR 001–028: algoritmos avaliados/introduzidos/substituídos e justificativas |
| [USER_GUIDE.md](../USER_GUIDE.md) | Uso, sintonia com `--ofdm`, calibração de ganho RF, troubleshooting |
| [blueprint_isdbt_decoder.md](../blueprint_isdbt_decoder.md) | ABNT NBR 15601/15603, ARIB STD-B31, desentrelaçadores, quadro |
| [AGENTS.md](../../AGENTS.md) | Invariantes, 29 erros corrigidos, diretrizes |
| [samples/README.md](../samples/README.md) | Amostras RF de referência (Canal 18), parâmetros, hashes |
