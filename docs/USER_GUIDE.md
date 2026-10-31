# 📖 Manual do Usuário e Guia de Utilização (ISDB-T / ISDB-Tb Receiver)

Este guia prático ensina como configurar o dongle RTL-SDR, instalar os drivers necessários, escanear canais de TV digital e assistir à transmissão em tempo real no **VLC Media Player**.

---

## 🔌 1. Configuração do Hardware (RTL-SDR no Windows 10+)

Ao conectar o dongle RTL-SDR pela primeira vez no Windows, o sistema operacional instala um driver padrão de controle de rádio FM que bloqueia o acesso direto às amostras I/Q brutas. É necessário substituir o driver pelo **WinUSB** usando o utilitário **Zadig**:

1. Baixe o **Zadig** gratuitamente em [zadig.akeo.ie](https://zadig.akeo.ie/).
2. Conecte o dongle RTL-SDR na porta USB do computador.
3. Abra o **Zadig** como Administrador.
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

- **Varredura em uma faixa específica (ex: canais 28 ao 32):**
  ```powershell
  .\build\isdbtb_rx.exe scan --start 28 --end 32
  ```

Os canais descobertos são salvos automaticamente no arquivo:
- **Windows:** `%APPDATA%\isdbtb_rx\channels.json`
- **Linux:** `~/.config/isdbtb_rx/channels.json`

---

### 📋 2.3 Listar os Canais Salvos (`list`)
Exibe uma tabela formatada no terminal com todos os canais sintonizados, suas frequências e níveis de potência (RSSI):
```powershell
.\build\isdbtb_rx.exe list
```
*Exemplo:*
```text
=== Lista de Canais ISDB-T Armazenados (5) ===

Canal | Frequencia   |   RSSI    | Codec Video | Codec Audio | Nome / Emissora
------+--------------+-----------+-------------+-------------+----------------
  28  |  557.143 MHz |   -2.32 dBFS |       H.264 |         AAC | Canal 28
  29  |  563.143 MHz |   -2.08 dBFS |       H.264 |         AAC | Canal 29
  30  |  569.143 MHz |   -1.93 dBFS |       H.264 |         AAC | Canal 30
  31  |  575.143 MHz |   -2.25 dBFS |       H.264 |         AAC | Canal 31
  32  |  581.143 MHz |   -4.17 dBFS |       H.264 |         AAC | Canal 32
```

---

### 📺 2.4 Assistir ao Canal no VLC (`play`)
Sintoniza a frequência desejada, inicia o decodificador DSP e transmite os pacotes MPEG-TS em tempo real via UDP na porta 1234.

#### Opção A: Sintonizar e abrir o VLC automaticamente
```powershell
.\build\isdbtb_rx.exe play -c 30 --vlc
```

#### Opção B: Iniciar a transmissão e abrir o VLC manualmente
1. No terminal, execute:
   ```powershell
   .\build\isdbtb_rx.exe play -c 30
   ```
2. Abra o VLC Media Player.
3. Clique no menu **Mídia** > **Abrir Fluxo de Rede** (ou pressione `Ctrl + N`).
4. Digite a URL:
   ```
   udp://@127.0.0.1:1234
   ```
5. Clique em **Reproduzir**.

Durante a exibição, o terminal atualiza os dados em tempo real:
```text
[Status] RSSI: -1.9 dBFS | OFDM: LOCKED (Pico 0.94) | FFO: 314 Hz | TS Enviados: 1420 pkts
```
Para encerrar a transmissão, pressione **Ctrl + C** no terminal.

---

### 📑 2.5 Exportar Playlist M3U (`export`)
Gera um arquivo de playlist `.m3u` que pode ser aberto diretamente no VLC ou Kodi:
```powershell
.\build\isdbtb_rx.exe export -o canais.m3u
```
Para assistir: basta dar duplo clique no arquivo `canais.m3u` ou arrastá-lo para dentro da janela do VLC!

---

## 🛠️ 3. Solução de Problemas Frequentes (Troubleshooting)

### ❓ "Nenhum RTL-SDR detectado"
- Verifique se o cabo USB está bem conectado.
- Reabra o **Zadig** e certifique-se de que o driver instalado na Interface 0 é o **WinUSB**.

### ❓ Mensagem "[R82XX] PLL not locked!"
- Esta mensagem é comum durante a inicialização do chip sintonizador Rafael Micro R820T ao trocar abruptamente de frequência e não impede a recepção.

### ❓ "Status OFDM: Ruido / Não Travado" no scanner
- **Posicionamento da Antena:** Sinais de TV digital UHF (470–700 MHz) necessitam de antena adequada (preferencialmente perto de janelas ou externa).
- **Extensão da antena:** Ajuste o comprimento da haste telescópica da antena para cerca de **12 a 15 cm** (correspondente a \(1/4\) de onda da faixa média de UHF ~550 MHz).
- Verifique se a sua cidade/bairro possui cobertura digital ativa naquele canal específico.
