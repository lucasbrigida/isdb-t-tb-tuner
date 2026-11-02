# 📺 ISDB-T / ISDB-Tb One-Seg Receiver & Real-Time Player (Windows x64 / x86)

Receptor de TV Digital aberta (**ISDB-T / ISDB-Tb / SBTVD**) para dongles **RTL-SDR** (RTL2832U com tuners R820T/R828D/E4000) com player **MPV embutido** e **interpolação de resolução e movimento em tempo real**.

---

## 🚀 Guia de Início Rápido (3 Passos)

1. **Conecte** seu dongle RTL-SDR e a antena UHF a uma porta USB do computador.
2. **Execute o `zadig.exe`** para instalar o driver WinUSB (apenas na primeira vez — instruções abaixo).
3. **Abra o terminal (PowerShell ou CMD)** na pasta descompactada e execute:
   ```powershell
   # 1. Verificar se o dongle foi reconhecido:
   .\isdbtb_rx.exe probe

   # 2. Varrer os canais do ar (UHF 14 a 69):
   .\isdbtb_rx.exe scan

   # 3. Assistir a um canal com o player MPV integrado:
   .\isdbtb_rx.exe play -c 23 --player
   ```

---

## 🔌 Instalação do Driver USB (Zadig)

Para que o decodificador se comunique diretamente com o hardware do RTL-SDR, é necessário o driver nativo **WinUSB**:

1. Dê um duplo clique no arquivo **`zadig.exe`** presente nesta pasta (conceda permissão de Administrador quando solicitado).
2. No menu superior do Zadig, clique em **Options** e marque a opção **List All Devices**.
3. Na caixa de seleção principal, procure e selecione:
   - `Bulk-In, Interface (Interface 0)`
   - *(ou `RTL2832U`, `RTL2838UHIDIR`)*
   > ⚠️ **IMPORTANTE:** Se o seu dongle listar `Interface 0` e `Interface 1`, selecione **obrigatoriamente a `Interface 0`**. A Interface 0 é o barramento de alta velocidade do sinal de RF; a Interface 1 é apenas o receptor infravermelho de controle remoto.
4. No campo do driver de destino (à direita da seta verde `➔`), certifique-se de que está selecionado **`WinUSB`** (ex: `WinUSB (v6.1.7600.16385)`).
5. Clique no botão **`Install Driver`** (ou **`Replace Driver`**).
6. Aguarde alguns segundos até a mensagem **`The driver was installed successfully`**. Pronto! O driver está configurado.

---

## 💻 Manual de Uso da CLI (`isdbtb_rx.exe`)

### 1. Diagnóstico do Dongle (`probe`)
Identifica o dongle conectado, tipo de sintonizador e lista de ganhos suportados:
```powershell
.\isdbtb_rx.exe probe
```

### 2. Varredura Automática de Canais (`scan`)
Realiza a varredura inteligente em dois passos nos canais UHF (14 a 69):
- **Passo 1 (Triagem Rápida):** Mede a potência de RF (RSSI) e a correlação de prefixo cíclico em ~35ms por canal, descartando ruído.
- **Passo 2 (Identificação):** Decodifica a tabela SDT (PID `0x0011`) para identificar o nome oficial da emissora (ex: `BAND HD`, `GLOBO`, `SBT`, `RECORD`).
```powershell
.\isdbtb_rx.exe scan
```
> Os canais encontrados são salvos automaticamente no arquivo `%APPDATA%\isdbtb_rx\channels.json`.

### 3. Listar Canais Salvos (`list`)
Exibe a lista de emissoras salvas na varredura:
```powershell
.\isdbtb_rx.exe list
```

### 4. Reproduzir Canal com MPV Integrado (`play`)
Sintoniza o canal e abre **automaticamente** a janela do player MPV embutido com interpolação de resolução e movimento ativada:
```powershell
# Reproduzir canal 23 com abertura automática do MPV (Modo padrão, leve):
.\isdbtb_rx.exe play -c 23 --player

# Reproduzir com modo de sincronismo reforçado (--ofdm 2) para sinais fracos ou com reflexos:
.\isdbtb_rx.exe play -c 23 --ofdm 2 --player

# Ajustar ganho manual (se estiver muito próximo à antena de transmissão):
.\isdbtb_rx.exe play -c 23 -g 28.0 --player

# Utilizar porta UDP customizada:
.\isdbtb_rx.exe play -c 23 -p 1234 --player

# Exibir status de telemetria em tempo real (RSSI, SNR, erros RS):
.\isdbtb_rx.exe play -c 23 --player -v
```

> 📶 **Opções de Sincronismo OFDM (`--ofdm`):**
> - `--ofdm 1` *(Padrão)*: Modo leve e econômico. Mais suave na CPU.
> - `--ofdm 2`: Modo reforçado. Mantém a imagem estável com sinal fraco, muito reflexo ou em movimento. Usa mais processamento.
> - *Aceita tanto sintaxe com espaço (`--ofdm 1` / `--ofdm 2`) quanto com sinal de igual (`--ofdm=1` / `--ofdm=2`).*
> - *A flag `--ofdm` é exclusiva de `play` e é ignorada nos demais comandos.*

> 💡 **Fechamento Automático:** O processo do MPV está vinculado a um *Job Object* do Windows. Ao fechar a CLI (pressionando `Ctrl+C` ou fechando a janela do console), o player MPV fecha instantaneamente sem deixar processos órfãos em segundo plano.

---

## 🎬 Recursos e Teclas do Player MPV Integrado

O MPV embutido nesta release foi pré-configurado especificamente para aprimorar a qualidade do sinal móvel 1-Seg (originalmente 320x240 / 320x180):
- **Super Resolução (Upscale):** Algoritmo `ewa_lanczossharp` com `sigmoid-upscaling` para bordas nítidas sem halos.
- **Filtro Anti-Blocos (Deband):** Remove gradações e artefatos de compressão típicos do H.264 em baixa taxa de bits.
- **Interpolação de Movimento (Display-Resample):** Sincroniza os quadros do vídeo à taxa de atualização do seu monitor (60Hz / 120Hz / 144Hz / 240Hz) eliminando trepidação (*judder*).

### Teclas de Atalho Úteis no MPV:
| Tecla | Função |
|:---:|---|
| **`f`** | Alternar modo **Tela Cheia** (*Full Screen*) |
| **`i`** | Exibir painel OSD com estatísticas em tempo real, resolução e FPS |
| **`d`** | Ligar / Desligar o filtro de deband |
| **`m`** | Ativar / Desativar mudo (*Mute*) |
| **`9` / `0`** | Diminuir / Aumentar volume |
| **`[` / `]`** | Ajustar velocidade de reprodução |
| **`q`** | Fechar o player |

---

## ❓ Perguntas Frequentes & Solução de Problemas (FAQ)

### P1: O comando `probe` retorna *"Nenhum dispositivo RTL-SDR encontrado"* ou *"rtlsdr_open failed"*.
- **Solução:**
  1. Desconecte e reconecte o dongle RTL-SDR.
  2. Abra o `zadig.exe`, marque `Options` -> `List All Devices`.
  3. Certifique-se de que selecionou **`Bulk-In, Interface (Interface 0)`** e instalou o driver **`WinUSB`**.
  4. Se estiver usando uma porta USB dianteira do gabinete ou um hub USB sem fonte externa, troque para uma **porta USB traseira direta da placa-mãe**.

### P2: Aparece o erro `[R82XX] PLL not locked!` no terminal.
- **Causa:** O oscilador local do sintonizador Rafael Micro R820T não conseguiu travar a frequência de RF.
- **Solução:**
  1. Verifique se o conector da antena não está em curto-circuito físico.
  2. Quedas de voltagem na porta USB podem causar instabilidade no tuner. Conecte diretamente na porta USB 3.0/2.0 da placa-mãe.
  3. A aplicação já inclui compensação automática de 100 ms para acomodação do PLL.

### P3: A varredura (`scan`) não encontra nenhum canal ou apenas canais vazios.
- **Causa:** Antena mal posicionada, sinal de RF UHF fraco ou ganho inadequado.
- **Solução:**
  1. Posicione a antena UHF próxima a uma janela ou em local aberto. Antenas internas no chão recebem atenuação severa de paredes de concreto.
  2. Ajuste o ganho manual no comando `play` caso saiba o canal da sua cidade:
     ```powershell
     .\isdbtb_rx.exe play -c 23 -g 38.0 --player
     ```
  3. Em áreas distantes dos transmissores, utilize ganho mais alto (`-g 42.0` ou `-g 49.6`). Em locais próximos a antenas, use ganho moderado (`-g 28.0` ou `-g 20.0`) para evitar saturação do conversor analógico-digital (ADC).

### P4: O player MPV abre mas a imagem fica travada ou preta.
- **Causa:** O sinal de RF está com taxa de erro elevada (BER) antes do sincronismo TMCC travar.
- **Solução:**
  1. Observe o console da CLI: aguarde a indicação `TMCC: LOCK` e `TS do Ar: > 0 pkts`. O desentrelaçador de tempo convolucional do ISDB-T requer cerca de 1 a 2 segundos de sinal contínuo para reconstruir os primeiros quadros válidos de vídeo.
  2. Pressione a tecla `i` no MPV para verificar o fluxo de entrada.
  3. Caso o sinal tenha muitos reflexos ou desvanecimento, experimente o modo reforçado: `.\isdbtb_rx.exe play -c <canal> --ofdm 2 --player`.

### P5: Posso reproduzir o fluxo no VLC Media Player em vez do MPV?
- **Sim!** O decodificador transmite o fluxo MPEG-TS padrão via UDP no formato de 1316 bytes (7 pacotes TS de 188B):
  1. Inicie a transmissão:
     ```powershell
     .\isdbtb_rx.exe play -c 23
     ```
  2. Abra o VLC Media Player.
  3. Vá em **Mídia** -> **Abrir Fluxo de Rede...** (`Ctrl+N`).
  4. Digite a URL: `udp://@127.0.0.1:1234`
  5. Marque "Exibir mais opções" e defina o **Cache de Rede** para `2000 ms`.
  *(Nota: Recomendamos o MPV integrado pelo suporte nativo a interpolação e ausência de problemas com resolução 1-Seg).*

---

## 📜 Licença e Componentes

- **ISDB-T / ISDB-Tb Tuner Player:** Licenciado sob a licença de software livre **GNU General Public License v3.0 (GPL-3.0-or-later)**. Veja o arquivo `LICENSE` para os termos completos.
- **librtlsdr / libusb:** Drivers de código aberto para dispositivos baseados em RTL2832U (GNU GPLv2+ / LGPLv2.1+).
- **MPV:** Player multimídia de alta precisão (GNU GPLv2+).
- **Zadig:** Utilitário para instalação de drivers USB no Windows (GNU GPLv3).
