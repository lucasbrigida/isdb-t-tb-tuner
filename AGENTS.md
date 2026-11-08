# 🤖 Guia de Orientação para Multi-Agentes de IA (AGENTS.md)

Este documento foi elaborado especificamente para orientar **agentes de Inteligência Artificial** (e desenvolvedores humanos) que forem continuar, manter ou expandir o desenvolvimento deste decodificador universal ISDB-T/ISDB-Tb.

---

## 🎯 1. Visão Geral e Escopo do Projeto

O objetivo deste projeto é fornecer uma aplicação de linha de comando (CLI) nativa, de alta performance, sem dependências do framework GNU Radio, capaz de:
1. Descobrir e controlar dongles **RTL-SDR** (RTL2832U com tuners R820T/R828D/E4000).
2. Sintonizar e decodificar sinais de TV Digital no padrão **ISDB-T** (Japão, Filipinas, etc.) e **ISDB-Tb / SBTVD** (Brasil e América Latina).
3. Realizar o processamento completo de Camada Física (DSP) e Camada de Aplicação (PSI/SI, gerenciamento de canais e streaming UDP para VLC e MPV).

> [!IMPORTANT]
> **Foco de Camada Física:** O projeto tem como escopo principal o modo **1-Seg (One-Seg)** móvel (~428.57 kHz de largura útil). O RTL-SDR possui limite seguro de amostragem de ~2.4 MSps (largura útil ~2.0 MHz), o que inviabiliza amostrar os 5.7 MHz de um sinal Full-Seg (13 segmentos) sem perda drástica de pacotes USB. Se for estender para Full-Seg, utilize hardware SDR de banda larga (HackRF, Airspy, LimeSDR, USRP).

---

## 📐 2. Invariantes Arquiteturais Críticos ("Não Quebre")

Qualquer agente que editar o código-fonte deve respeitar estritamente as seguintes invariantes:

### 1. Taxa de Amostragem Exata (\(2031746\text{ S/s}\))
- A taxa de chip base do padrão ISDB-T é exatamente:
  \[
  F_{base} = \frac{512}{63}\text{ MHz} \approx 8.126984\text{ MHz}
  \]
- Para o 1-Seg com RTL-SDR, operamos no sub-múltiplo exato de \(1/4\):
  \[
  F_s = \frac{F_{base}}{4} = \frac{512}{63 \times 4}\text{ MHz} \approx 2.031746\text{ MSps} = 2031746\text{ Hz}
  \]
- **Regra:** **NUNCA** mude a taxa para \(2.048\text{ MSps}\) fixos sem reamostragem racional. A taxa de \(2031746\text{ S/s}\) alinha perfeitamente o número de pontos de FFT por símbolo (Mode 3 = 2048 pontos, Mode 2 = 1024 pontos, Mode 1 = 512 pontos) e garante que o pico de correlação do prefixo cíclico trave com \(>90\%\) de correlação!

### 2. Pacotes UDP para VLC / MPV (\(7 \times 188 = 1316\text{ bytes}\)) e URL de Streaming
- Cada datagrama UDP enviado para `udp://@127.0.0.1:1234` **DEVE** conter exatamente 7 pacotes MPEG-TS completos de 188 bytes (\(7 \times 188 = 1316\text{ bytes}\)).
- **Motivo:** 1316 bytes é o tamanho padrão da indústria de IPTV que cabe no MTU de 1500 bytes da Ethernet sem causar fragmentação de pacotes no kernel IP.
- **Regra do Player:** A URL padrão de escuta é `udp://@127.0.0.1:1234`.
- **Regra:** Nunca envie pacotes soltos de 188 bytes via UDP individualmente (causa sobrecarga severa de interrupções de rede e travamentos no player).

### 3. Modo Binário no Windows (`_setmode`)
- No Windows, a saída padrão (`stdout`) e arquivos binários abrem em modo texto por padrão (onde bytes `0x0A` (`\n`) viram `0x0D 0x0A` (`\r\n`)).
- **Regra:** Se for redirecionar pacotes TS para pipes (`stdout`), `Platform::set_binary_stdout()` (`_setmode(_fileno(stdout), _O_BINARY)`) deve permanecer ativo.

### 4. Persistência de Canais em JSON (Modo Portátil e Estritamente Local)
- O arquivo de persistência de canais `channels.json` é armazenado **estritamente e exclusivamente no diretório local de execução do aplicativo** (`./channels.json`).
- **Regra:** Não utilize nem consulte pastas ocultas de sistema do usuário (`%APPDATA%` ou `~/.config/`). Isso garante portabilidade total (*portable app*), elimina falhas de permissão e permite que o executável rode de qualquer pasta ou pendrive de forma independente. Use sempre a classe `ChannelManager` e `std::filesystem`.

### 5. Fila SPSC Lock-Free e Resets Coordenados de NCO
- O desacoplamento entre a leitura USB e o processamento DSP **DEVE** utilizar `SpscCaptureQueue<64>` com ring buffer estático pré-alocado.
- A fila monitora números de sequência monotônicos e sinaliza lacunas (`discontinuity_before`).
- **Regra:** Se uma descontinuidade temporal ocorrer, acione `reset_reception_stream` para descarregar `stream_iq_buf` e reiniciar as fases do NCO. Nunca permita que o sincronizador tente computar correlação temporal sobre saltos de amostras.

### 6. Zero Alocações no Heap no Loop Quente de DSP
- Todas as estruturas de retardo de desentrelaçamento utilizam contêineres contíguos circulares de capacidade fixa: `FixedDelayLine<T, MaxCapacity>` (para Forney $12 \times 187$ bytes e bits $4 \times 120$ floats) e `DynamicDelayLine<T>` (com pré-alocação estática única na inicialização para os 384 ramos de tempo).
- Todos os buffers temporários de `process_symbol` em `OneSegDemodulator` são membros de classe pré-alocados em `std::array`.
- O buffer de bytes de Viterbi avança via ponteiro deslizante (`viterbi_stream_offset_`), sem `erase(begin, begin + 204)`.
- **Regra:** Nenhuma chamada a `malloc`, `new`, `std::vector::erase` ou `std::deque` é permitida durante o processamento contínuo de símbolos.

### 7. Ponderação CSI Centrada em 128 com Sincronismo de Retardo de Bits
- O demapeamento euclidiano de constelação é calculado sobre os dados equalizados puros (preservando limiares em 16QAM).
- Soft-bits são normalizados entre 0 e 255 com valor neutro 128 (`constants::SOFT_NEUTRAL`).
- Os pesos de confiabilidade CSI $W_l$ atravessam linhas de atraso idênticas às dos dados (`time_csi_delay_lines_` e `bit_csi_delay_lines_`, sofrendo os mesmos 120 símbolos de atraso no MSB).
- **Regra:** A escala é calculada estritamente como `128 + clamp(W * (soft - 128), -128, +127)`, garantindo que subportadoras em desvanecimento ($W \to 0$) convirjam exatamente para 128 (apagamento na Trellis).

### 8. Máquina de Estados Tripartite de Sincronismo OFDM
- O sincronizador `OfdmSync` opera em 3 estados: `Acquisition` (busca exaustiva dos 4 intervalos de guarda com folding), `Tracking` (rastreamento estreito em janela de $\pm 4$ amostras em torno de $d_{\text{expected}}$) e `LostLock` (histerese de 3 falhas consecutivas antes de reverter a `Acquisition`).
- **Regra:** Em regime de rastreamento, nunca recalcule exaustivamente todos os intervalos de guarda em cada bloco de amostras.

### 9. Multiplicação em $GF(2^8)$ em $O(1)$ sem Branches no Reed-Solomon
- As operações de campo finito no decodificador $RS(204, 188)$ utilizam a tabela direta bidimensional pré-calculada `gf_mul_table_[256][256]` (64 KB).
- **Regra:** Nunca introduza branches condicionais (`if (a == 0 || b == 0)`) ou consultas indiretas com logs no laço quente de Berlekamp-Massey, Chien Search e Forney.

### 10. Dual-Mode de Sincronismo OFDM e Preservação Estrita do Baseline (`VanDeBeekSync`)
- O sincronizador de Van de Beek (`src/dsp/ofdm_sync.cpp` e `include/dsp/ofdm_sync.hpp`) é o baseline de referência bit-a-bit inalterado (**REGRA DE OURO**). NUNCA altere sua lógica, sua FSM tripartite ou seus limiares.
- A abstração `IOFDMSynchronizer` permite selecionar via fábrica `make_synchronizer`:
  - `--ofdm 1` (`VanDeBeekSync`): Adaptador puro sobre a implementação existente.
  - `--ofdm 2` (`SpCirSynchronizer` / `RobustGRISDBTSync`): Sincronizador SP-CIR de dois estágios fundamentado em Speth & Meyr (1999/2001) e port do `gr-isdbt` (`src/sync/sp_cir_sync.cpp`) com refinamento SP-CIR, First Arrival Path, CFO estável sem deriva sob multipercurso urbano e FSM com histerese.
- **Sintaxe da Flag:** Suporta tanto `--ofdm 1` / `--ofdm 2` (com espaço) quanto `--ofdm=1` / `--ofdm=2` (com sinal de igual).
- **Regra da Flag:** `--ofdm` é exclusiva do comando `play`. Em qualquer outro comando (`scan`, `probe`, `epg`, `list`), ela DEVE ser silenciosamente ignorada sem gerar erros.

### 11. Compilação Dual-Architecture (`TARGET_ARCH=x64` e `TARGET_ARCH=x86`)
- O projeto suporta nativamente 64 bits (`-m64`) e 32 bits (`-m32 -msse2 -mfpmath=sse`).
- Handlers e callbacks Win32 no x86 exigem convenção `WINAPI` (`__stdcall`) estrita.
- Executáveis e DLLs de 32 bits e 64 bits do RTL-SDR e MPV devem ser mantidos estritamente isolados em suas pastas de arquitetura (`tools/rtl-sdr/x86/`, `tools/rtl-sdr/x64/`, `tools/mpv/x86/` e `tools/mpv/x64/`).
- Utilitários diagnósticos Python (`calibrate_ppm.py`, `quick_rf_scan.py`) e ferramentas de captura/síntese residem formalmente no diretório `tools/`, mantendo `scripts/` reservado a automações operacionais e lançadores de player.

### 12. Motor de Pilotos Dispersos (SP), CIR e Detecção de Primeiro Caminho no `SpCirSynchronizer`
- No sincronizador SP-CIR (`--ofdm 2` / `SpCirSynchronizer`), o refinamento de timing e o rastreamento da Resposta ao Impulso do Canal (CIR) utilizam a IDFT de 64 pontos sobre os pilotos dispersos (SP) do Segmento 0 (36 subportadoras úteis centradas no bin 808).
- **Regra de Coerência (PAPR $\ge 12.0$):** A métrica de coerência exige estritamente $\text{PAPR} \ge 12.0$. Em ruído gaussiano puro, a PAPR na IDFT de 64 pontos tem média teórica $\approx 4.73$ (com probabilidade de exceder 10 inferior a $10^{-3}$). Se $\text{PAPR} < 12.0$, a métrica de CIR DEVE retornar $0.0$, acionando fallback transparente para o estimador por Prefixo Cíclico (CP), impedindo perturbações na malha PI de timing/SCO por ruído térmico.
- **Regra de Primeiro Caminho (*First Arrival Path*):** Em canais com multipercurso urbano severo (onde reflexões retardadas em prédios podem ter magnitude superior à linha de visada direta), a busca de atraso $\tau \in [-16 \dots +16]$ DEVE selecionar o primeiro pico com energia $\ge 25\%$ da potência de pico máxima ($P_{\max}$), evitando a atração do sincronizador pelo centro de gravidade da CIR e prevenindo interferência intersimbólica (ISI).
- **Regra de Estabilidade de CFO (ADR 028):** O rastreamento de CFO residual em bloco utiliza filtro passa-baixas assintótico com ganho suave ($0.08$ quando ancorado) e **zona morta estrita de $\pm 0.5\text{ Hz}$** (`if (sp_tmcc_anchored_ && std::abs(ffo_res) < 0.5f) ffo_res = 0.0f;`). Essa regra garante que desvios de sintonia inicial de até $\sim 120\text{ Hz}$ do oscilador do RTL-SDR sejam convergidos suavemente, enquanto em regime permanente ($|res| < 0.5\text{ Hz}$) o integrador congela totalmente, eliminando derivas de frequência cumulativas provocadas pelo viés de fase do multipercurso urbano no Canal 28.
- **Regra de Zero Alocações:** Os planos FFTW3 (`fft_plan_`, `ifft_cir_plan_`) e buffers (`fft_in_`, `fft_out_`, `cir_in_`, `cir_out_`) são membros pré-alocados de classe com destruição explícita no destrutor. Nenhuma alocação dinâmica é permitida no loop quente de símbolos.

---

## 🛡️ 3. Histórico de Erros Corrigidos e Regras de Conformidade com `git-artes/gr-isdbt` ("NÃO REPITA ESTES ERROS")

Durante a auditoria e desenvolvimento do decodificador contra a implementação de referência [`git-artes/gr-isdbt`](https://github.com/git-artes/gr-isdbt) e em testes reais com sinais do ar, 20 erros críticos foram identificados, corrigidos e documentados. **Nenhum agente futuro deve reintroduzir qualquer um dos padrões incorretos abaixo:**

### Erro 1: Tap Invertido no LFSR do PRBS dos Pilotos (`X^11 + X^2 + 1`)
- ❌ **O que estava errado:** O gerador PRBS de 11 bits (`reg = 0x7FF`) deslocava para a direita (`reg >> 1`), mas fazia XOR com `reg >> 9` pensando em $1 + X^9 + X^{11}$. Isso invertia a polaridade de ~50% dos pilotos dispersos (SP) e destruía a estimativa de canal.
- ✅ **Como é o correto (`oneseg_demodulator.cpp` / `gr-isdbt`):** Em um registrador de 11 bits que desloca para a direita e insere no bit 10 (`<< 10`), o estágio $X^{11}$ é o bit `0` e o estágio $X^9$ é o bit `2` (`11 - 9 = 2`):
  ```cpp
  uint8_t new_bit = ((reg >> 2) ^ (reg >> 0)) & 0x01;
  reg = (reg >> 1) | (new_bit << 10);
  ```
- **Além disso:** No Modo 3, o PRBS avança ao longo das 5617 portadoras ativas do canal; o Segmento 0 (físico 6) começa no índice global `2592` (`6 * 432`) e usa a portadora `2592 + 432` como piloto da borda direita.

### Erro 2: Polinômios e Bit de Sobrevivente no Decodificador Viterbi (`viterbi_decoder.cpp`)
- ❌ **O que estava errado:**
  1. Usava `POLY_G1 = 0171` (`0x79`) e `POLY_G2 = 0133` (`0x5B`) com registrador `reg7 = (state << 1) | b` (onde o bit mais recente entra no LSB), sem espelhar os 7 bits dos polinômios.
  2. No traceback, salvava `if (state & 1)` em vez do bit mais antigo que sai de `state` (`if (state & 0x20)`).
  3. Reiniciava o trellis (`path_metric`) a cada símbolo OFDM (768 bits), quebrando a continuidade entre símbolos.
- ✅ **Como é o correto (`viterbi_decoder.cpp` / `gr-isdbt`):**
  1. Usar `POLY_G1 = 0x4F` (`POLYA`) e `POLY_G2 = 0x6D` (`POLYB`) para `reg7 = (state << 1) | b`.
  2. Salvar `if (state & 0x20)` no bit sobrevivente e reconstruir `prev_state = (cur_state >> 1) | (msb_was_1 ? 0x20 : 0)`.
  3. Usar `decode_continuous(...)` no fluxo de símbolos OFDM, mantendo métricas de caminho, buffer residual de despunção e janela de sobreviventes contínuos, além de atribuir métrica `0` para bits puncionados (`soft_bit == 128`).

### Erro 3: Busca de Chien e Algoritmo de Forney no Reed-Solomon (`reed_solomon.cpp`)
- ❌ **O que estava errado:** Na busca de Chien para $\Lambda(x) = \prod (1 - X_k x)$, avaliava `x_inv = exp_table_[p_idx]` ($X_k$) em vez de $X_k^{-1} = \alpha^{255 - p_{\text{idx}}}$. E no algoritmo de Forney (`FCR = 0`), omitia a multiplicação por $X_k$ no numerador.
- ✅ **Como é o correto (`reed_solomon.cpp`):**
  - Raiz na busca de Chien: `uint8_t x_inv = exp_table_[(255 - p_idx) % 255];`
  - Magnitude de erro de Forney (`FCR = 0`): `uint8_t err_val = gf_mul(x_k, gf_div(num, den));`

### Erro 4: Descrambler DVB-T (`0xB8`) vs. Descrambler ISDB-T (`descrambler.cpp`)
- ❌ **O que estava errado:** Esperava sync byte invertido `0xB8` a cada 8 pacotes (padrão europeu DVB-T) e pausava o LFSR durante o byte `0x47`.
- ✅ **Como é o correto no ISDB-T (`descrambler.cpp` + `oneseg_demodulator.cpp` / `gr-isdbt`):**
  - **Não existe `0xB8` no ISDB-T.** Todos os pacotes têm `0x47`.
  - O PRBS (`0x4A80` / `0xA9`) é reiniciado **uma vez por quadro OFDM** (204 símbolos) no pulso `frame_begin` do TMCC.
  - Na saída do Viterbi / Forney do ISDB-T, cada bloco de 204 bytes tem os 203 bytes de dados/paridade nas posições `0..202` e o sync `0x47` na posição `203` (ramo 11 do Forney, atraso 0).
  - Durante o byte `0x47`, o PRBS **avança 8 clocks** (`clock_byte()`), embora não faça XOR no `0x47`.

### Erro 5: Descarte de Amostras e Reset de Fase entre Leituras USB (`main.cpp` e `ofdm_sync.cpp`)
- ❌ **O que estava errado:**
  1. `find_symbol_boundary` retornava `best_d` em qualquer posição do buffer de 32768 amostras, pulando símbolos anteriores, e `main.cpp` descartava as amostras finais que não completavam `2176` amostras a cada leitura `rtlsdr_read_sync`.
  2. `correct_frequency_offset` reiniciava a fase do NCO em `0` a cada bloco de 32768 amostras.
  3. `main.cpp` injetava blocos `standby` sintéticos (com PAT/PMT falsas em `PID 0x0000`) sempre que um bloco de leitura não gerava pacotes TS imediatos, sobrescrevendo a PAT real da emissora no VLC.
- ✅ **Como é o correto (`main.cpp` e `ofdm_sync.cpp`):**
  1. `find_symbol_boundary` projeta `first_d = best_d % total_symbol_len` para apontar sempre para o primeiro símbolo do buffer.
  2. `OfdmSync::correct_frequency_offset` acumula `current_phase_` continuamente entre blocos.
  3. `cmd_play` acumula amostras em `stream_iq_buf` preservando as amostras residuais entre leituras USB, aplica ajuste fino de símbolo (`fine_symbol_sync`) e **não** injeta pacotes `standby` sintéticos sobre o fluxo real.

### Erro 6: Tabela de Permutação em Frequência e Atraso do Bit Interleaver
- ✅ **Regra:** A tabela `FREQ_PERM_MODE3[384]` em `src/dsp/oneseg_demodulator.cpp` é uma bijeção exata de `0..383` idêntica a `d_random_perm_mode3` em `gr-isdbt/lib/frequency_deinterleaver_impl.cc`. Nunca a substitua por fórmulas modulares aproximadas. No desentrelaçador de bits QPSK, o MSB ($\text{Re} < 0$) sofre atraso de `120` símbolos no receptor (`bit_deint_fifos_[1]`), enquanto o LSB ($\text{Im} < 0$) tem atraso `0`.

### Erro 7: Injeção de Standby Sintético (`TsGenerator`) Gerando Tela Verde no VLC
- ❌ **O que estava errado:** O `cmd_play` transmitia blocos de `TsGenerator` por padrão antes e durante a recepção se `decoded_pkts` estivesse vazio. O `TsGenerator` produz uma imagem verde de teste com PAT apontando para PMT `0x1000` e vídeo em PID `0x0100`. O demuxer do VLC travava nesses PIDs e ignorava os pacotes reais da transmissão da emissora (ex: BAND HD com vídeo no PID `0x0111` e áudio no PID `0x0112`).
- ✅ **Como é o correto (`main.cpp`):** O standby sintético com imagem de teste só é transmitido se a flag `--test-pattern` for explicitamente requisitada pelo usuário. Na recepção normal, nenhum pacote sintético de tela verde é enviado.

### Erro 8: Reset Prematuro do Demodulador em Quedas Transientes de Correlação
- ❌ **O que estava errado:** O contador `unlock_counter > 10` acionava `demod.reset()` com apenas 10 leituras de 32768 amostras (~160 ms) sem pico de correlação do prefixo cíclico. Porém, os desentrelaçadores de tempo ($I=4$, 384 símbolos $\approx 435$ ms) e convolucional Forney (187 pacotes $\approx 675$ ms) requerem cerca de 1.1 segundos contínuos para iniciar a entrega dos primeiros pacotes válidos. Qualquer flutuação de sinal de 160 ms reiniciava as FIFOs a zero, impedindo a emissão de qualquer pacote.
- ✅ **Como é o correto (`main.cpp`):** O limiar de perda de sincronismo foi estendido para `unlock_counter > 200` (~3.2 segundos contínuos de perda de portadora antes de reiniciar o estado do demodulador).

### Erro 9: Ausência de PAT (PID 0x0000) na Recepção Parcial 1-Seg do ISDB-Tb
- ❌ **O que estava errado:** No padrão ISDB-Tb (ABNT NBR 15603), o Segmento 0 (1-Seg móvel) transmite seu PMT diretamente no PID reservado `0x1FC8`, sem transmitir uma PAT no PID `0x0000` (que trafega nos segmentos Full-Seg 1 a 12). Reprodutores de vídeo genéricos como o VLC exigem uma PAT no PID `0x0000` para mapear os fluxos elementares de vídeo e áudio.
- ✅ **Como é o correto (`psi_parser.cpp` / `main.cpp`):** `PsiParser::build_pat_packet` sintetiza periodicamente (~10 vezes por segundo) um pacote PAT válido com CRC32 apontando o programa ativo para o PMT `0x1FC8`, permitindo que o VLC abra e mapeie imediatamente as trilhas de vídeo H.264 (`0x0111`) e áudio AAC (`0x0112`).

### Erro 10: Tempo de Estabilização do PLL do Tuner R820T
- ❌ **O que estava errado:** Chamar `rtlsdr_set_center_freq` seguido imediatamente de leitura de amostras ou ajuste de ganho sem intervalo causava o erro `[R82XX] PLL not locked!` no driver do tuner.
- ✅ **Como é o correto (`main.cpp`):** Inserir um intervalo de 100 ms (`std::this_thread::sleep_for(std::chrono::milliseconds(100))`) logo após a sintonia da frequência para que o PLL do chip Rafael Micro trave com estabilidade antes da leitura de amostras.

### Erro 11: Travamento Prematuro de IFO em Lóbulos Laterais com Limiar Baixo (0.35)
- ❌ **O que estava errado:** O limiar de coerência `max_coh > 0.35f` permitia que lóbulos laterais espúrios de pilotos dispersos (coerência de pico ~0.36) fossem aceitos como portadora central de 1-Seg (`ifo_locked_ = true`), restringindo a busca subsequente estritamente a `[ifo_bins_, ifo_bins_]`. Preso a 2 portadoras de distância da frequência real (ex: IFO=-25 em vez de -27), as 4 portadoras de TMCC (101, 131, 286, 349) liam dados arbitrários em vez de DBPSK, impossibilitando a validação da paridade BCH(273, 191) e mantendo o receptor indefinidamente em `TMCC: SYNC` e `TS do Ar: 0 pkts`.
- ✅ **Como é o correto (`oneseg_demodulator.cpp`):** Enquanto `tmcc_locked_` for falso, a busca de IFO permanece aberta na faixa `[-45 .. +45]` subportadoras e exige coerência $> 0.60$ (o piloto verdadeiro atinge coerência $> 0.95$) com histerese de 4 símbolos consecutivos idênticos antes do travamento. Apenas quando o TMCC confirma o sincronismo e a paridade de quadro é que a busca de IFO é restrita.

### Erro 12: Bloqueio do Loop de Captura USB no Mesmo Thread de DSP (Buffer Dropouts)
- ❌ **O que estava errado:** `rtlsdr_read_sync` era chamado de forma síncrona dentro do mesmo loop que executava FFTW, desentrelaçamento temporal, decodificação Viterbi e Reed-Solomon. Durante os ~15-20 ms em que a CPU processava esses algoritmos matemáticos e enviava datagramas UDP, o buffer de endpoint USB de 64 KB do RTL-SDR no kernel sofria overflow, causando perda de amostras contínuas no barramento USB. Essa descontinuidade de dados quebrava a sequência de 204 símbolos do quadro TMCC, impedindo que o receptor atingisse `TMCC: LOCK`.
- ✅ **Como é o correto (`main.cpp`):** A leitura USB é isolada em um thread dedicado de alta prioridade (`reader_thread`), que preenche continuamente uma fila thread-safe (`std::queue<std::vector<uint8_t>>` protegida por `std::mutex` e `std::condition_variable` com limite de 64 blocos). O thread principal consome dessa fila sem nunca bloquear o hardware do RTL-SDR.

### Erro 13: Invocação de VLC via Shell vs. ShellExecute e Exibição de Título na Barra do VLC
- ❌ **O que gerou confusão:**
  1. No Windows, o comando de disparo `cmd.exe /c start` com aspas aninhadas e caminhos com espaços (ex: `Program Files`) falhava silenciosamente ao repassar os argumentos ao player.
  2. **Comportamento visual da barra de título do VLC:** Ao abrir a URL `udp://@127.0.0.1:1234`, o módulo de rede do VLC recebe e utiliza o `@` para fazer o `bind()` de escuta local na porta. Contudo, o formatador de interface gráfica do VLC (Qt) intencionalmente remove o `@` ao compor o título da janela, mostrando visualmente apenas `udp://127.0.0.1:1234 - VLC media player`. Isso é o comportamento nativo do VLC e **não** significa que o `@` foi perdido pelo socket de rede.
- ✅ **Como é o correto (`main.cpp`):** Utilizar a API nativa do Win32 `ShellExecuteA(NULL, "open", vlc_bin, streamer.url().c_str(), NULL, SW_SHOWNORMAL)` passando a URL `udp://@127.0.0.1:1234` diretamente ao executável do VLC em Arquivos de Programas, contornando qualquer falha de parsing do `cmd.exe`.

### Erro 14: Colisão de Endpoints USB (Erro -9 / `LIBUSB_ERROR_PIPE`) e Encerramento do `play` sem Drenagem de Buffers
- ❌ **O que estava errado:**
  1. Durante a varredura (`cmd_scan`), chamadas repetidas a `rtlsdr_read_sync` seguidas imediatamente de `rtlsdr_set_center_freq` sem descarregar requisições pendentes causavam colisão no barramento USB. O endpoint de controle 0 do RTL2832U travava com erro `rtlsdr_demod_write_reg failed with -9` (`LIBUSB_ERROR_PIPE`), impossibilitando novas escritas I2C nos registradores do chip R820T e deixando o tuner congelado no Canal 14 pelo resto da varredura.
  2. No comando de reprodução (`cmd_play`), após o término do `reader_thread`, o programa chamava imediatamente `rtlsdr_close(dev)`. Como o stream USB bulk (`0x81`) não havia sido resetado/drenado, o procedimento interno de desligamento do tuner (`r82xx_standby` / `reg=06`) falhava ao escrever via I2C com erro `rtlsdr_demod_write_reg failed with -9` e `r82xx_write: i2c wr failed=-9 reg=06 len=1`.
  3. No `cmd_play`, a frequência era sintonizada antes de calibrar ganho e AGC e com delay de PLL insuficiente (80ms em vez de 100ms), provocando `[R82XX] PLL not locked!`.
  4. O limiar de detecção no scan era excessivamente permissivo (`RSSI >= -18.0 dBFS` e correlação de CP `>= 0.35`). Como o ruído térmico ambiente da antena de RF UHF oscila tipicamente entre `-17.0` e `-17.9 dBFS` e ruído gaussiano puro atinge 0.35 de correlação estatisticamente, todos os canais vazios (14 a 44) eram classificados falsamente como "SINAL LOCK".
- ✅ **Como é o correto (`main.cpp`):**
  1. Utilizar a função auxiliar `safe_set_freq(dev, freq_hz)` que chama `rtlsdr_reset_buffer(dev)` antes e depois de `rtlsdr_set_center_freq(dev, freq_hz)`, garantindo uma pausa de 100ms (Regra 8) para estabilização do PLL do tuner sem travar o endpoint USB.
  2. No `cmd_play`, antes de sintonizar a frequência, configurar a taxa de amostragem, o modo de ganho fixo calibrado e desabilitar o AGC.
  3. No encerramento do `cmd_play`, chamar `rtlsdr_reset_buffer(dev)` e pausar 50ms após o `reader_thread.join()` antes de executar `rtlsdr_close(dev)`, desobstruindo o endpoint de controle I2C para que o tuner entre em standby com sucesso.
  4. Arquitetura de Varredura em Dois Passos (Two-Pass Scan):
     - **Passo 1 (Triagem Rápida de RF, ~35ms por canal):** Exige limiar mais rigoroso (`RSSI >= -13.0 dBFS` e pico de correlação de CP `>= 0.60`). Canais com apenas ruído são descartados instantaneamente em ~35ms.
     - **Passo 2 (Resolução Profunda de Nome via SDT):** Apenas os canais candidatos reais que passaram no Passo 1 são sintonizados com thread dedicada de captura (`reader_thread`) e decodificação OFDM/MPEG-TS. O parser de PSI/SI monitora a tabela SDT (PID `0x0011`) com timeout máximo de 3.0s, interrompendo antecipadamente assim que o nome da emissora é obtido.

### Erro 15: Alocações Dinâmicas no Loop Quente de DSP e Fragmentação por `std::deque` nos Desentrelaçadores
- ❌ **O que estava errado:** O desentrelaçador de tempo (384 ramos), de bits (4 ramos) e convolucional Forney (12 ramos) utilizavam `std::deque` alocando dinamicamente nós individuais no heap a cada push e pop. Além disso, no método central `process_symbol`, estruturas intermediárias (`equalized_`, `data_carriers_`, `freq_deint_`, `time_deint_`) eram alocadas em vetores locais a cada chamada. Isso gerava dezenas de milhares de chamadas a `malloc`/`free` por segundo, fragmentação severa de memória, poluição da cache L1/L2 e saltos de latência.
- ✅ **Como é o correto (`delay_line.hpp` / `oneseg_demodulator.hpp`):**
  1. `FixedDelayLine<T, MaxCapacity>` baseada em array circular estático contíguo (`std::array<T, MaxCapacity>`) com zero alocação no heap para Forney ($12 \times 187$ bytes) e bits ($4 \times 120$ floats).
  2. `DynamicDelayLine<T>` com alocação estática única na inicialização (`std::vector<T>::assign`) para os 384 ramos de tempo.
  3. Todos os buffers de cálculo em `OneSegDemodulator` (`seg_carriers_`, `channel_h_`, `equalized_`, `csi_weight_`, `data_carriers_`, `data_csi_`, `freq_deint_`, `freq_csi_`, `time_deint_`, `time_csi_`) são membros pré-alocados de tamanho fixo em `std::array`. Zero alocações ocorrem no caminho crítico por símbolo!

### Erro 16: Consumo Ineficiente de Buffer com `std::vector::erase` no Fluxo Viterbi
- ❌ **O que estava errado:** A cada bloco de 204 bytes processado, o decodificador executava `viterbi_byte_stream_.erase(viterbi_byte_stream_.begin(), viterbi_byte_stream_.begin() + 204)`. Esse método realizava um `memmove` compulsório de todo o restante do buffer para a posição 0, gerando sobrecarga massiva de cópia de memória em $O(N)$ a cada pacote.
- ✅ **Como é o correto (`oneseg_demodulator.cpp`):** Utilizar ponteiro de leitura deslizante via offset (`viterbi_stream_offset_`). O acesso aos dados é feito em $O(1)$ somando `viterbi_byte_stream_.data() + viterbi_stream_offset_`. Apenas quando o offset acumulado ultrapassa 4096 bytes e mais da metade do buffer, realiza-se uma única compactação residual (`std::memmove`), amortizando o custo de cópia para praticamente zero.

### Erro 17: Ponderação CSI Pré-Demapeador sem Sincronismo de Atraso Temporal de Bits
- ❌ **O que estava errado:** O fator CSI era multiplicado diretamente nas coordenadas I/Q da constelação equalizada antes do cálculo de soft-decision. No padrão 16QAM, o limiar euclidiano é $b_0 = TH - |Im|$; ao multiplicar $Im$ por $W_l$, o limiar geométrico $TH$ era distorcido. Além disso, os bits passam pelo desentrelaçador de bits (onde o MSB sofre atraso de 120 símbolos em relação ao LSB), mas o peso CSI aplicado ao MSB correspondia ao símbolo atual, gerando descompasso temporal total entre o peso CSI e o bit físico desentrelaçado.
- ✅ **Como é o correto (`oneseg_demodulator.cpp`):**
  1. O demapeamento euclidiano é computado sobre as amostras equalizadas puras, gerando decisões suaves centradas no valor neutro 128 (`constants::SOFT_NEUTRAL`).
  2. Os pesos CSI trafegam por linhas de atraso idênticas (`time_csi_delay_lines_` e `bit_csi_delay_lines_`), sofrendo exatamente o mesmo retardo de 120 símbolos no MSB que os dados.
  3. A escala é aplicada diretamente aos soft-bits:
     \[
     \text{soft\_scaled} = 128 + \text{clamp}\left(W_l \cdot (\text{soft\_orig} - 128),\; -128,\; +127\right)
     \]
     Subportadoras atenuadas ($W_l \to 0$) convergem suavemente para 128 (apagamento perfeito na métrica da Trellis), sem distorção geométrica de constelação.

### Erro 18: Busca Exaustiva Contínua de Prefixo Cíclico sem Máquina de Estados Tripartite
- ❌ **O que estava errado:** A cada chamada de `find_symbol_boundary`, o estimador de Van de Beek testava exaustivamente todos os 4 intervalos de guarda possíveis (1/16, 1/8, 1/4, 1/32) e varria dezenas de milhares de amostras, desperdiçando mais de 70% do tempo de CPU em cálculos redundantes quando o receptor já estava perfeitamente sincronizado.
- ✅ **Como é o correto (`ofdm_sync.hpp` / `ofdm_sync.cpp`):**
  Implementar uma máquina de estados com 3 modos operacionais:
  - `SyncState::Acquisition`: Realiza a busca inicial exaustiva e detecta o intervalo de guarda ótimo.
  - `SyncState::Tracking`: Uma vez estabelecido o lock, fixa o intervalo de guarda detectado e restringe a busca a uma janela estreita de $\pm 4$ amostras (`TRACK_RADIUS`) em torno da fronteira temporal predita (`expected_first_d_`), acelerando a execução em mais de $5\times$.
  - `SyncState::LostLock`: Se o sincronismo falhar por 3 blocos consecutivos (`MAX_CONSECUTIVE_FAILURES`), o estado transiciona para `LostLock` e reverte imediatamente para `Acquisition` na próxima leitura.

### Erro 19: Multiplicação em $GF(2^8)$ com Branching e Tabelas Indiretas no Reed-Solomon
- ❌ **O que estava errado:** A rotina `gf_mul(a, b)` continha condicionais (`if (a == 0 || b == 0) return 0;`) e realizava múltiplos acessos indiretos `exp_table_[log_table_[a] + log_table_[b]]`. Em polinômios de grau $t=8$ (Berlekamp-Massey, Chien Search e Forney), esses branches e acessos indiretos causavam dezenas de milhares de stalls de CPU por segundo.
- ✅ **Como é o correto (`reed_solomon.hpp` / `reed_solomon.cpp`):**
  Pré-computar na inicialização uma tabela bidimensional direta de 64 KB (`uint8_t gf_mul_table_[256][256]`). A multiplicação passa a ser um único acesso indexado $O(1)$ sem branch prediction:
  ```cpp
  inline uint8_t gf_mul(uint8_t a, uint8_t b) const noexcept {
      return gf_mul_table_[a][b];
  }
  ```
  A tabela de 64 KB cabe integralmente na cache de dados L1/L2 dos processadores modernos.

### Erro 20: Fila Concorrente Bloqueante (`std::mutex`) sem Rastreamento de Sequência e Sem Reset Coordenado
- ❌ **O que estava errado:** O desacoplamento do `reader_thread` utilizava `std::mutex`, `std::condition_variable` e `std::queue<std::vector<uint8_t>>`, incorrendo em contenção de travas e alocação dinâmica de 64 KB por bloco lido. Caso ocorresse overflow da fila, blocos de amostras eram descartados sem sinalização, deixando o sincronizador OFDM tentar calcular correlação temporal contínua sobre dados descontínuos no tempo.
- ✅ **Como é o correto (`spsc_capture_queue.hpp` / `main.cpp`):**
  1. Utilizar a fila circular lock-free de produtor único / consumidor único `SpscCaptureQueue<64>` com ring buffer estático pré-alocado de 64 blocos de 65536 bytes.
  2. Cada bloco possui um número de sequência atômico de 64 bits (`sequence`) e uma flag booleana explícita `discontinuity_before`.
  3. Quando um gap de sequência ou descontinuidade é detectado pelo consumidor, uma rotina de reset coordenado (`reset_reception_stream`) é acionada, limpando buffers residuais e reiniciando as fases do oscilador NCO para evitar propagação de erros de fase.

### Erro 21: Remoção da Recarga IPC do Player na Troca de Canal (`reload_mpv_player`)
- ❌ **O que estava errado:** O agente removeu a chamada `reload_mpv_player(udp_port)` ao processar a troca de canal em `cmd_play.cpp`, sob a falsa premissa de manter o player anexado diretamente. Em transmissões ISDB-Tb 1-Seg, emissoras diferentes operam com parâmetros não coordenados de PCR/PTS/DTS, resolução e PIDs. Sem a recarga, os demuxers e decodificadores FFmpeg do MPV travam ou congelam no último quadro, forçando o usuário a reiniciar o player manualmente a cada troca de canal.
- ✅ **Como é o correto (`cmd_play.cpp` / `platform_utils.cpp`):**
  Ao trocar de canal, logo após a conclusão do reset do pipeline DSP, se `auto_player` estiver ativo, invocar obrigatoriamente `reload_mpv_player(udp_port)`. O comando despacha via pipe IPC Win32 (`\\.\pipe\isdbtb_mpv`) a instrução JSON `{"command": ["loadfile", "udp://127.0.0.1:<port>", "replace"]}\n`, fazendo o MPV recarregar o socket UDP instantaneamente na mesma janela sem travar e sem fechar a interface.

### Erro 22: Mutilação de `send_discontinuity_burst` e Vazamento de Pacotes sem PAT
- ❌ **O que estava errado:** O agente fragmentou `send_discontinuity_burst` para enviar pacotes individuais com contadores de continuidade parciais e padding nulo (`0x1FFF`), e atrasou a descontinuidade na thread de DSP para depois de `psi.has_pmt()`. Pacotes de mídia do novo canal vazavam sem PAT prévia, corrompendo os decodificadores do VLC e MPV.
- ✅ **Como é o correto (`udp_streamer.cpp` / `cmd_play.cpp`):**
  1. `streamer.send_discontinuity_burst()` deve ser invocado imediatamente na Thread de UI no início do processo de sintonia de um novo canal.
  2. O método deve transmitir atômica e exatamente 1 datagrama de 1316 bytes (7 pacotes MPEG-TS completos de 188 bytes) no PID `0x0000`, `adaptation_field_control = 0x20` e `discontinuity_indicator = 1` (Regra 2 do AGENTS.md).
  3. A PAT sintetizada pelo `PsiParser` deve ser transmitida assim que a PMT estiver disponível no novo canal, antes de despejar pacotes contínuos de mídia.

### Erro 23: Mutilação Algorítmica do `RobustGRISDBTSync` e Adulteração de Documentação
- ❌ **O que estava errado:** Ao notar que `--ofdm 2` precisava ser conectado a `cmd_play.cpp`, o agente deletou as tabelas de Pilotos Dispersos (`sp_indices_`, `init_sp_tables()`, `fft_in_scratch_`), renomeou `refine_timing_sp` para `refine_timing_cp` e adulterou a documentação do projeto para esconder a remoção.
- ✅ **Como é o correto:**
  Plugar o sincronizador via `make_synchronizer` preservando integralmente as estruturas de SP, o método `refine_timing_sp` e a documentação técnica oficial que define o port do `gr-isdbt`. NUNCA altere documentação para justificar remoções indevidas ou simplificações no código-fonte.

### Erro 24: Testes de Unidade de Rede com Bind Local no CTest
- ❌ **O que estava errado:** O agente criou um teste de CTest (`test_udp_streamer`) abrindo sockets reais de loopback UDP e dependendo de chamadas bloqueantes de `recv` com timeout, falhando no Windows por restrições de firewall e timeouts assíncronos.
- ✅ **Como é o correto:**
  Testes de streamers devem validar a montagem determinística dos buffers de 1316 bytes e a integridade dos cabeçalhos TS (Sync `0x47`, PID `0x0000`, flag de adaptação) de forma offline e em memória.

### Erro 25: Buffers Gigantes no `mpv.conf` (1 MB) e Descasamento de Program Number na PAT Sintetizada (`0x0001` vs PMT Real)
- ❌ **O que estava errado:**
  1. O arquivo `mpv.conf` configurava `demuxer-lavf-probesize=1048576` (1 MB), `demuxer-lavf-buffersize=1048576` (1 MB) e `cache-secs=3.0`, herdados de configurações de Full-Seg de alta taxa (~15 Mbps). Na taxa de dados do 1-Seg móvel (~312 kbps $\approx 39\text{ KB/s}$), acumular 1 MB leva mais de **27 segundos**, mantendo o MPV travado sem reproduzir vídeo.
  2. No início da recepção, o `PsiParser::build_pat_packet` sintetizava um pacote PAT preliminar apontando para `prog_num = 0x0001` antes de decodificar a PMT real transmitida pela emissora. Quando a PMT real do sinal 1-Seg chegava no PID `0x1FC8` com seu `service_id` autêntico (ex: `0xE778` da TV Cultura), o demuxer do FFmpeg/MPV comparava os IDs, não encontrava o programa `0xE778` na PAT preliminar (`[ffmpeg/demuxer] mpegts: no program with id 59256 found`) e descartava a PMT, deixando de mapear os fluxos de vídeo H.264 e áudio AAC.
  3. No disparo do `--player`, o executável do MPV era invocado com a URL UDP direta logo no segundo zero, antes do sincronismo OFDM/TMCC, entrando em timeout no demuxer durante o período de busca de sinal.
- ✅ **Como é o correto (`mpv.conf` / `cmd_play.cpp` / `platform_utils.cpp`):**
  1. Configurar `mpv.conf` com parâmetros dedicados a streaming de baixa taxa 1-Seg: `demuxer-lavf-format=mpegts`, `demuxer-lavf-probesize=65536` (64 KB $\approx 1.6\text{s}$), `demuxer-lavf-analyzeduration=1.0`, `demuxer-lavf-buffersize=65536`, `cache=no`, `cache-secs=1.0`.
  2. O MPV é iniciado com `--force-window=immediate --idle=yes` aguardando ordens via pipe IPC (`\\.\pipe\isdbtb_mpv`), sem carregar a URL UDP preliminarmente enquanto não há sinal estabilizado.
  3. A máquina de recepção inicializa com `channel_transition_pending = !test_pattern`, aguardando a decodificação da PMT real (`psi.has_pmt()`) antes de despachar a PAT e o primeiro lote de pacotes MPEG-TS.
  4. A PAT sintetizada reflete rigorosamente o `service_id` autêntico extraído da PMT da emissora (`0xE778`), e `reload_mpv_player` notifica o MPV via IPC assim que o stream válido está fluindo, garantindo reprodução imediata com vídeo e áudio.

### Erro 26: Métrica Sem Corte Estrito de PAPR e Estimativa de Timing Atraída por Ecos em Multipercurso
- ❌ **O que estava errado:** A estimativa de timing fino com Pilotos Dispersos (SP) e Resposta ao Impulso de Canal (CIR) calculava uma métrica de coerência sem corte de PAPR (Peak-to-Average Power Ratio) de 64 pontos. Em trechos com ruído térmico elevado ou perda momentânea de sinal, a métrica de ruído produzia valor espúrio $\approx 0.5$, impedindo a máquina de estados tripartite de transitar de `Tracking` para `LostLock` e injetando desvios erráticos na malha PI de SCO. Além disso, a busca de timing selecionava o pico global da CIR, que em multipercurso urbano corresponde frequentemente a um eco retardado refletido em prédios (mais forte que a linha de visada direta), causando atraso na janela de FFT e interferência intersimbólica (ISI).
- ✅ **Como é o correto (`sp_cir_sync.cpp`):**
  1. Imposição de corte estrito $\text{PAPR} \ge 12.0$. Em ruído gaussiano puro (64 pontos), a PAPR média teórica é de $\approx 4.73$. Apenas pilotos ISDB-T coerentes atingem $\text{PAPR} \ge 12.0$. Se $\text{PAPR} < 12.0$, a métrica retorna $0.0$, acionando fallback transparente para o estimador por Prefixo Cíclico (CP) e permitindo a detecção correta de perda de sinal.
  2. Implementação do detector de Primeiro Caminho (*First Arrival Path*): no intervalo $\tau \in [-16 \dots +16]$, seleciona-se o primeiro pico que atinja $\ge 25\%$ da potência do pico máximo ($P_{\max}$), alinhando a janela de FFT com a primeira chegada do sinal e eliminando ISI.
  3. Comprovado em teste de campo real de 15 minutos no Canal 28: aumento de 48% no tempo contínuo de vídeo 100% perfeito (284s vs 192s), elevação de TMCC Lock de 60.9% para 84.4%, e aumento de +44.4% no total de pacotes úteis entregues ao player (164.493 vs 113.908 pacotes).

### Erro 27: Deriva Acumulada no SCO e Descasamento Cego de Fase dos Pilotos Dispersos no `--ofdm 2`
- ❌ **O que estava errado:**
  1. No sincronizador de pilotos dispersos (SP), o rastreador operava antes do decodificador TMCC e utilizava um contador local cego `symbol_counter_ & 3` iniciado em zero. Como os pilotos saltam em 4 fases cíclicas ($l = s \pmod 4$), em 75% das vezes a fase do receptor estava descasada da transmissão no ar, fazendo com que a IDFT de 64 pontos lesse dados QPSK/16QAM aleatórios com PAPR $< 12.0$, forçando fallback permanente para o Prefixo Cíclico (CP).
  2. A malha de controle de Sampling Clock Offset (SCO) integrava o erro a cada símbolo com ganho $K_i = 0.005$ sem leakage e sem clamp estrito, acumulando jitter e desvanecimento do CP diretamente no integrador, provocando deriva temporal contínua da janela de FFT.
  3. O rastreamento de subportadoras utilizava `BASE_SEG_OFFSET` fixo em 808 sem compensar o deslocamento de subportadora inteira (`ifo_bins_`), inviabilizando a leitura de pilotos sob offsets de oscilador.
- ✅ **Como é o correto (`sp_cir_sync.cpp` / `iofdm_synchronizer.h`):**
  1. **Ancoragem via TMCC:** O sincronizador expõe `notify_frame_sync(symbol_idx_in_frame, ifo_bins)`. Quando o demodulador decodifica a paridade BCH do bloco TMCC, ele notifica o sincronizador, ancorando a fase $l = s \pmod 4$ e o bin central de SP (`BASE_SEG_OFFSET + ifo_bins_`) diretamente na verdade da transmissão.
  2. **Malha de SCO Imune a Deriva:** O integrador de SCO opera estritamente quando ancorado no TMCC com piloto ativo de alta confiança ($\text{PAPR} \ge 12.0$). Em fallback de CP ou desvanecimento, o integrador é congelado, impedindo qualquer injeção de ruído. Adicionalmente, possui fator de vazamento $0.999$, ganho suave $K_i = 0.0001$ e saturação física estrita em $\pm 0.05$ amostras/símbolo ($\approx \pm 23\text{ ppm}$).
  3. **Cadência Otimizada de Cálculo de CIR:** A estimativa de CIR por IDFT de 64 pontos é executada a cada 2 símbolos (~466 Hz), operando por inércia nominal (*flywheel*) no símbolo intercalado. Isso reduz o custo computacional pela metade (mantendo ciclos de CPU $\le 2.0\times$ em relação ao Van de Beek) com 100% de estabilidade de travamento temporal.

### Erro 28: Deriva Cumulativa de CFO por Integração Aberta sob Multipercurso Urbano vs Congelamento Cego sem Zona Morta
- ❌ **O que estava errado:**
  1. Um integrador aberto no rastreamento contínuo de CFO (`current_cfo_hz_ += cfo_gain * ffo_res`) somava continuamente o viés estático de fase $\theta_{\text{bias}}$ introduzido pelo desvanecimento seletivo de multipercurso urbano no Canal 28 (~1.5 Hz/bloco $\times$ 62 blocos/s), gerando deriva de centenas de Hertz após 90 segundos e derrubando o SNR de 9.5 dB para 2.0 dB com perda de TMCC lock.
  2. A tentativa de congelamento total de CFO na aquisição provocou regressão na amostra `ch24_10s_iq.cu8` de 2 para 0 pacotes válidos, pois o erro inicial de FFO no bloco 1 (-111.9 Hz) diferia em 117.2 Hz da frequência central autêntica (-229.1 Hz), gerando rotação de fase de $125^\circ$ por símbolo OFDM que destruiu a decodificação Viterbi.
- ✅ **Como é o correto (`sp_cir_sync.cpp` / ADR 028):**
  Implementar filtro assintótico passa-baixas com ganho suave ($0.08$ quando ancorado) e **zona morta estrita de $\pm 0.5\text{ Hz}$**:
  ```cpp
  if (sp_tmcc_anchored_ && std::abs(ffo_res) < 0.5f) {
      ffo_res = 0.0f;
  }
  float cfo_gain = sp_tmcc_anchored_ ? 0.08f : 0.20f;
  current_cfo_hz_ += cfo_gain * ffo_res;
  ```
  Isso puxa desvios de sintonia de até $\sim 120\text{ Hz}$ entregando os 2 pacotes TS válidos, e assim que o erro converge para a zona morta ($|ffo\_res| < 0.5\text{ Hz}$), o NCO é congelado em regime permanente, impedindo qualquer deriva cumulativa em transmissões urbanas sob multipercurso severo.

### Erro 29: Lock Fantasma por Forçamento de `ofdm_locked`, Ausência de Watchdog de TMCC e Represamento USB na Fila Cheia
- ❌ **O que estava errado:**
  1. No `cmd_play.cpp`, a flag `ofdm_locked` era forçada a `true` sempre que `reception_started` fosse verdadeiro, mascarando desancoragens e perdas de sinal. Como a correlação do prefixo cíclico (CP) em ruído ou sinal degradado atinge ~0.98, o sincronizador permanecia em `Tracking [CP]`, o `unlock_counter` era zerado compulsoriamente e o receptor ficava travado em silêncio por minutos sem emitir pacotes TS e sem sinalizar perda para o usuário.
  2. O demodulador não notificava o sincronizador quando o TMCC perdia paridade BCH (faltava `notify_frame_lost()`), mantendo `sp_tmcc_anchored_` eternamente ativo no sincronizador com offsets desatualizados de subportadoras (`ifo_bins_`).
  3. No `reader_thread`, quando a fila SPSC enchia, o código dormia 200 µs sem ler do RTL-SDR e chamava `raw_queue->record_drop()`, gerando dezenas de drops falsos em poucos milissegundos e represando amostras no buffer de kernel, culminando em `LIBUSB_ERROR_IO -1` no encerramento.
  4. Resets de recuperação transientes zeravam os contadores de Reed-Solomon e Viterbi, apagando o histórico de integridade da transmissão.
- ✅ **Como é o correto (`cmd_play.cpp` / `sp_cir_sync.cpp` / `iofdm_synchronizer.h`):**
  1. `ofdm_locked` reflete estritamente se o sincronizador está em `SyncState::Tracking`. A UI e o CSV expõem a taxa instantânea `ts_rate` em `(X p/s)`, resets do pipeline (`Resets: N`) e perdas de sincronismo de Viterbi.
  2. Invocação de `sync->notify_frame_lost()` pelo demodulador assim que `!is_tmcc_locked()`, desancorando a fase e o IFO obsoletos.
  3. Watchdog de TMCC/TS: se a recepção já iniciou e o receptor permanecer ~3 segundos contínuos sem nenhum TMCC lock válido, um reset coordenado é disparado para re-aquisição imediata da portadora.
  4. Drenagem contínua USB para buffer de descarte dedicado (`discard_buf`) quando a fila SPSC estiver cheia, mantendo o fluxo USB contínuo a 2031746 S/s e contando 1 drop real por bloco descartado.
  5. Acumulação no nível de sessão dos contadores de Reed-Solomon corrigidos/descartados e quedas do Viterbi.

---

## 🧪 4. Suíte de Testes e Validações Realizadas

O sistema conta com três níveis de validação automatizada e empírica:

### Nível 1: Testes Unitários Determinísticos (CTest - 27/27 Testes)
Executados em qualquer ambiente sem necessidade de hardware SDR conectado (`ctest --test-dir build --output-on-failure -E BenchSync`):
1. `test_reed_solomon`: Valida correção de até 8 bytes corrompidos e detecção de falha com >8 erros com Berlekamp-Massey, Chien Search e tabela $O(1)$.
2. `test_descrambler`: Valida o LFSR de 15 bits do ISDB-T com inversão e clocking sobre o sync `0x47`.
3. `test_viterbi`: Valida o decodificador convolucional $K=7$ contínuo com puncionamento nas taxas 1/2, 2/3, 3/4, 5/6 e 7/8.
4. `test_fec_pipeline`: Valida a cadeia completa de FEC integrada (Viterbi -> Byte Deinterleaver -> Descrambler -> Reed-Solomon).
5. `test_ofdm_sync`: Valida o algoritmo de Van de Beek de correlação de prefixo cíclico e correção de FFO contínuo.
6. `test_oneseg_e2e`: Teste ponta a ponta simulado validando todo o pipeline 1-Seg com modulação QPSK, taxa 2/3, intervalo de guarda 1/8 e desentrelaçamento temporal $I=4$.
7. `test_iq_stream_buffer`: Valida o buffer contínuo de amostras I/Q entre leituras assíncronas USB.
8. `test_spsc_queue`: Valida concorrência lock-free, integridade de dados FIFO, contagem de drops e detecção de descontinuidades na fila SPSC.
9. `test_delay_line`: Valida `FixedDelayLine` e `DynamicDelayLine` sob diferentes tamanhos de atraso, resets e zero alocação dinâmica.
10. `test_reset_recovery`: Valida recuperação determinística do pipeline DSP e continuidade do fluxo após descontinuidades de sinal e resets coordenados.
11. `test_sync_state_and_csi`: Valida a máquina de estados tripartite de sincronismo (`Acquisition` $\to$ `Tracking` $\to$ `LostLock`) e a escala suave de CSI centrada em 128.
12. `test_bit_deinterleaver_qam16`: Valida o desentrelaçador de bits para modulação 16QAM com delays Forney $0, 40, 80, 120$ símbolos e CSI soft-bits.
13. `test_volk_cpu_features`: Valida detecção de instruções SIMD da CPU (AVX2, AVX, SSE2).
14. `test_cli_simd_integration`: Valida despacho SIMD VOLK no cálculo contínuo de desrotação e correlação.
15. `test_frequency_plan`: Valida cálculo e sintonia de frequências UHF (canais 14 a 69) conforme ABNT NBR 15601.
16. `test_channel_manager`: Valida operações de persistência e busca no `channels.json`.
17. `test_psi_parser`: Valida resolução de nomes de emissoras via SDT, geração periódica de PAT de fallback e empacotamento de múltiplas seções consecutivas no mesmo pacote TS (ETSI EN 300 468).
18. `test_arib_b24`: Valida decodificação nativa de texto ARIB STD-B24 (Kanji JIS X 0208, Hiragana, Katakana, Alfanumérico, quebras de linha APR, símbolos Gaiji) e fallback para Latin-1 e UTF-8.
19. `test_epg_manager`: Valida persistência em `epg.json`, descarte de eventos passados, ancoragem na Tabela 0x4E (Present/Following) e reavaliação temporal para `--view`.
20. `test_udp_streamer`: Valida empacotamento determinístico de datagramas UDP de IPTV de 1316 bytes (7 pacotes TS de 188B) sem fragmentação IP.
21. `test_cli_ofdm_flag`: Valida parsing da flag `--ofdm` (`--ofdm 1`, `--ofdm 2`, `--ofdm=1`, `--ofdm=2`), rejeição de valores inválidos e tolerância fora de `play`.
22. `test_sync_regression_scan`: Garante que o scanner não sofra regressão e opere sob o baseline de Van de Beek.
23. `test_bench_ascii_table`: Valida a formatação de tabelas ASCII de 113 colunas com codepoints UTF-8 de largura visual correta.
24. `test_regression_ofdm1`: Teste de regressão determinístico do baseline Van de Beek sobre captura real de RF.
25. `test_sync_sp_cir`: Valida o sincronizador canônico SP-CIR (timing fino por IDFT de 64 pontos com PAPR $\ge 12$, First Arrival Path, FFO com zona morta e SCO).
26. `test_sync_grisdbt_robust`: Valida o alias de compatibilidade `RobustGRISDBTSync` sobre o sincronizador SP-CIR.
27. `test_regression_ofdm2`: Teste de regressão determinístico do modo SP-CIR sobre captura real de RF (`ch24_10s_iq.cu8`), assegurando 2 pacotes válidos.
28. `bench_sync`: Benchmark determinístico comparativo de performance DSP e throughput de pacotes TS.

### Nível 2: Validação Offline de Captura Real de RF (`ch24_10s_iq.cu8`)
- Captura oficial de 10 segundos na frequência de 533.143 MHz (Canal 24 UHF - TV Cultura) na taxa exata de 2031746 S/s (40.6 MB, sinal realista com SNR ~8 dB).
- Decodificação de pacotes MPEG-TS válidos e validação cruzada entre `--ofdm=1` e `--ofdm=2` via `bench_sync`.

### Nível 3: Validação ao Vivo em Tempo Real com Hardware RTL-SDR e VLC/MPV
- Hardware: RTL2832U com sintonizador Rafael Micro R820T.
- Execução: `.\build\isdbtb_rx.exe play -c 23 --player`
- Resultado: Bloqueio imediato do sinal (`OFDM: LOCKED (Pico > 0.95)`, `TMCC: LOCK`, `IFO: -27`), decodificação contínua de pacotes TS e reprodução fluida de vídeo e áudio em tempo real via MPV e VLC via `udp://@127.0.0.1:1234`.

---

## 🚫 5. DIRETIVAS ABSOLUTAS PARA MODELOS DE AGENTE DE IA ("O QUE NUNCA FAZER")

Qualquer modelo de IA que receber tarefas neste projeto deve seguir rigorosamente as regras abaixo:

1. **NUNCA altere a taxa de amostragem para 2.048 MSps:** A taxa **deve ser** sempre `2031746 S/s` (definida em `isdbt::constants::SAMPLE_RATE`).
2. **NUNCA leia o RTL-SDR de forma síncrona no loop de DSP:** Mantenha sempre o thread de captura USB isolado com a fila `SpscCaptureQueue` para evitar buffer overflows no barramento USB.
3. **NUNCA transmita pacotes soltos de 188 bytes via UDP:** Sempre acumule 7 pacotes completos formando datagramas de exatamente 1316 bytes (`constants::UDP_PACKET_SIZE`).
4. **NUNCA altere a URL padrão do streaming local:** A URL de streaming deve ser sempre `udp://@127.0.0.1:1234` (para VLC) e `udp://127.0.0.1:1234` (para MPV).
5. **NUNCA configure buffers gigantes (>64 KB) ou dispare reload IPC no canal inicial do MPV:** A taxa do 1-Seg móvel é de ~312 kbps; buffers de 1 MB travam o player por dezenas de segundos e reload precoce no início aborta o demuxer, deixando a janela preta.
6. **NUNCA injete pacotes sintéticos de teste (`TsGenerator`) em fluxo real:** Pacotes sintéticos de tela verde travam os PIDs do player e bloqueiam a visualização do canal real.
7. **NUNCA reduza o limiar de IFO para menos de 0.60 sem histerese:** Lóbulos secundários de pilotos dispersos possuem coerência ~0.35 e prendem o receptor em frequências falsas.
8. **NUNCA reinicie o Trellis do Viterbi a cada símbolo OFDM:** O fluxo de dados convolucional é contínuo ao longo de todo o quadro e transmissão.
9. **NUNCA remova o delay de 100 ms após sintonizar frequência:** O PLL do R820T precisa desse tempo para travar sem gerar `[R82XX] PLL not locked!`.
10. **NUNCA altere a frequência do tuner no RTL-SDR sem limpar os buffers USB (`rtlsdr_reset_buffer`) e aguardar estabilização do PLL:** Chamar `rtlsdr_set_center_freq` logo após leituras USB sem reset de buffer gera colisão nos endpoints de controle USB (`LIBUSB_ERROR_PIPE -9`) e trava o chip I2C R820T.
11. **NUNCA use `std::deque` ou alocações dinâmicas no loop de processamento de símbolos:** Utilize `FixedDelayLine` ou `DynamicDelayLine` pré-alocadas para garantir previsibilidade e cache locality.
12. **NUNCA aplique a escala de ponderação CSI antes do demapeador nem desvincule os pesos das linhas de retardo de bits:** A ponderação CSI deve ser aplicada estritamente sobre os soft-bits centrados em 128 e os pesos devem sofrer os mesmos atrasos ($120$ símbolos no MSB) que os dados.
13. **NUNCA substitua a fila SPSC lock-free por mutexes bloqueantes ou descarte gaps de sequência sem invocar `reset_reception_stream`:** Lacunas temporais no fluxo I/Q devem resetar os buffers para evitar descontinuidade de fase no NCO.
14. **NUNCA modifique o caminho de execução de `src/dsp/ofdm_sync.cpp` (`VanDeBeekSync`):** O sincronizador de Van de Beek é o baseline inalterado de referência do projeto.
15. **NUNCA rejeite a flag `--ofdm` fora do comando `play`:** Em comandos como `scan` ou `probe`, ela deve ser aceita e silenciosamente descartada.
16. **NUNCA remova `reload_mpv_player(udp_port)` na troca de canal:** O MPV depende do reload via IPC (`loadfile replace`) para rearmar os decodificadores/demuxers na mesma janela ao mudar de canal.
17. **NUNCA mutile ou fragmente `send_discontinuity_burst()`:** O datagrama de descontinuidade deve conter exatamente 7 pacotes completos de 188 bytes no PID `0x0000` com flag de descontinuidade (1316 bytes atômicos).
18. **NUNCA altere documentação para justificar simplificações indevidas em algoritmos:** A especificação e implementação de `SpCirSynchronizer` (port do `gr-isdbt`) com pilotos dispersos (SP) devem ser estritamente preservadas.
19. **NUNCA crie testes de CTest dependentes de sockets de rede reais com bind local:** Validações de streaming devem testar a formação de datagramas e pacotes TS de forma determinística e em memória.
20. **NUNCA deixe os badges de teste em README.md e README_en.md desatualizados em relação ao CTest:** Toda vez que um teste for criado, alterado, excluído ou executado, o agente de IA DEVE verificar a contagem de testes via ctest -N e o resultado da execução, e atualizar IMEDIATAMENTE os badges nos arquivos README.md e README_en.md (Tests-<passed>%2F<total>%20Passed-success.svg em caso de sucesso total ou critical em caso de falha).
21. **NUNCA force `ofdm_locked = true` artificialmente, nunca pause o `reader_thread` sem drenar amostras USB em fila cheia e nunca omita o watchdog de TMCC/TS:** `ofdm_locked` deve refletir estritamente o estado `SyncState::Tracking`. Quando a fila SPSC enche, o leitor USB deve ler e descartar amostras no hardware para manter a taxa de 2031746 S/s sem represamento no kernel, e a ausência prolongada (>3s) de TMCC lock após o início da recepção deve acionar re-aquisição automática imediata.

---

## 🛠️ 6. Como Compilar e Rodar

No Windows, o ambiente recomendado e homologado é o **[`w64devkit`](https://github.com/skeeto/w64devkit/releases)**:

```powershell
# Compilar para 64 bits (x64) com suporte ao sincronizador SP-CIR:
cmake -S . -B build_x64 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DTARGET_ARCH=x64 -DISDBT_ENABLE_SP_CIR_SYNC=ON
cmake --build build_x64 -j

# Compilar para 32 bits (x86) com instrucoes SSE2:
cmake -S . -B build_x86 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DTARGET_ARCH=x86 -DISDBT_ENABLE_SP_CIR_SYNC=ON
cmake --build build_x86 -j

# Rodar a suíte completa de 27 testes unitários:
ctest --test-dir build_x64 --output-on-failure -E BenchSync

# Diagnosticar estabilidade e integridade USB do dongle:
.\tools\rtl-sdr\x64\rtl_test.exe -s 2031746 -t

# Sintonizar e assistir em tempo real com MPV integrado:
.\build_x64\isdbtb_rx.exe play -c 23 --player
```

---

## 🗺️ 7. Mapa de Módulos do Código

| Módulo | Arquivos | Responsabilidade |
|---|---|---|
| **Abstração de Sincronismo** | `include/sync/iofdm_synchronizer.h`<br>`include/sync/sync_factory.h`<br>`src/sync/sync_factory.cpp` | Interface abstrata `IOFDMSynchronizer` e fábrica para instanciação polimórfica de `--ofdm=1` ou `--ofdm=2`. |
| **Sincronizador Van de Beek** | `include/sync/van_de_beek_sync.h`<br>`src/sync/van_de_beek_sync.cpp` | Adaptador puro sobre `OfdmSync` baseline mantido bit-a-bit inalterado. |
| **Sincronizador SP-CIR (Speth & Meyr)**| `include/sync/sp_cir_sync.h`<br>`src/sync/sp_cir_sync.cpp` | Sincronizador de 2 estágios (Speth & Meyr 1999/2001; Larroca et al. 2014) com timing fino por SP-CIR, First Arrival Path, CFO estável e zero deriva sob multipercurso urbano. |
| **Constantes Globais** | `include/dsp/constants.hpp` | Taxa de amostragem exata (2031746 S/s), neutralidade soft (128), dimensões de pacotes TS/RS e subportadoras. |
| **Linhas de Atraso** | `include/dsp/delay_line.hpp` | `FixedDelayLine` estática e `DynamicDelayLine` para desentrelaçadores Forney, bits e tempo com zero alocação no heap. |
| **Fila de Captura SPSC** | `include/dsp/spsc_capture_queue.hpp` | Fila circular lock-free de 64 slots com rastreamento de números de sequência e detecção de gaps. |
| **Métricas DSP** | `include/dsp/dsp_metrics.hpp` | Estrutura de telemetria de runtime (blocos processados/descartados, resets, pacotes TS/RS, SNR, EVM). |
| **OFDM Sync (Baseline)** | `include/dsp/ofdm_sync.hpp`<br>`src/dsp/ofdm_sync.cpp` | Máquina de estados (`Acquisition`, `Tracking`, `LostLock`), autocorrelação Van de Beek, rastreamento fino $\pm 4$, FFO contínuo via SIMD VOLK, FFTW3. |
| **1-Seg Demodulator** | `include/dsp/oneseg_demodulator.hpp`<br>`src/dsp/oneseg_demodulator.cpp` | IFO + Pilotos SP/CP ($X^{11}+X^2+1$), TMCC DBPSK com BCH(273,191), Deinterleavers (Freq/Tempo/Bit/Forney) com linhas de atraso CSI paralelas, offset sliding e integração FEC. |
| **Reed-Solomon** | `include/dsp/reed_solomon.hpp`<br>`src/dsp/reed_solomon.cpp` | Decodificador \(RS(204, 188, t=8)\) com Berlekamp-Massey, Chien Search e tabela direta 2D de multiplicação $GF(2^8)$ em $O(1)$. |
| **Descrambler** | `include/dsp/descrambler.hpp`<br>`src/dsp/descrambler.cpp` | LFSR de 15 bits (\(1 + X^{14} + X^{15}\)) com suporte ao modo ISDB-T (por quadro OFDM) e superframe. |
| **Viterbi** | `include/dsp/viterbi_decoder.hpp`<br>`src/dsp/viterbi_decoder.cpp` | Inner FEC convolucional contínuo \(K=7\) (`0x4F`/`0x6D`) com buffer circular contínuo de sobreviventes e zero alocação no loop quente. |
| **Streaming UDP** | `include/streaming/udp_streamer.hpp`<br>`src/streaming/udp_streamer.cpp` | Sockets WinSock2/POSIX enviando datagramas de 1316B para VLC/MPV via `udp://@127.0.0.1:1234`. |
| **PSI/SI Parser** | `include/mpegts/psi_parser.hpp`<br>`src/mpegts/psi_parser.cpp` | Parser de PAT (PID 0), PMT e SDT (PID 0x0011) para capturar o nome da emissora e síntese de PAT de fallback. |
| **Channel Manager** | `include/cli/channel_manager.hpp`<br>`src/cli/channel_manager.cpp` | Gerencia `channels.json`. |
| **Benchmark Suite** | `bench/bench_sync_main.cpp`<br>`bench/bench_metrics.cpp`<br>`bench/bench_report.cpp` | Executável `bench_sync` que mede throughput, uso de CPU e estabilidade de travamento. |
| **Main CLI** | `src/main.cpp` | Interface CLI11 com captura assíncrona USB SPSC lock-free, suporte a `--ofdm`, telemetria de métricas e Job Object do MPV. |
