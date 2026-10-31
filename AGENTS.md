# 🤖 Guia de Orientação para Multi-Agentes de IA (AGENTS.md)

Este documento foi elaborado especificamente para orientar **agentes de Inteligência Artificial** (e desenvolvedores humanos) que forem continuar, manter ou expandir o desenvolvimento deste decodificador universal ISDB-T/ISDB-Tb.

---

## 🎯 1. Visão Geral e Escopo do Projeto

O objetivo deste projeto é fornecer uma aplicação de linha de comando (CLI) nativa, de alta performance, sem dependências do framework GNU Radio, capaz de:
1. Descobrir e controlar dongles **RTL-SDR** (RTL2832U com tuners R820T/R828D/E4000).
2. Sintonizar e decodificar sinais de TV Digital no padrão **ISDB-T** (Japão, Filipinas, etc.) e **ISDB-Tb / SBTVD** (Brasil e América Latina).
3. Realizar o processamento completo de Camada Física (DSP) e Camada de Aplicação (PSI/SI, gerenciamento de canais e streaming UDP para VLC).

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

### 2. Pacotes UDP para VLC (\(7 \times 188 = 1316\text{ bytes}\)) e URL de Streaming
- Cada datagrama UDP enviado para `udp://@127.0.0.1:1234` **DEVE** conter exatamente 7 pacotes MPEG-TS completos de 188 bytes (\(7 \times 188 = 1316\text{ bytes}\)).
- **Motivo:** 1316 bytes é o tamanho padrão da indústria de IPTV que cabe no MTU de 1500 bytes da Ethernet sem causar fragmentação de pacotes no kernel IP.
- **Regra do Player:** A URL padrão de escuta no VLC é:
  ```text
  udp://@127.0.0.1:1234
  ```
- **Regra:** Nunca envie pacotes soltos de 188 bytes via UDP individualmente (causa sobrecarga severa de interrupções de rede e travamentos no VLC).

### 3. Modo Binário no Windows (`_setmode`)
- No Windows, a saída padrão (`stdout`) e arquivos binários abrem em modo texto por padrão (onde bytes `0x0A` (`\n`) viram `0x0D 0x0A` (`\r\n`)).
- **Regra:** Se for redirecionar pacotes TS para pipes (`stdout`), `Platform::set_binary_stdout()` (`_setmode(_fileno(stdout), _O_BINARY)`) deve permanecer ativo.

### 4. Persistência de Canais em JSON
- Windows: `%APPDATA%\isdbtb_rx\channels.json`
- Linux: `~/.config/isdbtb_rx/channels.json`
- **Regra:** Use sempre a classe `ChannelManager` e `std::filesystem` para manipular caminhos.

---

## 🛡️ 3. Histórico de Erros Corrigidos e Regras de Conformidade com `git-artes/gr-isdbt` ("NÃO REPITA ESTES ERROS")

Durante a auditoria e desenvolvimento do decodificador contra a implementação de referência [`git-artes/gr-isdbt`](https://github.com/git-artes/gr-isdbt) e em testes reais com sinais do ar, 12 erros críticos foram identificados, corrigidos e documentados. **Nenhum agente futuro deve reintroduzir qualquer um dos padrões incorretos abaixo:**

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

### Erro 14: Colisão de Endpoints USB (Erro -9 / `LIBUSB_ERROR_PIPE`) e Falsos Positivos na Varredura (`scan`)
- ❌ **O que estava errado:**
  1. Durante a varredura (`cmd_scan`), chamadas repetidas a `rtlsdr_read_sync` seguidas imediatamente de `rtlsdr_set_center_freq` sem descarregar requisições pendentes causavam colisão no barramento USB. O endpoint de controle 0 do RTL2832U travava com erro `rtlsdr_demod_write_reg failed with -9` (`LIBUSB_ERROR_PIPE`), impossibilitando novas escritas I2C nos registradores do chip R820T e deixando o tuner congelado no Canal 14 pelo resto da varredura.
  2. O limiar de detecção era excessivamente permissivo (`RSSI >= -18.0 dBFS` e correlação de CP `>= 0.35`). Como o ruído térmico ambiente da antena de RF UHF oscila tipicamente entre `-17.0` e `-17.9 dBFS` e ruído gaussiano puro atinge 0.35 de correlação estatisticamente, todos os canais vazios (14 a 44) eram classificados falsamente como "SINAL LOCK".
- ✅ **Como é o correto (`main.cpp`):**
  1. Utilizar a função auxiliar `safe_set_freq(dev, freq_hz)` que chama `rtlsdr_reset_buffer(dev)` antes e depois de `rtlsdr_set_center_freq(dev, freq_hz)`, garantindo ainda uma pausa de 80ms para estabilização do PLL do tuner sem travar o endpoint USB.
  2. Arquitetura de Varredura em Dois Passos (Two-Pass Scan):
     - **Passo 1 (Triagem Rápida de RF, ~35ms por canal):** Exige limiar mais rigoroso (`RSSI >= -13.0 dBFS` e pico de correlação de CP `>= 0.60`). Canais com apenas ruído são descartados instantaneamente em ~35ms.
     - **Passo 2 (Resolução Profunda de Nome via SDT):** Apenas os canais candidatos reais que passaram no Passo 1 são sintonizados com thread dedicada de captura (`reader_thread`) e decodificação OFDM/MPEG-TS. O parser de PSI/SI monitora a tabela SDT (PID `0x0011`) com timeout máximo de 3.0s, interrompendo antecipadamente assim que o nome da emissora é obtido.

---

## 🧪 4. Suíte de Testes e Validações Realizadas

O sistema conta com três níveis de validação automatizada e empírica:

### Nível 1: Testes Unitários Determinísticos (CTest)
Executados em qualquer ambiente sem necessidade de hardware SDR conectado:
1. `test_reed_solomon`: Valida correção de até 8 bytes corrompidos e detecção de falha com >8 erros com Berlekamp-Massey e Forney.
2. `test_descrambler`: Valida o LFSR de 15 bits do ISDB-T com inversão e clocking sobre o sync `0x47`.
3. `test_viterbi`: Valida o decodificador convolucional $K=7$ contínuo com puncionamento nas taxas 1/2, 2/3, 3/4, 5/6 e 7/8.
4. `test_fec_pipeline`: Valida a cadeia completa de FEC integrada (Viterbi -> Byte Deinterleaver -> Descrambler -> Reed-Solomon).
5. `test_ofdm_sync`: Valida o algoritmo de Van de Beek de correlação de prefixo cíclico e correção de FFO contínuo.
6. `test_oneseg_e2e`: Teste ponta a ponta simulado validando todo o pipeline 1-Seg com modulação QPSK, taxa 2/3, intervalo de guarda 1/8 e desentrelaçamento temporal $I=4$.

### Nível 2: Validação Offline de Captura Real de RF (`live_ch23.raw`)
- Captura de 8 MB de RF real obtida diretamente do ar na frequência 527.143 MHz (Canal 23 UHF de São Paulo).
- Decodificou **299 pacotes MPEG-TS válidos**, travou `TMCC=1`, `IFO=-26` e extraiu os descritores da emissora `BAND HD`.

### Nível 3: Validação ao Vivo em Tempo Real com Hardware RTL-SDR e VLC
- Hardware: RTL2832U com sintonizador Rafael Micro R820T.
- Execução: `.\build\isdbtb_rx.exe play -c 23`
- Resultado: Bloqueio imediato do sinal (`OFDM: LOCKED (Pico > 0.95)`, `TMCC: LOCK`, `IFO: -27`), decodificação contínua de pacotes TS e reprodução fluida de vídeo e áudio em tempo real no VLC via `udp://@127.0.0.1:1234`.

---

## 🚫 5. DIRETIVAS ABSOLUTAS PARA MODELOS DE AGENTE DE IA ("O QUE NUNCA FAZER")

Qualquer modelo de IA que receber tarefas neste projeto deve seguir rigorosamente as regras abaixo:

1. **NUNCA altere a taxa de amostragem para 2.048 MSps:** A taxa **deve ser** sempre `2031746 S/s`.
2. **NUNCA leia o RTL-SDR de forma síncrona no loop de DSP:** Mantenha sempre o thread de captura USB (`reader_thread`) isolado com fila concorrente para evitar buffer overflows no barramento USB.
3. **NUNCA transmita pacotes soltos de 188 bytes via UDP:** Sempre acumule 7 pacotes completos formando datagramas de exatamente 1316 bytes.
4. **NUNCA altere a URL padrão do VLC:** A URL deve ser sempre `udp://@127.0.0.1:1234`.
5. **NUNCA injete pacotes sintéticos de teste (`TsGenerator`) em fluxo real:** Pacotes sintéticos de tela verde travam os PIDs do player e bloqueiam a visualização do canal real.
6. **NUNCA reduza o limiar de IFO para menos de 0.60 sem histerese:** Lóbulos secundários de pilotos dispersos possuem coerência ~0.35 e prendem o receptor em frequências falsas.
7. **NUNCA reinicie o Trellis do Viterbi a cada símbolo OFDM:** O fluxo de dados convolucional é contínuo ao longo de todo o quadro e transmissão.
8. **NUNCA remova o delay de 100 ms após sintonizar frequência:** O PLL do R820T precisa desse tempo para travar sem gerar `[R82XX] PLL not locked!`.
9. **NUNCA altere a frequência do tuner no RTL-SDR sem limpar os buffers USB (`rtlsdr_reset_buffer`) e aguardar estabilização do PLL:** Chamar `rtlsdr_set_center_freq` logo após leituras USB sem reset de buffer gera colisão nos endpoints de controle USB (`LIBUSB_ERROR_PIPE -9`) e trava o chip I2C R820T.

---

## 🛠️ 6. Como Compilar e Rodar

No Windows com `w64devkit` (GCC 16.2 / CMake):

```powershell
# Reconfigurar o CMake
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

# Compilar todos os executáveis
cmake --build build -j

# Rodar todos os testes unitários
ctest --test-dir build --output-on-failure

# Sintonizar e assistir em tempo real com VLC (udp://@127.0.0.1:1234)
.\build\isdbtb_rx.exe play -c 23
```

---

## 🗺️ 7. Mapa de Módulos do Código

| Módulo | Arquivos | Responsabilidade |
|---|---|---|
| **OFDM Sync** | `include/dsp/ofdm_sync.hpp`<br>`src/dsp/ofdm_sync.cpp` | Autocorrelação de prefixo cíclico (Van de Beek), rastreamento fino CP, correção de FFO contínua via SIMD VOLK, FFTW3. |
| **1-Seg Demodulator** | `include/dsp/oneseg_demodulator.hpp`<br>`src/dsp/oneseg_demodulator.cpp` | IFO + Pilotos SP/CP ($X^{11}+X^2+1$), TMCC DBPSK com BCH(273,191), Deinterleavers (Freq/Tempo/Bit/Forney) e integração FEC. |
| **Reed-Solomon** | `include/dsp/reed_solomon.hpp`<br>`src/dsp/reed_solomon.cpp` | Decodificador \(RS(204, 188, t=8)\) com Berlekamp-Massey, Chien Search e Forney (`FCR=0`). |
| **Descrambler** | `include/dsp/descrambler.hpp`<br>`src/dsp/descrambler.cpp` | LFSR de 15 bits (\(1 + X^{14} + X^{15}\)) com suporte ao modo ISDB-T (por quadro OFDM) e superframe. |
| **Viterbi** | `include/dsp/viterbi_decoder.hpp`<br>`src/dsp/viterbi_decoder.cpp` | Inner FEC convolucional contínuo \(K=7\) (`0x4F`/`0x6D`) com matriz Trellis de 64 estados e puncionamento (1/2, 2/3, 3/4, 5/6, 7/8). |
| **Streaming UDP** | `include/streaming/udp_streamer.hpp`<br>`src/streaming/udp_streamer.cpp` | Sockets WinSock2/POSIX enviando datagramas de 1316B para VLC via `udp://@127.0.0.1:1234`. |
| **PSI/SI Parser** | `include/mpegts/psi_parser.hpp`<br>`src/mpegts/psi_parser.cpp` | Parser de PAT (PID 0), PMT e SDT (PID 0x0011) para capturar o nome da emissora e síntese de PAT de fallback. |
| **Channel Manager** | `include/cli/channel_manager.hpp`<br>`src/cli/channel_manager.cpp` | Gerencia `channels.json` e exportação `.m3u` formatada para VLC. |
| **Main CLI** | `src/main.cpp` | Interface CLI11 com captura assíncrona USB multithread, estabilização de PLL e comandos `probe`, `scan`, `list`, `export` e `play`. |
