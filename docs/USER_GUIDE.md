# 📖 Manual do Usuário e Guia de Utilização (ISDB-T / ISDB-Tb Receiver)

Este guia prático ensina como configurar o dongle RTL-SDR, instalar os drivers necessários, escanear canais de TV digital e assistir à transmissão em tempo real no **VLC Media Player**.

---

## 🔌 1. Configuração do Hardware (RTL-SDR no Windows 10+)

Ao conectar o dongle RTL-SDR pela primeira vez no Windows, o sistema operacional instala um driver padrão de controle de rádio FM que bloqueia o acesso direto às amostras I/Q brutas. É necessário substituir o driver pelo **WinUSB** usando o utilitário **Zadig**:

1. O utilitário **Zadig** (v2.9) já vem incluído no projeto na pasta **`tools/zadig.exe`** (ou pode ser baixado em [zadig.akeo.ie](https://zadig.akeo.ie/)).
2. Conecte o dongle RTL-SDR na porta USB do computador.
3. Execute o **`tools/zadig.exe`** como Administrador.
4. No menu superior, clique em **Options** e marque **List All Devices**.
5. No menu dropdown principal, selecione:
   - `Bulk-In, Interface (Interface 0)` ou `RTL2838UHIDIR`.
6. No campo do driver de destino (à direita da seta verde), certifique-se de que está selecionado **WinUSB**.
7. Clique no botão **Replace Driver** (ou **Install Driver**) e aguarde a confirmação de sucesso.

---

## 🚀 2. Comandos Principais da CLI

Abra o terminal (PowerShell ou CMD) na pasta do projeto:

### 🔍 2.1 Inspecionar o RTL-SDR (`probe`)
Verifica se o dongle foi reconhecido corretamente pelo sistema e inspeciona os recursos de hardware:
```powershell
.\build\isdbtb_rx.exe probe
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

- **Varredura padrão (Canais UHF 14 ao 45):**
  ```powershell
  .\build\isdbtb_rx.exe scan
  ```

- **Varredura em uma faixa específica (ex: canais 20 ao 25):**
  ```powershell
  .\build\isdbtb_rx.exe scan --start 20 --end 25
  ```

Os canais descobertos são salvos automaticamente no arquivo:
- **Windows:** `%APPDATA%\isdbtb_rx\channels.json`
- **Linux:** `~/.config/isdbtb_rx/channels.json`

---

### 📋 2.3 Listar os Canais Salvos (`list`)
Exibe uma tabela formatada no terminal com todos os canais sintonizados, suas frequências, codecs, força visual do sinal e potência em dBFS:
```powershell
.\build\isdbtb_rx.exe list
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

#### Opção Recomendada (Comando Único com MPV):
Use a flag `--player` para sintonizar o canal e **abrir automaticamente o MPV Player integrado** com interpolação de resolução e movimento em tempo real:
```powershell
.\build\isdbtb_rx.exe play -c 23 --player
```
O MPV abrirá diretamente carregando as otimizações de GPU de `tools/mpv/mpv.conf`:
- **Interpolação Espacial (EWA-Lanczos Sharp):** Upscaling inteligente de $320 \times 240/180$ para a resolução nativa do seu monitor (1080p, 1440p, 4K) eliminando pixelização e dentes de serra.
- **Interpolação Temporal (`display-resample` + `oversample`):** Converte a taxa de ~15/30 fps da TV móvel para 60Hz/120Hz/144Hz da tela, gerando movimentos fluidos sem engasgos.
- **Debanding Dinâmico:** Remove artefatos de compressão e faixas de cores em tempo real.

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
   .\build\isdbtb_rx.exe play -c 23
   ```
2. Abra o player:
   - **MPV:** Execute `.\scripts\play_mpv.bat` (ou duplo clique no arquivo).
   - **VLC:** Execute no menu **Mídia** > **Abrir Fluxo de Rede** (`Ctrl + N`) o endereço `udp://@127.0.0.1:1234` com Cache de Rede configurado em 2000 ms.

Durante a exibição, o terminal atualiza os dados em tempo real:
```text
[Status] RSSI: -1.8 dBFS | OFDM: LOCKED (Pico 0.98) | FFO: 120 Hz (IFO -26784 Hz) | TMCC: LOCK | TS do Ar: 542 pkts
```
Para encerrar a transmissão, pressione **Ctrl + C** no terminal.

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
