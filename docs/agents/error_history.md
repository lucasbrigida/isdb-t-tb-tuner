# Histórico de Erros Corrigidos (gr-isdbt)
29 erros críticos corrigidos (contra git-artes/gr-isdbt e sinal do ar). **Nunca reintroduzir.** ❌ = errado, ✅ = correto.

### Erro 1: Tap invertido no LFSR PRBS dos pilotos (`X^11 + X^2 + 1`)
❌ PRBS 11 bits (`reg = 0x7FF`) deslocava à direita (`reg >> 1`) mas fazia XOR com `reg >> 9` (pensando em $1 + X^9 + X^{11}$) → polaridade invertida em ~50% dos SP, estimativa de canal destruída.
✅ (`oneseg_demodulator.cpp`/gr-isdbt) Shift à direita inserindo no bit 10 (`<< 10`): $X^{11}$ = bit 0, $X^9$ = bit 2 (`11 - 9`):
```cpp
uint8_t new_bit = ((reg >> 2) ^ (reg >> 0)) & 0x01;
reg = (reg >> 1) | (new_bit << 10);
```
Modo 3: PRBS avança pelas 5617 portadoras ativas; Segmento 0 (físico 6) começa no índice global `2592` (`6 * 432`); piloto da borda direita = `2592 + 432`.

### Erro 2: Polinômios e bit sobrevivente no Viterbi (`viterbi_decoder.cpp`)
❌ 1. `POLY_G1 = 0171` (`0x79`), `POLY_G2 = 0133` (`0x5B`) com `reg7 = (state << 1) | b` (bit novo no LSB), sem espelhar os 7 bits. 2. Traceback salvava `state & 1` em vez do bit mais antigo (`state & 0x20`). 3. Reiniciava `path_metric` a cada símbolo OFDM (768 bits).
✅ 1. `POLY_G1 = 0x4F` (`POLYA`), `POLY_G2 = 0x6D` (`POLYB`) para `reg7 = (state << 1) | b`. 2. Salvar `state & 0x20`; `prev_state = (cur_state >> 1) | (msb_was_1 ? 0x20 : 0)`. 3. `decode_continuous(...)`: mantém métricas, buffer residual de despunção e janela de sobreviventes entre símbolos; bit puncionado (`soft_bit == 128`) → métrica `0`.

### Erro 3: Chien e Forney no Reed-Solomon (`reed_solomon.cpp`)
❌ Chien para $\Lambda(x) = \prod (1 - X_k x)$ avaliava `exp_table_[p_idx]` ($X_k$) em vez de $X_k^{-1} = \alpha^{255 - p_{idx}}$. Forney (`FCR = 0`) omitia multiplicação por $X_k$ no numerador.
✅ Chien: `uint8_t x_inv = exp_table_[(255 - p_idx) % 255];` Forney: `uint8_t err_val = gf_mul(x_k, gf_div(num, den));`

### Erro 4: Descrambler DVB-T (`0xB8`) vs ISDB-T (`descrambler.cpp`)
❌ Esperava sync invertido `0xB8` a cada 8 pacotes (DVB-T) e pausava o LFSR no `0x47`.
✅ (`descrambler.cpp` + `oneseg_demodulator.cpp`)
- Não existe `0xB8` no ISDB-T; todos os pacotes têm `0x47`.
- PRBS (`0x4A80`/`0xA9`) reinicia 1× por quadro OFDM (204 símbolos) no pulso `frame_begin` do TMCC.
- Saída Viterbi/Forney: bloco de 204 bytes = dados/paridade em `0..202`, sync `0x47` em `203` (ramo 11 do Forney, atraso 0).
- No byte `0x47` o PRBS avança 8 clocks (`clock_byte()`) sem XOR.

### Erro 5: Descarte de amostras e reset de fase entre leituras USB (`main.cpp`, `ofdm_sync.cpp`)
❌ 1. `find_symbol_boundary` retornava `best_d` em qualquer posição do buffer de 32768 amostras (pulando símbolos); `main.cpp` descartava as amostras finais que não completavam `2176` a cada `rtlsdr_read_sync`. 2. `correct_frequency_offset` zerava a fase do NCO a cada bloco de 32768. 3. `main.cpp` injetava blocos `standby` sintéticos (PAT/PMT falsas em PID `0x0000`) quando um bloco não gerava TS, sobrescrevendo a PAT real no VLC.
✅ 1. `first_d = best_d % total_symbol_len` (sempre o 1º símbolo do buffer). 2. `OfdmSync::correct_frequency_offset` acumula `current_phase_` entre blocos. 3. `cmd_play` acumula em `stream_iq_buf` preservando residuais, aplica `fine_symbol_sync` e **não** injeta `standby` sobre o fluxo real.

### Erro 6: Permutação em frequência e atraso do bit interleaver
✅ `FREQ_PERM_MODE3[384]` (`src/dsp/oneseg_demodulator.cpp`) é bijeção exata de `0..383` idêntica a `d_random_perm_mode3` (`gr-isdbt/lib/frequency_deinterleaver_impl.cc`). Nunca trocar por fórmula modular aproximada. QPSK: MSB ($Re < 0$) atrasa `120` símbolos no receptor (`bit_deint_fifos_[1]`); LSB ($Im < 0$) atraso `0`.

### Erro 7: Standby sintético (`TsGenerator`) → tela verde no VLC
❌ `cmd_play` enviava `TsGenerator` por padrão antes/durante a recepção com `decoded_pkts` vazio (imagem verde; PAT → PMT `0x1000`, vídeo PID `0x0100`). O demuxer do VLC travava nesses PIDs e ignorava os reais (ex.: BAND HD vídeo `0x0111`, áudio `0x0112`).
✅ (`main.cpp`) Standby sintético só com `--test-pattern` explícito.

### Erro 8: Reset prematuro do demodulador em quedas transientes de correlação
❌ `unlock_counter > 10` chamava `demod.reset()` após 10 leituras de 32768 (~160 ms) sem pico de CP. Mas o desentrelaçador de tempo ($I=4$, 384 símbolos ≈ 435 ms) + Forney (187 pacotes ≈ 675 ms) precisam de ~1,1 s contínuo até o 1º pacote → qualquer flutuação de 160 ms zerava as FIFOs.
✅ (`main.cpp`) `unlock_counter > 200` (~3,2 s contínuos sem portadora antes de resetar).

### Erro 9: Ausência de PAT (PID 0x0000) no 1-Seg ISDB-Tb
❌ ABNT NBR 15603: Segmento 0 (1-Seg) transmite a PMT direto no PID `0x1FC8`, sem PAT em `0x0000` (que vai nos segmentos Full-Seg 1–12). VLC e players genéricos exigem PAT.
✅ (`psi_parser.cpp`/`main.cpp`) `PsiParser::build_pat_packet` sintetiza PAT válida com CRC32 (~10×/s) → PMT `0x1FC8`; VLC mapeia H.264 (`0x0111`) e AAC (`0x0112`).

### Erro 10: Estabilização do PLL do R820T
❌ `rtlsdr_set_center_freq` seguido de leitura/ajuste de ganho imediato → `[R82XX] PLL not locked!`.
✅ (`main.cpp`) `std::this_thread::sleep_for(std::chrono::milliseconds(100))` logo após sintonizar.

### Erro 11: Lock prematuro de IFO em lóbulos laterais (limiar 0.35)
❌ `max_coh > 0.35f` aceitava lóbulos espúrios de SP (pico ~0.36) como portadora central (`ifo_locked_ = true`), restringindo a busca a `[ifo_bins_, ifo_bins_]`. Preso a 2 portadoras do real (ex.: IFO=-25 vs -27), as 4 portadoras TMCC (101, 131, 286, 349) liam lixo em vez de DBPSK → BCH(273,191) falhava → `TMCC: SYNC` e `TS do Ar: 0 pkts` indefinidamente.
✅ (`oneseg_demodulator.cpp`) Com `tmcc_locked_` falso: busca aberta em `[-45 .. +45]`, coerência > 0.60 (piloto real > 0.95), histerese de 4 símbolos idênticos consecutivos. Só restringe após TMCC confirmar sincronismo e paridade.

### Erro 12: Captura USB no mesmo thread do DSP (dropouts)
❌ `rtlsdr_read_sync` síncrono no loop de FFTW/deinterleave/Viterbi/RS. Nos ~15–20 ms de processamento + UDP, o buffer USB de 64 KB no kernel transbordava → perda de amostras → quebra da sequência de 204 símbolos do TMCC → sem `TMCC: LOCK`.
✅ (`main.cpp`) `reader_thread` dedicado de alta prioridade enche fila thread-safe (`std::queue<std::vector<uint8_t>>` + `std::mutex` + `std::condition_variable`, limite 64 blocos); thread principal consome sem bloquear o RTL-SDR. (Substituído depois por SPSC, ver Erro 20.)

### Erro 13: VLC via shell vs ShellExecute; título da janela do VLC
❌ 1. `cmd.exe /c start` com aspas aninhadas e espaços (`Program Files`) falhava silenciosamente. 2. Confusão: VLC usa o `@` de `udp://@127.0.0.1:1234` para `bind()` local, mas a UI Qt remove o `@` do título (`udp://127.0.0.1:1234 - VLC media player`). Comportamento nativo; o `@` **não** foi perdido.
✅ (`main.cpp`) `ShellExecuteA(NULL, "open", vlc_bin, streamer.url().c_str(), NULL, SW_SHOWNORMAL)` com `udp://@127.0.0.1:1234`, sem `cmd.exe`.

### Erro 14: Colisão de endpoints USB (-9 / `LIBUSB_ERROR_PIPE`) e `play` encerrando sem drenar
❌
1. `cmd_scan`: `rtlsdr_read_sync` seguido de `rtlsdr_set_center_freq` sem descarregar pendências → endpoint de controle 0 do RTL2832U travava (`rtlsdr_demod_write_reg failed with -9`), sem novas escritas I2C no R820T; tuner congelado no Canal 14 pelo resto do scan.
2. `cmd_play`: após `reader_thread` terminar, `rtlsdr_close(dev)` imediato sem resetar/drenar o bulk `0x81` → `r82xx_standby` (`reg=06`) falhava: `rtlsdr_demod_write_reg failed with -9`, `r82xx_write: i2c wr failed=-9 reg=06 len=1`.
3. `cmd_play` sintonizava antes de calibrar ganho/AGC e com delay de PLL de 80 ms (vs 100) → `[R82XX] PLL not locked!`.
4. Scan permissivo (`RSSI >= -18.0 dBFS`, CP `>= 0.35`): ruído térmico UHF fica em `-17.0`..`-17.9 dBFS` e ruído gaussiano atinge 0.35 → canais vazios 14–44 marcados "SINAL LOCK".
✅ (`main.cpp`)
1. `safe_set_freq(dev, freq_hz)`: `rtlsdr_reset_buffer(dev)` antes e depois de `rtlsdr_set_center_freq`, pausa 100 ms (Regra 8).
2. `cmd_play`: antes de sintonizar, configurar sample rate, ganho fixo calibrado e AGC desligado.
3. Encerramento: após `reader_thread.join()`, `rtlsdr_reset_buffer(dev)` + 50 ms, depois `rtlsdr_close(dev)` → tuner entra em standby OK.
4. Scan em dois passos:
   - Passo 1 (triagem RF, ~35 ms/canal): `RSSI >= -13.0 dBFS` e pico CP `>= 0.60`; ruído descartado em ~35 ms.
   - Passo 2 (nome via SDT): só candidatos; `reader_thread` + OFDM/MPEG-TS; monitora SDT (PID `0x0011`), timeout 3,0 s, sai ao obter o nome.

### Erro 15: Alocações no loop quente e `std::deque` nos desentrelaçadores
❌ Tempo (384 ramos), bits (4) e Forney (12) em `std::deque` (alocação por push/pop); `process_symbol` alocava vetores locais (`equalized_`, `data_carriers_`, `freq_deint_`, `time_deint_`) por chamada → dezenas de milhares de `malloc`/`free`/s, fragmentação, poluição L1/L2, picos de latência.
✅ (`delay_line.hpp`/`oneseg_demodulator.hpp`)
1. `FixedDelayLine<T, MaxCapacity>`: `std::array` circular, zero heap; Forney ($12 \times 187$ bytes) e bits ($4 \times 120$ floats).
2. `DynamicDelayLine<T>`: alocação única no init (`std::vector<T>::assign`) para os 384 ramos de tempo.
3. Buffers de `OneSegDemodulator` (`seg_carriers_`, `channel_h_`, `equalized_`, `csi_weight_`, `data_carriers_`, `data_csi_`, `freq_deint_`, `freq_csi_`, `time_deint_`, `time_csi_`) são membros `std::array` fixos. Zero alocação por símbolo.

### Erro 16: `std::vector::erase` no fluxo Viterbi
❌ `viterbi_byte_stream_.erase(begin(), begin() + 204)` por bloco → `memmove` $O(N)$ de todo o buffer a cada pacote.
✅ (`oneseg_demodulator.cpp`) Offset deslizante `viterbi_stream_offset_`; acesso $O(1)$ via `viterbi_byte_stream_.data() + viterbi_stream_offset_`. Só compacta (`std::memmove`) quando offset > 4096 bytes e > metade do buffer.

### Erro 17: CSI pré-demapeador sem sincronismo do atraso de bits
❌ CSI multiplicado no I/Q equalizado antes do soft-decision: em 16QAM ($b_0 = TH - |Im|$) multiplicar $Im$ por $W_l$ distorce $TH$. E o MSB atrasa 120 símbolos no bit deinterleaver, mas recebia o peso CSI do símbolo atual → descompasso total.
✅ (`oneseg_demodulator.cpp`)
1. Demapeamento euclidiano sobre amostras equalizadas puras → soft centrado em 128 (`constants::SOFT_NEUTRAL`).
2. Pesos CSI passam por linhas de atraso idênticas (`time_csi_delay_lines_`, `bit_csi_delay_lines_`), com o mesmo atraso de 120 no MSB.
3. Escala nos soft-bits: $soft\_scaled = 128 + \text{clamp}(W_l \cdot (soft\_orig - 128), -128, +127)$. $W_l \to 0$ → 128 (apagamento perfeito na treliça), sem distorção geométrica.

### Erro 18: Busca exaustiva contínua de CP sem máquina de estados tripartite
❌ Cada `find_symbol_boundary` testava os 4 GIs (1/16, 1/8, 1/4, 1/32) e varria dezenas de milhares de amostras → >70% da CPU desperdiçada já sincronizado.
✅ (`ofdm_sync.hpp`/`.cpp`)
- `SyncState::Acquisition`: busca exaustiva inicial, detecta GI ótimo.
- `SyncState::Tracking`: fixa o GI, janela de ±4 amostras (`TRACK_RADIUS`) em torno de `expected_first_d_` → >5× mais rápido.
- `SyncState::LostLock`: após 3 falhas consecutivas (`MAX_CONSECUTIVE_FAILURES`), volta a `Acquisition` na próxima leitura.

### Erro 19: Multiplicação $GF(2^8)$ com branch e tabelas indiretas
❌ `gf_mul(a, b)` com `if (a == 0 || b == 0) return 0;` + `exp_table_[log_table_[a] + log_table_[b]]` → dezenas de milhares de stalls/s em BM, Chien e Forney ($t=8$).
✅ (`reed_solomon.hpp`/`.cpp`) Tabela direta 64 KB `uint8_t gf_mul_table_[256][256]` pré-computada; cabe em L1/L2:
```cpp
inline uint8_t gf_mul(uint8_t a, uint8_t b) const noexcept {
    return gf_mul_table_[a][b];
}
```

### Erro 20: Fila bloqueante (`std::mutex`) sem sequência nem reset coordenado
❌ `reader_thread` com `std::mutex` + `std::condition_variable` + `std::queue<std::vector<uint8_t>>`: contenção e alocação de 64 KB por bloco. Overflow descartava blocos sem sinalizar → sincronizador correlacionava dados descontínuos.
✅ (`spsc_capture_queue.hpp`/`main.cpp`)
1. `SpscCaptureQueue<64>` lock-free SPSC, ring estático de 64 blocos × 65536 bytes.
2. Cada bloco: `sequence` atômico 64 bits + flag `discontinuity_before`.
3. Gap/descontinuidade → `reset_reception_stream` limpa residuais e reinicia fases do NCO.

### Erro 21: Remoção do reload IPC do player na troca de canal (`reload_mpv_player`)
❌ Agente removeu `reload_mpv_player(udp_port)` em `cmd_play.cpp` ("manter player anexado"). Emissoras 1-Seg têm PCR/PTS/DTS, resolução e PIDs não coordenados → demuxers FFmpeg do MPV travam/congelam no último quadro.
✅ (`cmd_play.cpp`/`platform_utils.cpp`) Na troca, após reset do DSP, se `auto_player`: chamar obrigatoriamente `reload_mpv_player(udp_port)` → pipe IPC Win32 `\\.\pipe\isdbtb_mpv` com `{"command": ["loadfile", "udp://127.0.0.1:<port>", "replace"]}\n`; MPV recarrega na mesma janela.

### Erro 22: Mutilação de `send_discontinuity_burst` e pacotes sem PAT
❌ Agente fragmentou `send_discontinuity_burst` em pacotes individuais com CC parciais e padding nulo (`0x1FFF`), e moveu a descontinuidade no thread DSP para depois de `psi.has_pmt()` → mídia do novo canal vazava sem PAT, corrompendo VLC/MPV.
✅ (`udp_streamer.cpp`/`cmd_play.cpp`)
1. `streamer.send_discontinuity_burst()` imediatamente no thread de UI no início da sintonia.
2. Exatamente 1 datagrama atômico de 1316 bytes (7×188) no PID `0x0000`, `adaptation_field_control = 0x20`, `discontinuity_indicator = 1` (Regra 2).
3. PAT sintetizada pelo `PsiParser` enviada assim que a PMT do novo canal existir, antes da mídia.

### Erro 23: Mutilação do `RobustGRISDBTSync` e adulteração de documentação
❌ Ao ligar `--ofdm 2` em `cmd_play.cpp`, agente apagou tabelas de SP (`sp_indices_`, `init_sp_tables()`, `fft_in_scratch_`), renomeou `refine_timing_sp` → `refine_timing_cp` e alterou a doc para esconder.
✅ Plugar via `make_synchronizer` preservando estruturas SP, `refine_timing_sp` e a doc oficial do port gr-isdbt. **Nunca** alterar doc para justificar remoções/simplificações.

### Erro 24: Testes de rede com bind local no CTest
❌ `test_udp_streamer` abria sockets UDP loopback reais com `recv` bloqueante + timeout → falhava no Windows (firewall/timeouts).
✅ Validar offline/em memória: montagem determinística dos 1316 bytes e cabeçalhos TS (sync `0x47`, PID `0x0000`, flag de adaptação).

### Erro 25: Buffers de 1 MB no `mpv.conf` e program number da PAT (`0x0001` vs PMT real)
❌
1. `mpv.conf`: `demuxer-lavf-probesize=1048576`, `demuxer-lavf-buffersize=1048576`, `cache-secs=3.0` (herdado de Full-Seg ~15 Mbps). 1-Seg ~312 kbps ≈ 39 KB/s → 1 MB leva >27 s; MPV parado.
2. `PsiParser::build_pat_packet` sintetizava PAT preliminar com `prog_num = 0x0001` antes da PMT real. A PMT no PID `0x1FC8` com `service_id` real (ex.: `0xE778` TV Cultura) era descartada (`[ffmpeg/demuxer] mpegts: no program with id 59256 found`) → sem H.264/AAC.
3. `--player` iniciava MPV com a URL UDP no segundo zero, antes do sync OFDM/TMCC → timeout do demuxer.
✅ (`mpv.conf`/`cmd_play.cpp`/`platform_utils.cpp`)
1. `demuxer-lavf-format=mpegts`, `demuxer-lavf-probesize=65536` (64 KB ≈ 1,6 s), `demuxer-lavf-analyzeduration=1.0`, `demuxer-lavf-buffersize=65536`, `cache=no`, `cache-secs=1.0`.
2. MPV com `--force-window=immediate --idle=yes`, aguardando IPC (`\\.\pipe\isdbtb_mpv`), sem URL preliminar.
3. Recepção inicia com `channel_transition_pending = !test_pattern`; espera `psi.has_pmt()` antes da PAT e do 1º lote TS.
4. PAT usa o `service_id` real da PMT (`0xE778`); `reload_mpv_player` notifica via IPC quando o stream válido flui.

### Erro 26: Métrica sem corte de PAPR e timing atraído por ecos (multipercurso)
❌ Timing fino SP/CIR calculava coerência sem corte de PAPR (64 pontos): em ruído/perda dava ≈ 0.5 espúrio, impedindo `Tracking` → `LostLock` e injetando desvios na malha PI de SCO. E escolhia o pico global da CIR, que em multipercurso urbano costuma ser eco retardado (mais forte que a visada direta) → janela FFT atrasada e ISI.
✅ (`sp_cir_sync.cpp`)
1. Corte estrito PAPR ≥ 12.0 (ruído gaussiano 64 pts: média teórica ≈ 4.73; só SP ISDB-T coerente atinge ≥ 12.0). Se < 12.0, métrica = 0.0 → fallback transparente para CP e detecção correta de perda.
2. Detector de primeiro caminho (*First Arrival Path*): em $\tau \in [-16 \dots +16]$, 1º pico ≥ 25% de $P_{\max}$ → janela FFT na 1ª chegada, sem ISI.
3. Campo, 15 min, Canal 28: +48% de vídeo 100% contínuo (284 s vs 192 s), TMCC Lock 60.9% → 84.4%, +44.4% pacotes úteis (164.493 vs 113.908).

### Erro 27: Deriva do SCO e fase cega dos SP no `--ofdm 2`
❌
1. Rastreador SP rodava antes do TMCC com contador cego `symbol_counter_ & 3` iniciado em 0. Fases cíclicas $l = s \pmod 4$ → 75% das vezes descasado; IDFT 64 pts lia dados QPSK/16QAM (PAPR < 12.0) → fallback permanente para CP.
2. Malha SCO integrava a cada símbolo com $K_i = 0.005$ sem leakage nem clamp → jitter/desvanecimento do CP acumulados → deriva contínua da janela FFT.
3. `BASE_SEG_OFFSET` fixo 808 sem compensar `ifo_bins_` → pilotos ilegíveis com offset de oscilador.
✅ (`sp_cir_sync.cpp`/`iofdm_synchronizer.h`)
1. Ancoragem TMCC: `notify_frame_sync(symbol_idx_in_frame, ifo_bins)` chamado pelo demodulador ao validar BCH do TMCC; ancora $l = s \pmod 4$ e bin central SP (`BASE_SEG_OFFSET + ifo_bins_`).
2. Integrador SCO só opera ancorado no TMCC com piloto PAPR ≥ 12.0; congelado em fallback CP/desvanecimento. Leakage 0.999, $K_i = 0.0001$, saturação ±0.05 amostra/símbolo (≈ ±23 ppm).
3. CIR (IDFT 64 pts) a cada 2 símbolos (~466 Hz), *flywheel* no intercalado → metade do custo (≤ 2.0× ciclos vs Van de Beek), 100% estável.

### Erro 28: Deriva de CFO por integração aberta vs congelamento cego sem zona morta
❌
1. Integrador aberto (`current_cfo_hz_ += cfo_gain * ffo_res`) somava o viés $\theta_{bias}$ do desvanecimento seletivo urbano no Canal 28 (~1.5 Hz/bloco × 62 blocos/s) → centenas de Hz após 90 s; SNR 9.5 → 2.0 dB, perda de TMCC lock.
2. Congelar CFO totalmente na aquisição regrediu `ch24_10s_iq.cu8` de 2 → 0 pacotes: FFO inicial no bloco 1 (-111.9 Hz) diferia 117.2 Hz do real (-229.1 Hz) → 125°/símbolo, Viterbi destruído.
✅ (`sp_cir_sync.cpp`/ADR 028) Passa-baixas com ganho 0.08 ancorado e **zona morta ±0.5 Hz**:
```cpp
if (sp_tmcc_anchored_ && std::abs(ffo_res) < 0.5f) {
    ffo_res = 0.0f;
}
float cfo_gain = sp_tmcc_anchored_ ? 0.08f : 0.20f;
current_cfo_hz_ += cfo_gain * ffo_res;
```
Puxa desvios até ~120 Hz (entrega os 2 pacotes válidos); convergido em $|ffo\_res| < 0.5$ Hz, NCO congela → sem deriva em multipercurso severo.

### Erro 29: Lock fantasma (`ofdm_locked` forçado), sem watchdog TMCC, represamento USB com fila cheia
❌
1. `cmd_play.cpp` forçava `ofdm_locked = true` com `reception_started`, mascarando perdas. CP em ruído/sinal degradado chega a ~0.98 → ficava em `Tracking [CP]`, `unlock_counter` zerado → minutos em silêncio sem TS e sem avisar.
2. Demodulador não chamava `notify_frame_lost()` ao perder BCH do TMCC → `sp_tmcc_anchored_` eterno com `ifo_bins_` obsoleto.
3. `reader_thread` com fila SPSC cheia dormia 200 µs sem ler do RTL-SDR e chamava `raw_queue->record_drop()` → dezenas de drops falsos em ms, amostras represadas no kernel, `LIBUSB_ERROR_IO -1` no encerramento.
4. Resets transientes zeravam contadores de RS e Viterbi (histórico perdido).
✅ (`cmd_play.cpp`/`sp_cir_sync.cpp`/`iofdm_synchronizer.h`)
1. `ofdm_locked` = estritamente `SyncState::Tracking`. UI e CSV mostram `ts_rate` `(X p/s)`, `Resets: N` e perdas de sync do Viterbi.
2. Demodulador chama `sync->notify_frame_lost()` quando `!is_tmcc_locked()` (desancora fase e IFO).
3. Watchdog TMCC/TS: recepção iniciada e ~3 s contínuos sem TMCC lock → reset coordenado e re-aquisição.
4. Fila cheia → drena USB para `discard_buf`, mantendo fluxo contínuo a 2031746 S/s; 1 drop real por bloco descartado.
5. Contadores de RS corrigidos/descartados e quedas do Viterbi acumulados por sessão.
