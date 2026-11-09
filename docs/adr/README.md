# ADRs (Architecture Decision Records)
Todas as decisões de arquitetura/engenharia do projeto, em arquivo único.

| ADR | Título |
|---|---|
| [001](#adr-001) | Escopo estrito em 1-Seg com RTL-SDR |
| [002](#adr-002) | Taxa exata 2031746 S/s vs 2.048 MSps |
| [003](#adr-003) | Streaming UDP 7×188 = 1316 B |
| [004](#adr-004) | Núcleo DSP nativo C++17 sem GNU Radio |
| [005](#adr-005) | Reed-Solomon RS(204,188,t=8) em GF(2^8) |
| [006](#adr-006) | Viterbi soft-decision (K=7, 64 estados) |
| [007](#adr-007) | Sincronismo de símbolo e FFO por CP (Van de Beek) |
| [008](#adr-008) | Alinhamento matemático com git-artes/gr-isdbt |
| [009](#adr-009) | Captura USB assíncrona multithread |
| [010](#adr-010) | VLC via ShellExecute e título do player |
| [011](#adr-011) | Equalização: ZF → MMSE regularizado |
| [012](#adr-012) | Ponderação CSI de soft-bits no Viterbi |
| [013](#adr-013) | Scan two-pass (triagem RF + SDT) |
| [014](#adr-014) | VLC externo → MPV embutido com Job Object e shaders |
| [015](#adr-015) | UDP sem flush prematuro, alinhado a 1316 B |
| [016](#adr-016) | Proteção do barramento USB (erro -9) e sintonia segura |
| [017](#adr-017) | Consolidação das correções matemáticas (gr-isdbt) |
| [018](#adr-018) | Fila SPSC lock-free com sequência e reset coordenado |
| [019](#adr-019) | Zero alocação no loop quente; `FixedDelayLine`/`DynamicDelayLine` |
| [020](#adr-020) | CSI centrado em 128 com atraso sincronizado |
| [021](#adr-021) | FSM tripartite de sincronismo OFDM |
| [022](#adr-022) | Tabela direta 64 KB O(1) para GF(2^8) |
| [023](#adr-023) | Otimizações de build (IPO/LTO, -O3, fast-math) |
| [024](#adr-024) | `IOFDMSynchronizer`, dual-mode `--ofdm`, baseline preservado |
| [025](#adr-025) | Dual-architecture x86/x64, MPV i686, releases |
| [026](#adr-026) | Persistência local portátil (`channels.json`) |
| [027](#adr-027) | Segregação de ferramentas por arquitetura e layout do terminal |
| [028](#adr-028) | Literatura formal, SP-CIR puro e fim da deriva de CFO |

<a id="adr-001"></a>
## ADR 001: Escopo estrito em 1-Seg com RTL-SDR
**Contexto:** ISDB-T divide 6 MHz em 13 segmentos: 1–12 = Full-Seg HDTV 1080i (~5.2–5.6 MHz úteis); segmento 0 central = 1-Seg móvel (428.57 kHz). RTL2832U envia I/Q 8 bits via USB 2.0; máximo contínuo seguro ~2.4 MSps (2.8–3.2 MSps perde pacotes USB).
**Decisão:** escopo estritamente 1-Seg.
**Consequências:** ✅ fluxo estável sem dropped samples; CPU baixa (laptops, mini-PCs); qualquer RTL-SDR barato. Limitação: só feed 1-Seg (QVGA/VGA H.264 + AAC). Futuro Full-Seg: SDR banda larga (HackRF One, Airspy R2, LimeSDR, BladeRF) a $F_s$ = 8.126984 MSps.

<a id="adr-002"></a>
## ADR 002: Taxa exata 2031746 S/s vs 2.048 MSps
**Contexto:** ARIB STD-B31/ABNT NBR 15601: $F_{base} = 512/63$ MHz ≈ 8.126984 MHz. 2.048 MSps (tutoriais) erra $(2.048 - 2.031746)/2.031746$ ≈ +0.80% → janela OFDM desliza a cada poucos símbolos, exige reamostrador fracionário pesado e degrada correlação de CP.
**Decisão:** $F_s = F_{base}/4 = 512/(63 \times 4)$ MHz = 2031746 Hz.
**Consequências:** sintetizador fracionário do RTL-SDR atinge 2031746.004 Hz (erro < 0.005 Hz). Modo 3: símbolo = 2048 + 128 = 2176 amostras (GI 1/16). Pico de correlação de CP: 11.5% → **94.5%** em testes reais.

<a id="adr-003"></a>
## ADR 003: Streaming UDP 7×188 = 1316 B
**Contexto:** alternativas: (1) pipe binário (`| vlc -`): no Windows PowerShell/CMD quebram pipes binários ao fechar o player, comandos longos; (2) HTTP/HLS: fatiar `.ts` + `.m3u8`, 2–4 s de latência; (3) UDP (padrão IPTV, RFC 2250).
**Decisão:** UDP para `udp://@127.0.0.1:1234`, exatamente 7 pacotes TS de 188 B por datagrama (1316 B).
**Por quê 1316:** MTU Ethernet 1500 − IPv4 20 − UDP 8 = 1472 úteis; $\lfloor 1472/188 \rfloor = 7$ → 1316 B. Zero fragmentação, mínimo de interrupções, latência < 300 ms no VLC.

<a id="adr-004"></a>
## ADR 004: Núcleo DSP nativo C++17 sem GNU Radio
**Contexto:** `gr-isdbt` é módulo out-of-tree do GNU Radio, que arrasta Python, Boost, log4cpp, Thrift, PyBind11, Qt5/6, UHD → deploy Windows volumoso e frágil.
**Decisão:** núcleo C++17 puro com os algoritmos essenciais; únicas dependências de cálculo: FFTW3 e VOLK.
**Consequências:** binário < 2 MB, partida rápida, sem interpretador Python no DSP; nativo em Windows (MinGW-w64, MSVC) e Linux.

<a id="adr-005"></a>
## ADR 005: Reed-Solomon RS(204,188,t=8) em GF(2^8)
**Contexto:** outer FEC (ISDB-T e DVB-T) = RS(204,188) encurtado de RS(255,239): 188 B de carga + 16 de paridade → $t = (204-188)/2 = 8$ bytes corrigíveis por pacote.
**Implementação:**
- Polinômio primitivo $p(x) = x^8 + x^4 + x^3 + x^2 + 1$ (0x11D / 285).
- `exp_table_[512]` e `log_table_[256]` pré-calculadas (mult./div. viram somas modulares O(1)). Ver ADR 022 para a tabela direta atual.
- Berlekamp-Massey: localizador $\Lambda(x)$ de grau $L \le 8$.
- Chien: raízes de $\Lambda(x)$ → posições dos erros.
- Forney: magnitude de cada erro sem inverter matrizes.

<a id="adr-006"></a>
## ADR 006: Viterbi soft-decision (K=7, 64 estados)
**Contexto:** inner FEC convolucional K=7, geradores $G_1 = 171_8$ = 0b1111001, $G_2 = 133_8$ = 0b1011011 (no código: espelhados para `0x4F`/`0x6D`, ver ADR 008).
**Implementação:**
- $2^{K-1} = 64$ estados na treliça.
- Métricas soft 0–255, 128 = apagado/puncionado → ganho ~**2 dB** vs hard-decision.
- Traceback com `std::array<uint32_t, 64>` fixos (sem alocação).

<a id="adr-007"></a>
## ADR 007: Sincronismo de símbolo e FFO por CP (Van de Beek)
**Contexto:** símbolo OFDM = $N_{FFT}$ amostras úteis precedidas de cópia das últimas $N_{GI}$ (prefixo cíclico/GI).
**Implementação:** cálculo deslizante
$$\Gamma(d) = \sum_{m=0}^{N_{GI}-1} r(d+m)\, r^*(d+m+N_{FFT}), \quad \Phi(d) = \tfrac{1}{2} \sum_{m=0}^{N_{GI}-1} \left(|r(d+m)|^2 + |r(d+m+N_{FFT})|^2\right)$$
$|\Gamma(d)|^2/\Phi(d)^2$ tem pico agudo na transição CP → símbolo útil. Fase do pico dá o FFO: $\Delta f = -\angle\Gamma(d_{max}) / (2\pi T_u)$. Correção em tempo real com SIMD `volk_32fc_x2_multiply_32fc` antes da FFT.

<a id="adr-008"></a>
## ADR 008: Alinhamento matemático com git-artes/gr-isdbt
**Contexto:** validação cruzada com [git-artes/gr-isdbt](https://github.com/git-artes/gr-isdbt): pequenas diferenças de convenção de bits/registradores e de enquadramento DVB-T vs ISDB-T impediam decodificar TS reais.
**Regras permanentes:**
1. **PRBS de pilotos ($X^{11} + X^2 + 1$):** LFSR 11 bits com shift à direita (`reg >> 1`): tap de $X^9$ é `reg >> 2` (não `reg >> 9`). PRBS roda desde a portadora 0 do Modo 3 até o Segmento 0 (`2592 = 6 * 432`).
2. **Viterbi contínuo (`0x4F`/`0x6D`):** bit entra no LSB (`reg7 = (state << 1) | b`) → $171_8$/$133_8$ espelhados em 7 bits = `0x4F`/`0x6D`; sobrevivente = `state & 0x20` (bit 5 que sai do estado de 6 bits); treliça contínua entre símbolos.
3. **RS (`FCR = 0`):** Chien avalia $X_k^{-1} = \alpha^{(255 - p_{idx}) \bmod 255}$; Forney multiplica por $X_k = \alpha^{p_{idx}}$ (1ª raiz consecutiva $\alpha^0$).
4. **Dispersão de energia sem `0xB8`:** ISDB-T não inverte o sync a cada 8 pacotes. PRBS reseta a cada quadro OFDM de 204 símbolos (`frame_begin` do TMCC), opera nos 203 bytes (`0..202`) antes do `0x47` (posição `203`, ramo 11 do Forney) e avança 8 clocks no `0x47`.
5. **TMCC DBPSK + BCH(273,191):** demodulação diferencial das portadoras `101, 131, 286, 349`; sync par `0x35EE` / ímpar `0xCA11`; valida síndrome BCH antes de aplicar parâmetros da Camada A (modulação, taxa Viterbi, profundidade $I$ do time deinterleaver).
6. **Continuidade temporal:** `src/main.cpp` mantém buffer deslizante `stream_iq_buf` e fase NCO contínua `current_phase_` → nenhum símbolo descartado nas fronteiras de leitura de `32768` amostras.

<a id="adr-009"></a>
## ADR 009: Captura USB assíncrona multithread
**Contexto:** `rtlsdr_read_sync` no mesmo thread do DSP (FFT 2048, time deinterleave, Viterbi, RS) bloqueava as transferências USB por 10–25 ms/bloco → excedia o buffer do RTL2832U → perda silenciosa de amostras → quadro TMCC de 204 símbolos fragmentado, preso em `TMCC: SYNC`.
**Decisão:**
1. `reader_thread` dedicado de alta prioridade só com `rtlsdr_read_sync` em loop.
2. Blocos de 32768 amostras em `std::queue<std::vector<uint8_t>>` + `std::mutex` + `std::condition_variable`, limite 64 blocos.
3. Thread principal consome e alimenta o DSP sem perder continuidade.
**Consequências:** fim dos overflows USB; TMCC trava em < 300 ms após sintonia. (Substituído pela fila SPSC no ADR 018.)

<a id="adr-010"></a>
## ADR 010: VLC via ShellExecute e título do player
**Contexto:** `start vlc udp://@127.0.0.1:1234` via `std::system` passa por `cmd.exe /c`, que falha com aspas aninhadas e espaços (`C:\Program Files\VideoLAN\VLC\vlc.exe`). A GUI Qt do VLC omite o `@` no título (`udp://127.0.0.1:1234 - VLC media player`), mas `access_udp` usa o `@` para o `bind()` local.
**Decisão:**
1. `ShellExecuteA(NULL, "open", vlc_path, url, NULL, SW_SHOWNORMAL)`: lançamento assíncrono direto, sem `cmd.exe`.
2. Documentar que o título sem `@` é estético e não afeta o binding.
**Consequências:** VLC abre de forma confiável; argumentos UDP íntegros.

<a id="adr-011"></a>
## ADR 011: Equalização ZF → MMSE regularizado
**Contexto:** `OneSegDemodulator` equalizava as 384 portadoras de dados com Zero-Forcing $\hat{X}_k = Y_k / H_k$ ($H_k$ por interpolação linear sobre SP e CP).
**Problema:** em multipercurso Rayleigh há nulos $|H_k| \approx 0$; ZF amplifica ruído por $1/|H_k|^2$. Medido: SNR médio 18 dB, mas subportadoras em fading com ruído +30 dB → pontos gigantes de alta confiança errônea no Viterbi, estourando o RS.
**Decisão:** MMSE regularizado com piso adaptativo:
$$\hat{X}_k = Y_k \frac{H_k^*}{|H_k|^2 + \sigma^2}, \quad \sigma^2 = \max(10^{-5}, 0.05 P_{avg}), \quad P_{avg} = \tfrac{1}{384}\sum_{k=0}^{383} |H_k|^2$$
**Consequências:** sinal forte ($|H_k|^2 \gg \sigma^2$) → $1/H_k$ ideal; fading ($|H_k|^2 \ll \sigma^2$) → denominador limitado por $\sigma^2$, atenua em vez de explodir ruído. Queda drástica da SER, sem artefatos de constelação.

<a id="adr-012"></a>
## ADR 012: Ponderação CSI de soft-bits no Viterbi
**Contexto:** soft-bits 0–255 (0 = bit 0 forte, 255 = bit 1 forte, 128 = apagamento) vinham só da distância euclidiana; subportadora com $|H_k|^2 \approx 0.01$ pesava o mesmo que uma limpa com $|H_k|^2 \approx 2.0$.
**Decisão:**
1. Confiabilidade por subportadora: $W_l = \text{clamp}(|H_l|^2 / P_{avg}, 0.0, 2.0)$.
2. Escala rumo ao neutro: $soft\_scaled = 128 + \text{round}((soft\_orig - 128) \cdot W_l)$.
3. Aplicada antes dos deinterleavers de frequência e bits, para que após o time deinterleaver ($I=4$) o Viterbi receba apagamentos onde houve fading.
**Consequências:** subportadoras atenuadas viram erasures; bits redundantes das fortes reconstroem a mensagem. **+2 a 3 dB de SNR** medidos em fading urbano (Rayleigh, reflexões). (Refinado no ADR 020.)

<a id="adr-013"></a>
## ADR 013: Scan two-pass (triagem RF + SDT)
**Contexto:** `isdbtb_rx scan` (UHF 14–69) usava `RSSI >= -18.0 dBFS` e `CP >= 0.35`. Ruído térmico UHF fica em `-17.0`..`-17.9 dBFS` e ruído gaussiano atinge CP 0.35 → **todos os canais vazios (14–44)** viravam "SINAL LOCK"; scan de minutos e `channels.json` poluído.
**Decisão:** fluxo por canal: Passo 1 → se reprovado, descarta (~35 ms) → senão Passo 2 → SDT achada (salva nome, sai cedo) ou timeout 3.0 s (registra canal com sinal RF) → próximo canal.
1. **Passo 1 (triagem RF, ~35 ms/canal):** `RSSI >= -13.0 dBFS` e pico CP `>= 0.60` (sinais reais > 0.90).
2. **Passo 2 (nome via SDT):** só candidatos; `reader_thread` + OFDM/MPEG-TS; monitora SDT (PID `0x0011`), timeout 3.0 s, sai ao obter o nome (ex.: `BAND HD`, `GLOBO`, `SBT`, `RECORD`).
**Consequências:** 55 canais em **< 15 s** (antes > 3 min); zero falsos positivos; `channels.json` com nomes oficiais da sinalização.

<a id="adr-014"></a>
## ADR 014: VLC externo → MPV embutido com Job Object e shaders
**Contexto:** só havia UDP para VLC, com 3 barreiras: (1) VLC lançado por script/CLI no Windows frequentemente ignorava o `@` de `udp://@127.0.0.1:1234`, exigindo configuração manual; (2) 1-Seg em QVGA (320x240) ou 320x180 a 15/30 fps: VLC esticava bilinear → imagem borrada, blocos e judder em 60/120/144 Hz; (3) usuário precisava instalar VLC.
**Decisão:** MPV embutido (**v0.41.0 portátil x64**) em `tools/mpv/` e na release, acionado por `--player`.
1. **Shaders em `tools/mpv/mpv.conf`:**
   - `scale=ewa_lanczossharp`, `cscale=ewa_lanczos`: super-resolução por Lanczos elíptico ponderado.
   - `sigmoid-upscaling=yes`: evita halos nas bordas.
   - `deband=yes`: dither adaptativo contra blocos/gradações de baixa taxa.
   - `video-sync=display-resample` + `interpolation=yes` + `tscale=oversample`: sincroniza quadros ao refresh do monitor (60/120/144/240 Hz), sem judder.
2. **Job Object:** MPV via `CreateProcessA`, vinculado a Job Object com `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`:
   ```cpp
   JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
   jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
   SetInformationJobObject(g_player_job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
   AssignProcessToJobObject(g_player_job, pi.hProcess);
   ```
   O kernel encerra o MPV se a CLI terminar (`Ctrl+C`, `signal_handler`, fechar terminal, crash), sem zumbis.
**Consequências:** out-of-the-box: baixar zip, `isdbtb_rx.exe play -c 23 --player`, vídeo aprimorado abre sozinho; encerramento limpo.

<a id="adr-015"></a>
## ADR 015: UDP sem flush prematuro, alinhado a 1316 B
**Contexto:** `UdpStreamer` acumula TS de 188 B (ideal 1316 B). Em `src/main.cpp`, `streamer.flush()` era chamado ao fim de cada bloco USB de 32768 amostras (~16 ms); com 3 ou 5 pacotes válidos, completava com 4 ou 2 nulos (`PID 0x1FFF`) → 20–40% de overhead e jitter de PCR (micro-travamentos, dessincronia A/V).
**Decisão:** `streamer.flush_if_stale(80)`:
1. Recepção normal: envio só em múltiplos de 7 pacotes (1316 B puros do ar).
2. Datagrama parcial com padding só se houver pendência e nenhum pacote novo por > 80 ms (pausa/perda de sinal).
**Consequências:** sem nulos artificiais; PCR contínuo; A/V fluido em MPV e VLC.

<a id="adr-016"></a>
## ADR 016: Proteção do barramento USB (erro -9) e sintonia segura
**Contexto:** em `scan`/troca de canal, `rtlsdr_read_sync` seguido de `rtlsdr_set_center_freq` sem descarregar pendências → endpoint de controle 0 do RTL2832U travava (`rtlsdr_demod_write_reg failed with -9`, `LIBUSB_ERROR_PIPE`); escritas I2C no R820T falhavam (`r82xx_write: i2c wr failed=-9 reg=06`), tuner congelado no canal anterior.
**Decisão:** `safe_set_freq(dev, freq_hz)` em `src/main.cpp`:
1. `rtlsdr_reset_buffer(dev)` antes do I2C de frequência.
2. `rtlsdr_set_center_freq(dev, freq_hz)`.
3. `rtlsdr_reset_buffer(dev)` de novo.
4. `std::this_thread::sleep_for(std::chrono::milliseconds(100))` para o PLL fracionário do R820T travar antes de ganho/leitura.
5. Encerramento (`cmd_play`): após `reader_thread` terminar, `rtlsdr_reset_buffer(dev)` + 50 ms, então `rtlsdr_close(dev)` → `r82xx_standby` sem -9.
**Consequências:** fim de `LIBUSB_ERROR_PIPE -9` e `[R82XX] PLL not locked!`; scan 100% estável em UHF 14–69.

<a id="adr-017"></a>
## ADR 017: Consolidação das correções matemáticas (gr-isdbt)
**Contexto:** ISDB-T (ARIB STD-B31, ABNT NBR 15601) difere do DVB-T. Auditoria contra [git-artes/gr-isdbt](https://github.com/git-artes/gr-isdbt) achou 5 discrepâncias, corrigidas:
1. **Tap do PRBS ($X^{11} + X^2 + 1$):** ❌ shift à direita com XOR em `reg >> 9` ($1 + X^9 + X^{11}$) → metade dos SP invertidos. ✅ inserindo no bit 10 (`new_bit << 10`), $X^{11}$ = bit 0, $X^9$ = bit 2: `uint8_t new_bit = ((reg >> 2) ^ (reg >> 0)) & 0x01;`. Modo 3: PRBS avança pelas 5617 portadoras; Segmento 0 inicia na global `2592` (`6 * 432`).
2. **Viterbi:** ❌ $171_8$/$133_8$ sem espelhar, salvava `state & 1`, resetava treliça por símbolo. ✅ bit novo no LSB (`reg7 = (state << 1) | b`) → `POLYA = 0x4F`, `POLYB = 0x6D`; sobrevivente `state & 0x20`; treliça contínua via `decode_continuous(...)`.
3. **Chien/Forney:** ❌ avaliava $X_k$ em vez de $X_k^{-1}$; Forney sem multiplicar por $X_k$. ✅ $X_k^{-1} = \alpha^{(255 - p_{idx}) \bmod 255}$; Forney × $X_k = \alpha^{p_{idx}}$ (`FCR = 0`) → corrige até 8 bytes/pacote.
4. **Descrambler:** ❌ esperava `0xB8` a cada 8 pacotes (DVB-T). ✅ todos com `0x47`; gerador de 15 bits reseta por quadro de 204 símbolos (`frame_begin`), avança nos 203 bytes e dá 8 clocks sem XOR no `0x47`.
5. **IFO:** ❌ limiar 0.35 prendia em lóbulos de SP a 2 portadoras do centro. ✅ busca aberta `[-45 .. +45]`, coerência > 0.60 (real > 0.95), histerese de 4 símbolos consecutivos.

<a id="adr-018"></a>
## ADR 018: Fila SPSC lock-free com sequência e reset coordenado
**Contexto:** a fila do ADR 009 (`std::mutex q_mtx; std::condition_variable q_cv; std::queue<std::vector<uint8_t>> raw_queue;`) tinha 3 gargalos:
1. Alocação de `std::vector<uint8_t>(65536)` a cada ~16 ms (fragmentação, pressão no alocador).
2. `std::mutex` → context switches para o kernel do Windows em acessos simultâneos.
3. Drops silenciosos com fila cheia (64 blocos): DSP sem saber do salto temporal; NCO contínuo ($\phi[n] = \phi[n-1] + 2\pi\Delta f/F_s$) sofria saltos de fase → PLL fino instável, perda em cascata do TMCC.
**Decisão:** ring buffer SPSC lock-free estático (`include/dsp/spsc_capture_queue.hpp`):
1. **Memória estática:** `SpscCaptureQueue<SlotCount=64>` com `std::array<CaptureBlock, SlotCount>`; cada `CaptureBlock`: `std::array<uint8_t, 65536> raw`, `size_t size` (bytes válidos), `uint64_t sequence` (monotônico, do produtor), `bool discontinuity_before` (houve descarte imediatamente antes).
2. **Índices atômicos:** `tail_` (escrita) e `head_` (leitura) com `memory_order_acquire`/`memory_order_release`, isolados em `alignas(64)` (sem false sharing entre thread USB e DSP).
3. **Gaps e reset:** produtor sem espaço (`(tail - head) >= SlotCount`) incrementa `dropped_` e marca `pending_discontinuity = true` → próximo bloco com `discontinuity_before = true`. Consumidor (`src/main.cpp`): se `block->sequence != expected_sequence` ou `discontinuity_before`, chama `reset_reception_stream(...)`: limpa `stream_iq_buf`, reseta fase do NCO (`sync.reset()`) e o demodulador (`demod.reset()`).
**Consequências:** zero `malloc`/`new` na captura; aquisição/publicação < 15 ns (atômicos puros); lacunas USB não corrompem Forney/Viterbi.

<a id="adr-019"></a>
## ADR 019: Zero alocação no loop quente; `FixedDelayLine`/`DynamicDelayLine`
**Contexto:** bancos de atraso do 1-Seg:
1. Time deinterleaver: 384 subportadoras; subportadora $c$ atrasa $D_c = I \times (95 - (5c \bmod 96))$ símbolos, $I \in \{0, 4, 8, 16\}$; $I=4$ → máx. $4 \times 95 = 380$.
2. Bit deinterleaver: 4 ramos; QPSK 0 e 120 símbolos; 16QAM 0, 40, 80, 120.
3. Forney (bytes): 12 ramos; ramo $j$ ($0 \le j \le 11$) atrasa $17 \times (11 - j)$ bytes, máx. 187.
Versão preliminar: `std::deque` (`std::vector<std::deque<std::complex<float>>>` tempo, `std::vector<std::deque<uint8_t>>` Forney); `process_symbol` criava `std::vector` locais (`equalized`, `data_carriers`, `freq_deint`, `time_deint`) por chamada; saída Viterbi fatiada com `viterbi_byte_stream_.erase(begin(), begin() + 204)`.
**Problema:** `std::deque` aloca páginas no heap ligadas por vetor de ponteiros; push/pop alocam ou percorrem ponteiros não contíguos. A 1000 símbolos/s, 384 ramos = **384.000 ops de deque/s** → poluição L1/L2, cache misses. `erase(begin, begin + 204)` = `memmove` $O(N)$ por pacote.
**Decisão:** estruturas circulares contíguas (`include/dsp/delay_line.hpp`):
1. **`FixedDelayLine<T, MaxCapacity>`:** `std::array<T, MaxCapacity>` + índice circular `index_`, zero heap:
   ```cpp
   inline T push_and_get(const T& val) noexcept {
       if (delay_ == 0) return val;
       T out = data_[index_];
       data_[index_] = val;
       index_++;
       if (index_ >= delay_) index_ = 0;
       return out;
   }
   ```
   Forney: `FixedDelayLine<uint8_t, 187>` × 12; bits: `FixedDelayLine<float, 120>` × 4.
2. **`DynamicDelayLine<T>`:** um `std::vector<T>` alocado uma vez em `init()` via `assign(delay, initial_val)`; 384 ramos de tempo (`time_delay_lines_`, `time_csi_delay_lines_`), $I \in \{0, 4, 8, 16\}$ sem alocação em processamento.
3. **Membros `std::array` em `OneSegDemodulator`:** `std::array<std::complex<float>, 433> seg_carriers_`; `<std::complex<float>, 432>`: `channel_h_`, `equalized_`; `<float, 432> csi_weight_`; `<std::complex<float>, 384>`: `data_carriers_`, `freq_deint_`, `time_deint_`; `<float, 384>`: `data_csi_`, `freq_csi_`, `time_csi_`.
4. **Offset sliding no Viterbi:** `viterbi_stream_offset_` em vez de `erase`; blocos de 204 B consumidos em O(1); `std::memmove` só quando offset > 4096 B e > 50% do vetor (custo amortizado ~0).
**Consequências:** nenhum `malloc`/`free` no processamento contínuo; dados contíguos → máximo hit L1/L2 e hardware prefetching.

<a id="adr-020"></a>
## ADR 020: CSI centrado em 128 com atraso sincronizado
**Contexto:** no ADR 012, $W_l = \text{clamp}(|H_l|^2/P_{avg}, 0.0, 2.0)$ multiplicava o I/Q equalizado antes do demapeador: $\tilde{Y}_l = Y_l W_l$.
**Problemas:**
1. **Limiares 16QAM distorcidos:** níveis $\pm 1/\sqrt{10}$, $\pm 3/\sqrt{10}$; bits: $b_0 = 2/\sqrt{10} - |Im(Y)|$, $b_1 = 2/\sqrt{10} - |Re(Y)|$, $b_2 = -Im(Y)$, $b_3 = -Re(Y)$. Com $W_l \ll 1$, $|Re|,|Im| \to 0$ ⇒ $b_0 \to +2/\sqrt{10} > 0$: convicção positiva espúria em $b_0$, quebrando o mapeamento de Gray mesmo sem sinal.
2. **Dessincronia temporal:** no receptor o MSB atrasa 120 símbolos (`bit_delay_lines_[1]`), LSB 0. Peso sem atraso pondera o MSB de 120 símbolos atrás com o canal atual (viola causalidade).
**Decisão:** CSI sobre soft-bits centrados em 128 com linhas de atraso dedicadas:
1. **Demapeamento puro:** euclidiano sobre $\hat{X}_l$ sem $W_l$; soft 0–255, neutro 128 (`constants::SOFT_NEUTRAL = 128.0f`); 0 = bit 0 máx., 255 = bit 1 máx.
2. **Linhas paralelas para pesos:** `time_csi_delay_lines_` (384 × `DynamicDelayLine<float>`) e `bit_csi_delay_lines_` (4 × `FixedDelayLine<float, 120>`); $W_l$ percorre os mesmos atrasos que os dados.
3. **`scale_soft`:** $soft\_scaled = 128 + \text{clamp}(W(soft\_val - 128), -128, +127)$.
   - $W \to 0$: exatamente 128; distâncias de ramo $|128-0| = 128$ e $|128-255| = 127$ → diferencial nulo, nenhuma decisão espúria.
   - $W = 1.0$: inalterado. $W > 1.0$: convicção acentuada, clamp em [0, 255].
**Consequências:** fim dos erros de constelação em 16QAM; sem desalinhamento entre fading e métricas de ramo; mais ganho de codificação e estabilidade em canais móveis seletivos.

<a id="adr-021"></a>
## ADR 021: FSM tripartite de sincronismo OFDM
**Contexto:** Van de Beek ($\Gamma(d)$, $\Phi(d)$ do ADR 007). Para detectar o GI ($\in \{1/4, 1/8, 1/16, 1/32\}$), aplica folding em 2–8 símbolos numa janela de 32768–65536 amostras. Isso rodava sempre em `find_symbol_boundary`: > 65% da CPU do DSP mesmo com sinal estável.
**Decisão:** `SyncState` em `include/dsp/ofdm_sync.hpp`. Transições: início → `Acquisition` → (pico > 0.20, GI travado) → `Tracking` → (sucesso ±4) `Tracking` | (3 falhas) `LostLock` → próximo bloco → `Acquisition`.
1. **`Acquisition`:** busca global exaustiva dos 4 GIs ($1/16, 1/8, 1/4, 1/32$) com folding multissímbolo; escolhe maior $\rho$, calcula $d_0$; com $\rho > 0.20$ fixa `gi_`, prevê a fronteira do próximo bloco e vai para `Tracking`.
2. **`Tracking`:** GI fixo; janela de **±4 amostras** (`TRACK_RADIUS = 4`): $d_{pred} = (d_{expected} + sh + N_{sym}) \bmod N_{sym}$, $sh \in [-4, +4]$. Com $\rho > 0.20$: `consecutive_failures_ = 0`, atualiza fase e tempo.
3. **`LostLock`:** 3 falhas seguidas (`MAX_CONSECUTIVE_FAILURES = 3`); na chamada seguinte volta a `Acquisition`.
**Consequências:** sync de símbolo em `Tracking` cai de ~3.8 ms para **< 0.6 ms por bloco de 32768** (> 6×); janela ±4 imune a reflexões transitórias e ruído impulsivo.

<a id="adr-022"></a>
## ADR 022: Tabela direta 64 KB O(1) para GF(2^8)
**Contexto:** RS(204,188,t=8) sobre $GF(2^8)$, $p(x) = x^8 + x^4 + x^3 + x^2 + 1$ (0x11D). Multiplicações em 4 etapas: 16 síndromes $S_i = \sum_{j=0}^{203} r_j \alpha^{ij}$; Berlekamp-Massey ($\Lambda(x)$); Chien (204 posições, grau $t \le 8$); Forney.
**Problema da versão logarítmica:**
```cpp
uint8_t ReedSolomon::gf_mul(uint8_t a, uint8_t b) const {
    if (a == 0 || b == 0) return 0;
    return exp_table_[log_table_[a] + log_table_[b]];
}
```
1. Branch `a == 0 || b == 0` difícil de prever em laços intensivos.
2. 3 acessos indiretos (`log_table_[a]`, `log_table_[b]`, `exp_table_`).
3. Dependência serial impede autovetorização.
**Decisão:** tabela 2D direta de 64 KB (`include/dsp/reed_solomon.hpp`, `src/dsp/reed_solomon.cpp`):
```cpp
// Tabela 256 x 256 pré-computada na inicialização
uint8_t gf_mul_table_[256][256];

inline uint8_t ReedSolomon::gf_mul(uint8_t a, uint8_t b) const noexcept {
    return gf_mul_table_[a][b];
}
```
1. Gerada uma vez no construtor de `ReedSolomon` (256 × 256 = 65.536 B).
2. `gf_mul` = indexação 2D pura, sem `jmp`/`jne`.
3. Cabe em L2 (frequentemente na L1D de 48–64 KB) de Intel Alder/Raptor Lake, AMD Zen 3/4/5, ARM Cortex-A78/Apple Silicon: 3–4 ciclos.
**Consequências:** zero branches no RS; síndromes + BM + Chien ~40% mais rápidos.

<a id="adr-023"></a>
## ADR 023: Otimizações de build (IPO/LTO, -O3, fast-math)
**Contexto:** DSP em tempo real em CPUs de uso geral; overhead de chamadas, limites de inlining e asserts em runtime custam CPU e podem causar descarte USB.
**Decisão (`CMakeLists.txt`):**
1. **Nível:** GCC/MinGW-w64 `-O3 -DNDEBUG`; MSVC `/O2 /DNDEBUG`. Desliga `assert()`.
2. **IPO/LTO** com detecção automática:
   ```cmake
   include(CheckIPOSupported)
   check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
   if(ipo_supported)
       set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
   endif()
   ```
   Linker funde inlines entre unidades de tradução (ex.: `ReedSolomon::gf_mul`, `FixedDelayLine::push_and_get`, `SpscCaptureQueue::try_acquire_read`, deinterleavers), sem overhead de calling convention.
3. **Fast math opcional:**
   ```cmake
   option(ISDBT_ENABLE_FAST_MATH "Habilitar otimizacao de ponto flutuante rapido (-ffast-math / /fp:fast)" OFF)
   ```
   `-DISDBT_ENABLE_FAST_MATH=ON` → `-ffast-math` (GCC) / `/fp:fast` (MSVC): reassociação, FMA, autovetorização AVX2 em MMSE, energia de CP e rotações do NCO.
**Consequências:** binário compacto com inlining agressivo; estável em CPUs de baixo consumo (Intel Celeron, mini-PCs, Raspberry Pi 4/5).

<a id="adr-024"></a>
## ADR 024: `IOFDMSynchronizer`, dual-mode `--ofdm`, baseline preservado
**Contexto:** Van de Beek (ML, CP, FSM tripartite ±4) é eficiente, mas com atenuação severa, Rayleigh intenso ou Doppler (veículos) a correlação de CP dispersa e atrasa a recuperação. gr-isdbt usa rastreio contínuo de SP, correção de CFO inteiro e fracionário, acumulação de SCO e histerese de re-lock após 3 símbolos válidos. Diretriz: **preservação estrita do baseline** (Van de Beek bit-a-bit idêntico e padrão).
**Decisão:**
1. **Interface (`include/sync/iofdm_synchronizer.h`):** classe abstrata pura `IOFDMSynchronizer` com `process()`, `state()`, `reset()` virtuais.
2. **Baseline `VanDeBeekSync`:** Adapter sobre `OfdmSync`; `src/dsp/ofdm_sync.cpp` e `include/dsp/ofdm_sync.hpp` **100% inalterados**.
3. **`RobustGRISDBTSync`:** port nativo autocontido (sem GNU Radio, boost, pmt) de `isdb_t_sync_impl.cc`: aquisição coarse por CP; timing fino por SP; CFO inteiro via TMCC e fracionário via SP; SCO por deriva de fase acumulada; FSM com histerese de 3 símbolos. (Renomeado `SpCirSynchronizer` no ADR 028.)
4. **Fábrica (`src/sync/sync_factory.cpp`):** injeta a instância; estágios seguintes (equalizador, Viterbi, RS) não sabem qual está ativa.
5. **CLI:** `--ofdm` só em `play`, aceita `--ofdm 1`/`--ofdm 2` e `--ofdm=1`/`--ofdm=2`; padrão `--ofdm 1`; ignorada em silêncio fora de `play`.
**Consequências:** zero risco de regressão (`--ofdm 1` idêntico ao histórico); `--ofdm 2` para RF adversa; sintaxe alinhada a `-c 18`, `-p 1234`, `-g 28.0`; responsabilidades desacopladas.

<a id="adr-025"></a>
## ADR 025: Dual-architecture x86/x64, MPV i686, releases
**Contexto:** ainda há hardware Windows 32 bits (legado, industrial embarcado, portáteis, receptores antigos). O receptor, `librtlsdr.dll` e o MPV eram só 64 bits.
**Decisão:**
1. **CMake (`CMakeLists.txt`):** `TARGET_ARCH` (`x64`, `x86`, `auto`). x64: `-m64`. x86: `-m32 -msse2 -mfpmath=sse` (IEEE-754 em XMM, não x87 de 80 bits). Windows: `_WIN32_WINNT=0x0600` (declara `inet_pton` e WinCrypt `CALG_SHA_256`).
2. **Isolamento por ABI:** `tools/rtl-sdr/x64/`, `tools/rtl-sdr/x86/` (DLLs e utilitários oficiais); MPV 64 e 32 bits oficial (mpv-i686) por pasta (layout simétrico atual `tools/mpv/x64/` e `tools/mpv/x86/`, ver ADR 027). `src/platform/platform_utils.cpp` detecta `sizeof(void*) == 4` e prioriza `tools\mpv\x86\mpv.exe`.
3. **ABI Win32 x86:** callbacks do kernel (ex.: `SetConsoleCtrlHandler`) exigem `__stdcall`; lambda anônima trocada por função estática `WINAPI`.
4. **Release (`scripts/build_release.ps1`):** `-Arch all` (padrão), `x64` ou `x86`; CTest nas duas arquiteturas; staging isolado; zips `yyyymmdd_v1.0.0_win64.zip` e `yyyymmdd_v1.0.0_win32.zip`; `objdump -f` confere `pei-x86-64`/`pei-i386`.
5. **MPV i686 estável + smoke test:** só a release oficial `mpv-0.41.0-i686` (não snapshots `git`, que regrediram com `OpenSSL internal error: assertion failed: lock != NULL`); smoke test `mpv.exe --version` com saída 0 e sem erro OpenSSL antes de empacotar.
**Consequências:** Windows 7/8/10/11 em x64 e x86; pacotes autocontidos e portáteis; compila e passa 100% dos testes determinísticos em 32 e 64 bits.

<a id="adr-026"></a>
## ADR 026: Persistência local portátil (`channels.json`)
**Contexto:** `ChannelManager` salvava em `%APPDATA%\isdbtb_rx\channels.json` (Windows) / `~/.config/isdbtb_rx/channels.json` (Linux). Em contas sem privilégio, sem `%APPDATA%` ou em pendrive: problemas de permissão, caminhos ocultos, inconsistência entre instalações.
**Decisão:** sem consultar `%APPDATA%`, `USERPROFILE`, `HOME`.
1. `ChannelManager::get_default_config_path()` retorna só `"channels.json"`.
2. Leitura/gravação sempre no diretório de trabalho (portátil puro).
3. `channels.json` no `.gitignore`.
**Consequências:** app 100% autocontido (descompactar e rodar em qualquer pasta/pendrive); zero erro de permissão.

<a id="adr-027"></a>
## ADR 027: Segregação de ferramentas por arquitetura e layout do terminal
**Contexto:** (1) MPV e Osmocom RTL-SDR 64 bits soltos na raiz de `tools/mpv/` e `tools/rtl-sdr/`, 32 bits em `x86/`; diagnósticos Python misturados em `scripts/`. (2) `play -v` quebrava linha (*word wrap*) e duplicava linhas ao redimensionar o terminal.
**Decisão:**
1. **Simetria:** `tools/rtl-sdr/x64/`, `tools/rtl-sdr/x86/` (DLLs e executáveis oficiais por ABI); `tools/mpv/x64/`, `tools/mpv/x86/` (MPV portátil completo por ABI); `calibrate_ppm.py`, `quick_rf_scan.py` em `tools/`; `scripts/` só automação (`build_release.ps1`) e lançadores do player.
2. **Terminal:** `get_terminal_size()`; com `-v`, status RF por posicionamento ANSI absoluto na **antepenúltima linha** (`height - 2`), truncado em `width - 1`; última linha (`height`) com legenda em **fundo branco/letras pretas** (`\033[30;47m`), sem `\n` final (não rola o buffer).
**Consequências:** repositório simétrico por arquitetura; UX estável, sem piscar/duplicar linhas.

<a id="adr-028"></a>
## ADR 028: Literatura formal, SP-CIR puro e fim da deriva de CFO
**Contexto:** testes longos no Canal 28 (SBT HD, 557.143 MHz), recepção urbana real, antena UHF portátil, multipercurso severo, `--ofdm 2`:
- Início: RSSI -6.0 dBFS, correlação 1.00, `TMCC: LOCK`, SNR 9.5 dB, zero perda TS.
- Após ~90 s (~28.123 pacotes): SNR 9.5 → 2.0 dB, `TMCC: SYNC`, RS descartando (5 corrigidos / 43 dropados).
- Reiniciar com os mesmos parâmetros voltava a 9.5 dB, `TMCC: LOCK`, 0 drops.

**Diagnóstico:**
1. **Viés angular do CP em multipercurso:** Van de Beek et al. (1997): $\gamma = \sum_{m=0}^{N_{GI}-1} r(m) r^*(m + N_{FFT})$. Em AWGN sem dispersão, $\arg(\gamma) = -2\pi \Delta f T_{sym}$. Em canais urbanos (prédios, SFN com ecos dentro do GI), fading seletivo e ICI introduzem viés estático $\theta_{bias} \neq 0$ (Daecke & Saupe, 2000; Minn et al., 2003; Speth & Meyr, 1999).
2. **Deriva por integração aberta:** a cada bloco USB (32768 amostras ≈ 16.1 ms) `current_cfo_hz_ += cfo_gain * ffo_res;` com `ffo_res` da fase de $\gamma$. Com $\theta_{bias}$ ≈ 0.01 rad, acumulava ~+1.5 Hz/bloco × 62 blocos/s → centenas de Hz em 90 s. TMCC DBPSK girava, BCH(273,191) falhava, Viterbi recebia soft-bits degradados, SNR 9.5 → 2.0 dB. Reiniciar zerava o NCO → falha interna, não do canal.
3. **Memória do PDP (`pdp_avg_`):** IIR $P[t] = 0.85 P_{antigo}[t] + 0.15 P_{novo}[t]$ mantinha 85% da energia em bins fixos da IDFT 64; ao mover a janela FFT, o pico antigo puxava de volta → histerese rumo à ISI.

**Decisão:**
1. **Regra permanente:** todo algoritmo de sincronismo, estimação e DSP **deve** seguir estritamente a literatura formal de comunicações digitais e arquiteturas canônicas. Proibidos enxertos heurísticos que violem os artigos de referência.
2. **Literatura adotada:**
   - M. Speth, S. A. Fechtel, G. Fock, H. Meyr, *"Optimum receiver design for wireless broad-band systems using OFDM - Part I: Frequency domain interpretation, synchronization, and channel estimation"*, IEEE Trans. Commun., vol. 47, no. 11, pp. 1668–1677, Nov. 1999.
   - M. Speth, S. Fechtel, G. Fock, H. Meyr, *"Optimum receiver design for OFDM-based broadband transmission - Part II: A case study"*, IEEE Trans. Commun., vol. 49, no. 4, pp. 571–578, Apr. 2001.
   - F. Larroca et al., *"gr-isdbt: An Open-Source ISDB-T Physical Layer Receiver Implementation"*, IEEE Latin America Transactions, 2014.
   - J.-J. van de Beek, M. Sandell, P. O. Börjesson, *"ML estimation of time and frequency offset in OFDM systems"*, IEEE Trans. Signal Process., vol. 45, no. 7, pp. 1800–1805, Jul. 1997.
   - H. Minn, V. K. Bhargava, K. B. Letaief, *"A robust timing and frequency synchronization for OFDM systems"*, IEEE Trans. Wireless Commun., vol. 2, no. 4, pp. 822–839, Jul. 2003.
3. **Sincronismo em dois estágios (Speth & Meyr):**
   - **Estágio 1 (coarse, CP):** Van de Beek só em `Acquisition`: borda do símbolo, GI ótimo (1/16, 1/8, 1/4, 1/32), CFO fracionário inicial.
   - **Estágio 2 (fino, SP-CIR):** adquirido e com TMCC ancorado, rastreio na frequência pela CIR da IDFT de 64 pts sobre os 36 SP do Segmento 0 centrados no bin 808.
   - **CFO com passa-baixas assintótico + zona morta (Opção 2 aprovada):** congelar o CFO da aquisição regrediu `ch24_10s_iq.cu8` de 2 → 0 pacotes: bloco 1 estimou -111.9 Hz, real -229.1 Hz (resíduo 117.2 Hz ≈ 12% do espaçamento entre portadoras, 125°/símbolo). Solução: ganho 0.20 na aquisição, 0.08 ancorado ao TMCC, **zona morta ±0.5 Hz**:
     ```cpp
     if (sp_tmcc_anchored_ && std::abs(ffo_res) < 0.5f) {
         ffo_res = 0.0f;
     }
     float cfo_gain = sp_tmcc_anchored_ ? 0.08f : 0.20f;
     current_cfo_hz_ += cfo_gain * ffo_res;
     ```
     Converge suavemente os 117.2 Hz até -229.1 Hz (recupera os **2 pacotes TS válidos** no Canal 24); com $|ffo\_res| < 0.5$ Hz, `ffo_res` = 0 → **NCO congelado em regime**, sem integrar $\theta_{bias}$; SNR estável em 9.5 dB no Canal 28.
   - **CIR instantânea + First Arrival Path:** remove a média IIR `pdp_avg_`; CIR instantânea com coerência PAPR ≥ 12.0; em $\tau \in [-16 \dots +16]$ escolhe o 1º caminho ≥ 25% de $P_{\max}$ (evita centro de gravidade de reflexões, blinda contra ISI).
4. **Nome canônico `SpCirSynchronizer`:** substitui `RobustGRISDBTSync` (`include/sync/sp_cir_sync.h`, `src/sync/sp_cir_sync.cpp`); alias de retrocompatibilidade `using RobustGRISDBTSync = SpCirSynchronizer`.

**Consequências:**
- Canal 28 (e multipercurso urbano em geral) estável por horas, SNR ~9.5 dB; desvios iniciais de cristal/PPM até ~120 Hz (Canal 24) convergem e entregam TS.
- Fiel a Speth & Meyr e ao gr-isdbt, sem enxertos.
- Nomenclatura canônica (`SpCirSynchronizer`).
- 27/27 CTest em x64 e x86; golden `tests/golden/ch24_baseline_ofdm2.stats.json` com 2 pacotes válidos, SHA256 `bb5f8990202b6687bad0671495b058a1a8f7bf2e340c75b485769e8fabbd306a`.
