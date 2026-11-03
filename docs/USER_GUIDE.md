# 📖 Manual do Usuário e Guia de Utilização (ISDB-T / ISDB-Tb Receiver)

Este guia prático ensina como configurar o dongle RTL-SDR, instalar os drivers necessários, escanear canais de TV digital e assistir à transmissão em tempo real no **MPV Media Player** (integrado) ou no **VLC Media Player**.

---

## 🔌 1. Configuração do Hardware (RTL-SDR no Windows 10+)

Ao conectar o dongle RTL-SDR pela primeira vez no Windows, o sistema operacional instala um driver padrão de controle de rádio FM que bloqueia o acesso direto às amostras I/Q brutas. É necessário substituir o driver pelo **WinUSB** usando o utilitário **Zadig**:

1. O utilitário **Zadig** (v2.9) já vem incluído no projeto na pasta **`tools/zadig.exe`** (ou na raiz das releases portáteis).
2. Conecte o dongle RTL-SDR na porta USB do computador.
3. Execute o **`tools/zadig.exe`** como Administrador.
4. No menu superior, clique em **Options** e marque **List All Devices**.
5. No menu dropdown principal, selecione:
   - `Bulk-In, Interface (Interface 0)` ou `RTL2838UHIDIR`.
6. No campo do driver de destino (à direita da seta verde), certifique-se de que está selecionado **WinUSB**.
7. Clique no botão **Replace Driver** (ou **Install Driver**) e aguarde a confirmação de sucesso.

---

## 🚀 2. Comandos Principais da CLI

Abra o terminal (PowerShell ou CMD) na pasta do projeto ou da release:

### 🔍 2.1 Inspecionar o RTL-SDR (`probe`)
Verifica se o dongle foi reconhecido corretamente pelo sistema e inspeciona os recursos de hardware:
```powershell
.\isdbtb_rx.exe probe
```
*Saída esperada:*
```text
=== Inspecao de Hardware RTL-SDR e Aceleracao DSP ===

[RTL-SDR] Dispositivos conectados: 1
  - Dongle #0: Generic RTL2832U OEM
    Fabricante: Realtek | Serial: 00000001
    Ganhos de RF suportados (29): 0 0.9 1.4 2.7 3.7 7.7 8.7 ... 49.6 dB

[SIMD/DSP] VOLK Versao 3.1.2
  - Aceleracao AVX2: ATIVADA
  - Aceleracao FMA: ATIVADA
  - FFTW3: 2048/4096/8192 pontos inicializados
```

---

### 📡 2.2 Varrer os Canais de TV (`scan`)
Executa a varredura profunda no espectro UHF, detecta o sinal de cada canal, verifica a sincronização OFDM e salva a lista no computador:

- **Varredura padrão (Canais UHF 14 ao 69):**
  ```powershell
  .\isdbtb_rx.exe scan
  ```

- **Varredura em uma faixa específica (ex: canais 20 ao 25):**
  ```powershell
  .\isdbtb_rx.exe scan --start 20 --end 25
  ```

Os canais descobertos são salvos automaticamente no arquivo local:
- `./channels.json` (no próprio diretório da aplicação, 100% autocontido e portátil).

---

### 📋 2.3 Listar os Canais Salvos (`list`)
Exibe uma tabela formatada no terminal com todos os canais sintonizados, suas frequências, codecs, força visual do sinal e potência em dBFS:
```powershell
.\isdbtb_rx.exe list
```
*Exemplo:*
```text
=== Lista de Canais ISDB-T Armazenados ===

Canal | Frequencia   |   Forca do Sinal    |  Potencia | Codecs (V/A) | Nome / Emissora
------+--------------+---------------------+-----------+--------------+----------------
  23  |  527.143 MHz | [#####] Alto (Forte) |   -1.8 dBFS |    H.264/AAC | BAND HD
  28  |  557.143 MHz | [#####] Alto (Forte) |    0.5 dBFS |    H.264/AAC | Canal 28
  30  |  569.143 MHz | [#####] Alto (Forte) |    0.4 dBFS |    H.264/AAC | Canal 30
```

---

### 📺 2.4 Assistir ao Canal no Player (`play`)
Sintoniza a frequência desejada, inicia a captura assíncrona USB com alta prioridade de thread (`THREAD_PRIORITY_HIGHEST`) e decodifica os pacotes MPEG-TS em tempo real via UDP na porta 1234.

#### Opção Recomendada (Comando Único com MPV Integrado):
Use a flag `--player` para sintonizar o canal e **abrir automaticamente o MPV Player integrado** com interpolação de resolução e movimento em tempo real:
```powershell
.\isdbtb_rx.exe play -c 23 --player
```
O MPV abrirá diretamente carregando as otimizações de GPU de `mpv.conf`:
- **Interpolação Espacial (EWA-Lanczos Sharp):** Upscaling inteligente de $320 \times 240/180$ para a resolução nativa do seu monitor (1080p, 1440p, 4K) eliminando pixelização e dentes de serra.
- **Interpolação Temporal (`display-resample` + `oversample`):** Converte a taxa de ~15/30 fps da TV móvel para 60Hz/120Hz/144Hz da tela, gerando movimentos fluidos sem engasgos.
- **Debanding Dinâmico:** Remove artefatos de compressão e faixas de cores em tempo real.

#### 📶 Escolha do Modo de Sincronismo OFDM (`--ofdm <1|2>`):
O comando `play` permite selecionar o algoritmo de sincronismo OFDM através da flag `--ofdm`:

```powershell
# Modo 1 (Padrão): Van de Beek baseline. Mais leve e econômico para a CPU:
.\isdbtb_rx.exe play -c 23 --player

# Modo 2 (Reforçado): Port robusto do gr-isdbt com rastreamento ativo de timing por pilotos (SP),
# correção de desvio de frequência fracionário e inteiro (CFO) e relógio de amostragem (SCO):
.\isdbtb_rx.exe play -c 23 --ofdm 2 --player
```

> [!NOTE]
> **Quando usar cada modo:**
> - **`--ofdm 1` (Padrão):** Ideal para uso diário, notebooks com economia de bateria e estações de TV com sinal estável e boa visada direta da torre transmissora.
> - **`--ofdm 2` (Reforçado):** Recomendado quando o sinal sofre com ecos de multi-percurso (reflexo em prédios), ruído elevado, antenas internas em locais difíceis ou recepção em movimento (desvio Doppler). Exige maior capacidade de processamento da CPU.
> - **Flexibilidade Sintática:** Aceita tanto argumento separado por espaço (`--ofdm 1` / `--ofdm 2`) quanto com sinal de igual (`--ofdm=1` / `--ofdm=2`).
> - **Compatibilidade:** A flag `--ofdm` é exclusiva do comando `play`. Em qualquer outro comando (`scan`, `probe`), ela é silenciosamente ignorada sem gerar erros.

> [!TIP]
> **Atalhos Úteis no MPV durante a exibição:**
> - `f`: Alternar tela cheia (*Fullscreen*).
> - `i`: Exibir estatísticas de renderização, resolução de interpolação e perda de quadros na tela (OSD).
> - `d`: Ligar ou desligar o filtro de Deband.
> - `q`: Fechar o player.

#### Abertura Manual ou Outros Reprodutores:
Se preferir executar o receptor e o player em terminais separados:
1. No terminal, inicie o receptor SDR:
   ```powershell
   .\isdbtb_rx.exe play -c 23
   ```
2. Abra o player:
   - **MPV:** Execute `.\scripts\play_mpv.bat` (ou duplo clique no arquivo).
   - **VLC:** Execute no menu **Mídia** > **Abrir Fluxo de Rede** (`Ctrl + N`) o endereço `udp://@127.0.0.1:1234` com Cache de Rede configurado em 2000 ms.

#### Modo Detalhado (-v / --verbose) & Interface Interativa:
Adicione `-v` ou `--verbose` para exibir a telemetria do sinal RF e do decodificador em tempo real:
```powershell
.\isdbtb_rx.exe play -c 23 --player -v
```

- **Barra de Navegação no Rodapé (Última Linha):** Exibida em alto contraste com **fundo branco e letras pretas** (`\033[30;47m`):
  `Comandos: [▲/▼] Navegar | [Enter] Sintonizar | [q] Sair`
- **Telemetria de Sinal (Antepenúltima Linha com `-v`):** Atualizada continuamente na antepenúltima linha, com truncamento dinâmico que impede quebras de linha ao redimensionar o terminal:
```text
[Status] Ch 23 | RSSI: -6.9 dBFS | OFDM: LOCK (0.98) | TMCC: LOCK | SNR: 18.5 dB | TS: 542 pkts (RS: 0 corr / 0 drop) | Drop: 0 | BAND HD
```
Para encerrar a transmissão, pressione **q** ou **Ctrl + C** no terminal.

---

## 🛠️ 3. Solução de Problemas Frequentes (Troubleshooting)

### ❓ "Nenhum RTL-SDR detectado"
- Verifique se o cabo USB está bem conectado.
- Reabra o **Zadig** e certifique-se de que o driver instalado na Interface 0 é o **WinUSB**.

### ❓ Mensagem "[R82XX] PLL not locked!"
- Esta mensagem ocorria quando não havia tempo de espera após a sintonia. A versão atual já inclui 100 ms de estabilização do PLL após `rtlsdr_set_center_freq`.

### ❓ "Status OFDM: Ruido / Não Travado" no scanner
- **Posicionamento da Antena:** Sinais de TV digital UHF (470–700 MHz) necessitam de antena adequada (preferencialmente perto de janelas ou externa).
- **Extensão da antena:** Ajuste o comprimento da haste telescópica da antena para cerca de **12 a 15 cm** (correspondente a \(1/4\) de onda da faixa média de UHF ~550 MHz).

---

## 🏗️ 4. Compilação a partir do Código-Fonte & Testes (CTest)

Para compilar o projeto em ambiente MinGW-w64 (`w64devkit`):

### Compilação para 64 bits (x64):
```powershell
# 1. Configurar o CMake em Release para 64 bits com suporte ao gr-isdbt sync:
cmake -S . -B build_x64 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DTARGET_ARCH=x64 -DISDBT_ENABLE_GRISDBT_SYNC=ON

# 2. Compilar todos os binários e executáveis de teste:
cmake --build build_x64 -j

# 3. Executar os 20 testes unitários determinísticos:
ctest --test-dir build_x64 --output-on-failure -E BenchSync
```

### Compilação para 32 bits (x86):
```powershell
# 1. Configurar o CMake em Release para 32 bits com instruções SSE2:
cmake -S . -B build_x86 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DTARGET_ARCH=x86 -DISDBT_ENABLE_GRISDBT_SYNC=ON

# 2. Compilar todos os binários e executáveis de teste:
cmake --build build_x86 -j

# 3. Executar os 20 testes unitários determinísticos:
ctest --test-dir build_x86 --output-on-failure -E BenchSync
```
