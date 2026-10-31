# 🏛️ Registros de Decisões de Arquitetura (ADR)

Este documento registra as decisões de engenharia fundamentais tomadas durante o design do decodificador ISDB-T/ISDB-Tb, suas alternativas avaliadas, justificativas matemáticas e de implementação.

---

## ADR 001: Escopo de Recepção Estrito em 1-Seg com Dongles RTL-SDR

### Contexto
O padrão ISDB-T/ISDB-Tb divide a banda de 6 MHz do canal de TV em 13 segmentos ortogonais:
- **Segmentos 1 a 12:** Transmissão Full-Seg de alta definição (HDTV 1080i), ocupando aproximadamente 5.2 MHz a 5.6 MHz de largura útil.
- **Segmento 0 (Central):** Transmissão One-Seg (1-Seg) para recepção móvel, ocupando \(428.57\text{ kHz}\).

O dongle RTL-SDR (baseado no chip Realtek RTL2832U) transfere amostras I/Q de 8 bits via USB 2.0. A taxa máxima de amostragem contínua segura sem descarte de amostras (dropped samples / buffer overflows) é de aproximadamente **2.4 MSps**. Taxas acima disso (como 2.8 a 3.2 MSps) provocam perda esporádica de pacotes de dados no barramento USB.

### Decisão
O escopo principal deste decodificador é **estritamente o modo 1-Seg**.

### Consequências
- **Positivas:** Estabilidade total de fluxo contínuo no RTL-SDR sem dropped samples; carga de processamento de CPU muito menor (viável em laptops e mini-PCs); compatível com qualquer dongle RTL-SDR de baixo custo.
- **Limitações:** O áudio e vídeo recebidos são os do feed 1-Seg (QVGA/VGA em H.264 e áudio AAC).
- **Variações Futuras:** Caso se deseje decodificar o feed Full-Seg (13 segmentos), a arquitetura pode ser estendida para suportar hardware SDR de banda larga (como HackRF One, Airspy R2, LimeSDR ou BladeRF) operando a \(F_s = 8.126984\text{ MSps}\).

---

## ADR 002: Taxa de Amostragem Exata (\(2031746\text{ S/s}\)) vs \(2.048\text{ MSps}\)

### Contexto
Pelas normas ARIB STD-B31 e ABNT NBR 15601, a taxa de chip fundamental do ISDB-T é:
\[
F_{base} = \frac{512}{63}\text{ MHz} \approx 8.126984\text{ MHz}
\]
A maioria dos tutoriais genéricos de SDR utiliza \(2.048\text{ MSps}\). No entanto, \(2.048\text{ MHz}\) possui um erro de frequência de amostragem de:
\[
\frac{2.048 - 2.031746}{2.031746} \approx +0.80\%
\]
Esse erro de 0.8% faz com que a janela do símbolo OFDM deslize a cada poucos símbolos, exigindo um reamostrador racional fracionário computacionalmente pesado e degradando a correlação de prefixo cíclico.

### Decisão
Configurar o hardware do RTL-SDR para operar na taxa exata:
\[
F_s = \frac{F_{base}}{4} = \frac{512}{63 \times 4}\text{ MHz} = 2031746\text{ Hz}
\]

### Consequências
- O sintonizador do RTL-SDR possui sintetizador de frequência fracionário interno que atinge \(2031746.004\text{ Hz}\) (erro inferior a 0.005 Hz).
- No Modo 3 (8K), um símbolo completo dura exatamente \(2048 + 128 = 2176\) amostras (para GI 1/16).
- A correlação do prefixo cíclico saltou de \(11.5\%\) para **\(94.5\%\)** de pico nos testes reais, garantindo sincronismo de símbolo robusto e estável.

---

## ADR 003: Streaming UDP (\(7 \times 188 = 1316\text{ bytes}\)) para o Reprodutor VLC

### Contexto
Para exibir o canal sintonizado ao usuário, foram analisadas três abordagens:
1. **Pipe binário no terminal (`| vlc -`):** No Windows, o PowerShell e o CMD possuem problemas frequentes com quebra de pipes binários ao fechar o player, além de exigir comandos longos.
2. **Servidor HTTP integrado (HLS):** Exige fatiar arquivos `.ts` e gerar listas `.m3u8`, introduzindo 2 a 4 segundos de atraso (latência) de buffer.
3. **Datagramas UDP Broadcast/Unicast:** O padrão da indústria de IPTV (RFC 2250).

### Decisão
Adotar o streaming nativo via **datagramas UDP** enviados para `udp://@127.0.0.1:1234`, encapsulando exatamente 7 pacotes MPEG-TS de 188 bytes por datagrama (\(1316\text{ bytes}\)).

### Justificativa do Tamanho de 1316 Bytes
- O MTU (Maximum Transmission Unit) padrão da camada Ethernet é de 1500 bytes.
- Cabeçalho IPv4 = 20 bytes; Cabeçalho UDP = 8 bytes. Espaço útil máximo sem fragmentação: \(1500 - 28 = 1472\text{ bytes}\).
- O maior múltiplo inteiro de pacotes MPEG-TS de 188 bytes que cabe em 1472 bytes é:
  \[
  \lfloor 1472 / 188 \rfloor = 7 \implies 7 \times 188 = 1316\text{ bytes}
  \]
- Enviar exatamente 1316 bytes por datagrama garante zero fragmentação no kernel, consumo mínimo de interrupções de CPU e latência inferior a 300 ms no VLC via `udp://@127.0.0.1:1234`.

---

## ADR 004: Núcleo DSP Nativo em C++17 Descartando o GNU Radio

### Contexto
O projeto acadêmico de referência `gr-isdbt` foi construído como um módulo out-of-tree para o GNU Radio. No entanto, o GNU Radio traz dezenas de dependências gigantescas (Python runtime, Boost, log4cpp, Thrift, PyBind11, Qt5/6, UHD) que tornam o deploy em Windows extremamente volumoso e frágil.

### Decisão
Construir um núcleo C++17 puro, extraindo e otimizando os algoritmos matemáticos essenciais, mantendo como únicas dependências externas de cálculo vetorial a **FFTW3** e o **VOLK**.

### Consequências
- Binário final compacto (\(< 2\text{ MB}\)), rápido para iniciar, sem latência de inicialização de interpretador Python para o DSP.
- Compatibilidade cruzada nativa garantida no Windows (MinGW-w64 e MSVC) e Linux.

---

## ADR 005: Decodificador Reed-Solomon \(RS(204, 188, t=8)\) com Campo de Galois \(GF(2^8)\)

### Contexto
A Camada Externa de FEC (Outer FEC) do ISDB-T e DVB-T utiliza o código Reed-Solomon encurtado \(RS(204, 188)\) derivado de \(RS(255, 239)\). Cada bloco de 204 bytes possui 188 bytes de carga útil e 16 bytes de paridade, permitindo corrigir até:
\[
t = \frac{204 - 188}{2} = 8\text{ bytes corrompidos por pacote}
\]

### Implementação
- **Polinômio Primitivo:** \(p(x) = x^8 + x^4 + x^3 + x^2 + 1\) (\(0x11D\) / 285 decimal).
- **Tabelas de Exponenciação e Logaritmo:** `exp_table_[512]` e `log_table_[256]` pré-calculadas para transformar multiplicações e divisões em adições e subtrações modulares em \(O(1)\).
- **Algoritmo de Berlekamp-Massey:** Determina o polinômio localizador de erros \(\Lambda(x)\) de grau \(L \le 8\).
- **Busca de Chien:** Avalia as raízes de \(\Lambda(x)\) para identificar as posições exatas dos erros no bloco.
- **Algoritmo de Forney:** Calcula a magnitude exata de cada erro sem inversão de matrizes.

---

## ADR 006: Decodificador Viterbi Soft-Decision (\(K=7\), 64 Estados)

### Contexto
A Camada Interna de FEC (Inner FEC) utiliza código convolucional com comprimento de restrição \(K=7\) e polinômios geradores:
- \(G_1 = 171_8 = 0b1111001\)
- \(G_2 = 133_8 = 0b1011011\)

### Implementação
- Como \(K=7\), o encoder possui \(2^{K-1} = 2^6 = 64\) estados possíveis na Trellis.
- **Métricas Soft (Soft-Decision):** O decodificador aceita métricas de entrada contínuas (valores quantizados de 0 a 255, onde 128 representa símbolo apagado / puncionado). Isso proporciona um ganho de codificação de aproximadamente **\(2\text{ dB}\)** em relação à decodificação hard-decision (bits binários 0 ou 1).
- **Traceback com `std::array`:** Uso de arrays estáticos de tamanho fixo 64 (`std::array<uint32_t, 64>`) para evitar alocações dinâmicas de memória durante o percurso da Trellis.

---

## ADR 007: Sincronismo de Símbolo OFDM e FFO por Autocorrelação de Prefixo Cíclico (Van de Beek)

### Contexto
O sinal OFDM no domínio do tempo é composto por uma parte útil de \(N_{FFT}\) amostras precedida por uma cópia de suas últimas \(N_{GI}\) amostras (o Prefixo Cíclico ou Intervalo de Guarda).

### Implementação
O algoritmo calcula de forma deslizante:
\[
\Gamma(d) = \sum_{m=0}^{N_{GI}-1} r(d+m) \cdot r^*(d+m+N_{FFT})
\]
\[
\Phi(d) = \frac{1}{2} \sum_{m=0}^{N_{GI}-1} \left( |r(d+m)|^2 + |r(d+m+N_{FFT})|^2 \right)
\]
A métrica normalizada \(|\Gamma(d)|^2 / \Phi(d)^2\) atinge um pico agudo exatamente na transição do prefixo cíclico para o símbolo útil.
Além disso, a fase do pico fornece diretamente o Desvio Fracionário de Frequência (Fractional Frequency Offset - FFO):
\[
\Delta f = -\frac{\angle \Gamma(d_{max})}{2\pi \cdot T_u}
\]
A correção desse desvio em tempo real é acelerada pela função SIMD `volk_32fc_x2_multiply_32fc`, eliminando qualquer rotação residual de fase antes da FFT.

---

## ADR 008: Alinhamento Matemático Integral com `git-artes/gr-isdbt` (Erros Corrigidos e Acertos Validados)

### Contexto
Na validação cruzada contra o repositório de referência [`git-artes/gr-isdbt`](https://github.com/git-artes/gr-isdbt), constatou-se que pequenas diferenças de convenção de bits/registradores e de enquadramento entre DVB-T e ISDB-T impediam a decodificação de pacotes MPEG-TS reais do ar.

### Erros Corrigidos e Regras Permanentes
1. **PRBS de Pilotos (`X^11 + X^2 + 1`):** Em registrador LFSR de 11 bits com deslocamento à direita (`reg >> 1`), o tap correspondente a $X^9$ é `reg >> 2` (e não `reg >> 9`). O PRBS deve ser rodado desde a portadora 0 do Modo 3 até o início do Segmento 0 (`2592 = 6 * 432`).
2. **Viterbi Contínuo (`0x4F` / `0x6D`):** Como o bit de entrada entra no LSB (`reg7 = (state << 1) | b`), os polinômios octais $171_8$ e $133_8$ devem ser espelhados em 7 bits para `0x4F` e `0x6D`, e o bit sobrevivente salvo no traceback deve ser `state & 0x20` (bit 5 que deixa o estado de 6 bits), mantendo a Trellis contínua entre símbolos OFDM.
3. **Reed-Solomon (`FCR = 0`):** Na busca de Chien, avaliar $X_k^{-1} = \alpha^{(255 - p_{\text{idx}}) \bmod 255}$; no algoritmo de Forney, multiplicar por $X_k = \alpha^{p_{\text{idx}}}$ devido ao primeiro radical consecutivo ser $\alpha^0$ (`FCR = 0`).
4. **Dispersão de Energia ISDB-T (sem `0xB8`):** Diferente do DVB-T, o ISDB-T não inverte o sync byte para `0xB8` a cada 8 pacotes. O PRBS reseta no início de cada quadro OFDM de 204 símbolos (`frame_begin` do TMCC), opera sobre os 203 bytes (`0..202`) que antecedem o `0x47` (posição `203`, ramo 11 do Forney) e avança 8 clocks durante o byte `0x47`.
5. **Decodificador TMCC DBPSK + BCH(273, 191):** Demodula diferencialmente as 4 portadoras TMCC (`101, 131, 286, 349`), verifica as palavras de sincronismo par (`0x35EE`) e ímpar (`0xCA11`) e valida a síndrome `BCH(273, 191)` antes de aplicar automaticamente os parâmetros da Camada A (modulação, taxa Viterbi e profundidade $I$ do desentrelaçador de tempo).
6. **Continuidade Temporal de Símbolos no Receptor SDR:** O loop de leitura USB em `src/main.cpp` mantém um buffer deslizante contínuo (`stream_iq_buf`) e acumulador de fase NCO contínuo (`current_phase_`), garantindo que nenhum símbolo OFDM seja descartado nas fronteiras de leitura de `32768` amostras.

---

## ADR 009: Arquitetura Multithread de Captura Assíncrona USB (Fila Concorrente de Amostras I/Q)

### Contexto
Durante os testes de recepção ao vivo em tempo real, verificou-se que chamar `rtlsdr_read_sync` no mesmo thread onde é executada a computação intensiva de DSP (FFT 2048 pontos, desentrelaçamento temporal, Trellis de Viterbi e Reed-Solomon) bloqueava o atendimento das transferências USB por 10 a 25 ms por bloco.
Esse atraso excedia a capacidade do buffer interno do chip RTL2832U, provocando perda silenciosa de amostras contínuas no barramento USB. Essa perda fragmentava o quadro TMCC de 204 símbolos contínuos, mantendo o demodulador preso no estado `TMCC: SYNC` sem nunca atingir `TMCC: LOCK`.

### Decisão
Desacoplar a recepção USB do processamento DSP:
1. Um thread dedicado de alta prioridade (`reader_thread`) executa exclusivamente `rtlsdr_read_sync` em loop contínuo.
2. Cada bloco de 32768 amostras brutas lidas é inserido em uma fila concorrente (`std::queue<std::vector<uint8_t>>`) sincronizada por `std::mutex` e `std::condition_variable` com limite de segurança de 64 blocos.
3. O thread principal extrai os blocos da fila e alimenta o pipeline DSP sem perda de continuidade.

### Consequências
- Fim total de buffer overflows no barramento USB.
- O sincronismo do TMCC trava em menos de 300 ms após a sintonia, iniciando imediatamente a emissão contínua de pacotes MPEG-TS do ar.
