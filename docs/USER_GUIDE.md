# Guia do Usuário
Uso detalhado do receptor: hardware, cada comando com exemplos de saída e solução de problemas. Para o resumo rápido, veja o [README](../README.md); para todas as opções, a [Referência da CLI](user/cli_reference.md).

## 1. Hardware e driver
- **Dongle:** RTL-SDR (RTL2832U + R820T/R828D/E4000).
- **Antena:** UHF (470–700 MHz). Uma haste telescópica com ~12–15 cm (¼ de onda em ~550 MHz) funciona bem. Perto da janela ou externa.
- **Driver WinUSB:** o Windows instala por padrão um driver que bloqueia o acesso às amostras I/Q. Troque-o com o Zadig (`zadig.exe` na release, `tools\zadig.exe` no repositório):
  1. Conecte o dongle e abra o Zadig como Administrador.
  2. **Options → List All Devices**.
  3. Selecione `Bulk-In, Interface (Interface 0)` (ou `RTL2832U` / `RTL2838UHIDIR`). ⚠ Sempre a **Interface 0**; a Interface 1 é o receptor infravermelho.
  4. Driver de destino **WinUSB** → **Install Driver** / **Replace Driver**.

## 2. Comandos
Abra o PowerShell ou o CMD na pasta do programa.

### 2.1 `probe`: verificar o dongle
```powershell
.\isdbtb_rx.exe probe
```
```text
[RTL-SDR] Dispositivos conectados: 1
  - Dongle #0: Generic RTL2832U OEM
    Fabricante: Realtek | Serial: 00000001
    Ganhos de RF suportados (29): 0 0.9 1.4 2.7 3.7 7.7 8.7 ... 49.6 dB
[SIMD/DSP] VOLK Versao 3.1.2
  - Aceleracao AVX2: ATIVADA
```

### 2.2 `scan`: procurar canais
Em cada canal, faz uma triagem rápida de RF (~35 ms). Nos canais com sinal, lê o nome da emissora pela tabela SDT (até 30 s). O resultado é salvo em `./channels.json`.
```powershell
.\isdbtb_rx.exe scan                 # canais 14 a 45 (padrão)
.\isdbtb_rx.exe scan -s 20 -e 25     # faixa específica
.\isdbtb_rx.exe scan -e 69           # até o canal 69
```

### 2.3 `list`: canais salvos
```powershell
.\isdbtb_rx.exe list
```
```text
Canal | Frequencia   |   Forca do Sinal     |  Potencia   | Codecs (V/A) | Nome / Emissora
  23  |  527.143 MHz | [#####] Alto (Forte) |   -1.8 dBFS |    H.264/AAC | BAND HD
  28  |  557.143 MHz | [#####] Alto (Forte) |    0.5 dBFS |    H.264/AAC | Canal 28
```
Força: `[#####]` Alto ≥ -6 dBFS, `[#### ]` Normal ≥ -12 dBFS, `[###  ]` Moderado, `[##   ]` Fraco.

### 2.4 `play`: assistir
```powershell
.\isdbtb_rx.exe play -c 23 --player      # pelo número, abrindo o MPV
.\isdbtb_rx.exe play -n "BAND HD" --player
.\isdbtb_rx.exe play --player            # primeiro canal salvo
```
- **Escolha do canal:** `-c` (número) ou `-n` (nome salvo). Um `-c` que não está salvo é sintonizado mesmo assim.
- **Interface:** a lista de canais salvos fica no terminal. Use `▲`/`▼` (ou `k`/`j`) e `Enter` para trocar de canal sem fechar o player; `q` ou `Ctrl+C` encerra. O MPV aberto com `--player` fecha junto, porque está ligado ao programa por um Job Object do Windows.
- **Status em tempo real (`-v`):**
  ```text
  [Status] Ch 23 | RSSI: -6.9 dBFS | OFDM: LOCK (0.98) | TMCC: LOCK | SNR: 18.5 dB | TS: 542 pkts (RS: 0 corr / 0 drop) | Drop: 0 | BAND HD
  ```
  `TMCC: LOCK` e `TS` subindo indicam que o vídeo está chegando.
- **Modo de sincronismo (`--ofdm`):**

  | Modo | Quando usar |
  |---|---|
  | `--ofdm 1` (padrão) | Uso diário, sinal estável, notebook economizando bateria |
  | `--ofdm 2` | Reflexos em prédios, sinal fraco ou ruidoso, antena interna difícil, em movimento. Usa mais CPU |

  Aceita `--ofdm 2` ou `--ofdm=2`. Só vale no `play`; nos outros comandos é ignorada.
- **Ganho (`-g`, padrão 28 dB):** longe da torre use 38, 42 ou 49.6; perto, 20 (evita saturar o conversor).
- **Player MPV:** vem configurado para o 1-Seg (320x240/320x180): ampliação EWA-Lanczos, filtro anti-blocos (deband) e interpolação de movimento na taxa do monitor (60–240 Hz). Teclas: `f` tela cheia, `i` estatísticas, `d` deband, `m` mudo, `9`/`0` volume, `q` fecha.
- **Outro player:** rode `play` sem `--player`.
  - MPV manual (repositório): `.\scripts\play_mpv.bat`.
  - VLC: **Mídia → Abrir Fluxo de Rede** (`Ctrl+N`) → `udp://@127.0.0.1:1234`, cache de rede 2000 ms. O título do VLC mostra a URL sem o `@`; é normal.

### 2.5 `epg`: programação
```powershell
.\isdbtb_rx.exe epg                  # captura todos os canais salvos
.\isdbtb_rx.exe epg -c 23            # um canal
.\isdbtb_rx.exe epg --view           # só mostra o que já foi salvo (sem rádio)
.\isdbtb_rx.exe epg -c 23 -f         # força nova captura, ignorando o cache
.\isdbtb_rx.exe epg --view --json    # exporta em JSON
```
A grade fica salva em `./epg.json`. Textos em ARIB STD-B24 (incluindo japonês), UTF-8 e Latin-1 são decodificados. Na grade, `▲`/`▼` seleciona o programa e `q` sai.

## 3. Solução de problemas
| Sintoma | Causa provável | Solução |
|---|---|---|
| "Nenhum dispositivo RTL-SDR encontrado" / `rtlsdr_open failed` | Driver errado ou USB instável | Refaça o Zadig (Interface 0 + WinUSB); porta USB traseira, sem hub |
| `[R82XX] PLL not locked!` | Queda de tensão na USB ou antena em curto | Conecte direto na placa-mãe; verifique o cabo (o programa já espera 100 ms pelo PLL) |
| `scan` sem canais / "OFDM: Ruído" | Sinal fraco ou antena mal posicionada | Antena perto da janela, haste de 12–15 cm, `scan -e 69`, ou teste `play -c <canal> -g 38` |
| Imagem preta ou travada no MPV | Sinal ainda não sincronizado | Aguarde `TMCC: LOCK` (1–2 s); use `-v`; tente `--ofdm 2` ou ajuste `-g` |

## 4. Compilar a partir do código
Veja [Compilação e testes](user/installation.md).
