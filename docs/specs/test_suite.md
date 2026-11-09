# Suíte de Testes

## Nível 1: CTest unitário determinístico (27/27)
Sem hardware: `ctest --test-dir build --output-on-failure -E BenchSync`

| # | Teste | Valida |
|---|---|---|
| 1 | `test_reed_solomon` | Corrige até 8 bytes; detecta falha > 8 (BM, Chien, tabela O(1)) |
| 2 | `test_descrambler` | LFSR 15 bits ISDB-T, inversão e clocking sobre sync `0x47` |
| 3 | `test_viterbi` | K=7 contínuo, puncionamento 1/2, 2/3, 3/4, 5/6, 7/8 |
| 4 | `test_fec_pipeline` | Viterbi → byte deinterleaver → descrambler → RS |
| 5 | `test_ofdm_sync` | Van de Beek (correlação CP) e FFO contínuo |
| 6 | `test_oneseg_e2e` | Pipeline 1-Seg simulado: QPSK, 2/3, GI 1/8, $I=4$ |
| 7 | `test_iq_stream_buffer` | Buffer I/Q contínuo entre leituras USB assíncronas |
| 8 | `test_spsc_queue` | Concorrência lock-free, FIFO, drops, descontinuidades |
| 9 | `test_delay_line` | `FixedDelayLine`/`DynamicDelayLine`: tamanhos, resets, zero alocação |
| 10 | `test_reset_recovery` | Recuperação determinística após descontinuidade e reset coordenado |
| 11 | `test_sync_state_and_csi` | FSM `Acquisition` → `Tracking` → `LostLock`; CSI centrado em 128 |
| 12 | `test_bit_deinterleaver_qam16` | Bit deinterleaver 16QAM, delays 0, 40, 80, 120 símbolos, CSI soft |
| 13 | `test_volk_cpu_features` | Detecção SIMD (AVX2, AVX, SSE2) |
| 14 | `test_cli_simd_integration` | Despacho VOLK em desrotação e correlação |
| 15 | `test_frequency_plan` | Frequências UHF 14–69 (ABNT NBR 15601) |
| 16 | `test_channel_manager` | Persistência e busca em `channels.json` |
| 17 | `test_psi_parser` | Nome via SDT, PAT de fallback periódica, múltiplas seções no mesmo TS (ETSI EN 300 468) |
| 18 | `test_arib_b24` | ARIB STD-B24 (Kanji JIS X 0208, Hiragana, Katakana, alfanumérico, APR, Gaiji); fallback Latin-1/UTF-8 |
| 19 | `test_epg_manager` | `epg.json`, descarte de eventos passados, Tabela 0x4E (Present/Following), reavaliação para `--view` |
| 20 | `test_udp_streamer` | Datagramas 1316 B (7×188) determinísticos, sem fragmentação |
| 21 | `test_cli_ofdm_flag` | `--ofdm 1`, `--ofdm 2`, `--ofdm=1`, `--ofdm=2`; rejeita inválidos; tolera fora de `play` |
| 22 | `test_sync_regression_scan` | Scanner sem regressão no baseline Van de Beek |
| 23 | `test_bench_ascii_table` | Tabelas ASCII de 113 colunas com largura UTF-8 correta |
| 24 | `test_regression_ofdm1` | Regressão do baseline Van de Beek sobre captura RF real |
| 25 | `test_sync_sp_cir` | SP-CIR: IDFT 64 pts, PAPR ≥ 12, First Arrival Path, FFO com zona morta, SCO |
| 26 | `test_sync_grisdbt_robust` | Alias `RobustGRISDBTSync` sobre SP-CIR |
| 27 | `test_regression_ofdm2` | Regressão SP-CIR sobre `ch24_10s_iq.cu8` (2 pacotes válidos) |
| 28 | `bench_sync` | Benchmark comparativo de DSP e throughput TS (excluído do `-E BenchSync`) |

## Nível 2: captura RF real offline (`ch24_10s_iq.cu8`)
- 10 s em 533.143 MHz (Canal 24 UHF, TV Cultura), 2031746 S/s, 40.6 MB, SNR ~8 dB.
- Decodifica TS válidos; validação cruzada `--ofdm=1` vs `--ofdm=2` via `bench_sync`.

## Nível 3: ao vivo (RTL-SDR + VLC/MPV)
- RTL2832U + R820T; `.\build\isdbtb_rx.exe play -c 23 --player`
- Resultado: `OFDM: LOCKED (Pico > 0.95)`, `TMCC: LOCK`, `IFO: -27`, TS contínuo, vídeo/áudio fluidos via `udp://@127.0.0.1:1234`.
