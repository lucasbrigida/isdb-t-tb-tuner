# ISDB-T / ISDB-Tb Tuner Player (Windows)
TV Digital aberta (ISDB-T / ISDB-Tb / SBTVD, sinal 1-Seg) no computador com dongle **RTL-SDR**. Programa portátil: tudo o que precisa está nesta pasta.

**Conteúdo:** `isdbtb_rx.exe` (programa), DLLs do RTL-SDR, `zadig.exe` (driver USB), `tools\mpv\` (player), `LICENSE`. Requer Windows 10 (1607+) ou 11.

## Primeiro uso
1. Conecte o dongle RTL-SDR e a antena UHF.
2. **Driver (só na primeira vez):** abra `zadig.exe` (aceite o pedido de Administrador) → **Options → List All Devices** → escolha **`Bulk-In, Interface (Interface 0)`** (ou `RTL2832U` / `RTL2838UHIDIR`) → driver **WinUSB** → **Install Driver** (ou **Replace Driver**) → espere *"The driver was installed successfully"*.
   > ⚠ Se aparecerem Interface 0 e Interface 1, use sempre a **Interface 0** (a 1 é só o controle remoto infravermelho).
3. Abra o PowerShell nesta pasta (Shift + botão direito → *Abrir janela do PowerShell aqui*) e rode:
   ```powershell
   .\isdbtb_rx.exe scan
   .\isdbtb_rx.exe play -c 23 --player
   ```
   Troque `23` por um canal encontrado no `scan` (veja com `.\isdbtb_rx.exe list`).

## Comandos
| Comando | Para que serve |
|---|---|
| `probe` | Verifica se o dongle foi reconhecido |
| `scan` | Procura emissoras (UHF 14–45) e salva em `channels.json` |
| `list` | Mostra os canais salvos e a força do sinal |
| `play` | Sintoniza um canal e abre o vídeo |
| `epg` | Mostra a programação das emissoras |

```powershell
.\isdbtb_rx.exe probe                          # o dongle está funcionando?
.\isdbtb_rx.exe scan                           # procurar canais
.\isdbtb_rx.exe scan -s 14 -e 69               # procurar em outra faixa
.\isdbtb_rx.exe list                           # ver canais salvos
.\isdbtb_rx.exe play -c 23 --player            # assistir ao canal 23
.\isdbtb_rx.exe play -n GLOBO --player         # assistir pelo nome
.\isdbtb_rx.exe play -c 23 --ofdm 2 --player   # sinal fraco, reflexos ou em movimento
.\isdbtb_rx.exe play -c 23 -g 42 --player      # mais ganho (longe da torre)
.\isdbtb_rx.exe play -c 23 --player -v         # mostrar qualidade do sinal
.\isdbtb_rx.exe epg                            # programação de todos os canais
.\isdbtb_rx.exe epg -c 23                      # programação de um canal
.\isdbtb_rx.exe epg --view                     # programação salva, sem usar o rádio
.\isdbtb_rx.exe epg --view --json              # exportar em JSON
```

Opções do `play`:

| Opção | Padrão | Efeito |
|---|---|---|
| `-c <n>` / `-n <nome>` | 1º canal salvo | Canal por número ou nome |
| `--player` | desligado | Abre o MPV integrado |
| `--ofdm 1\|2` | 1 | `2` = modo reforçado (mais CPU). Aceita `--ofdm=2` |
| `-g <dB>` | 28 | Ganho: longe da torre 38–49.6; perto 20 |
| `-v` | desligado | Mostra RSSI, OFDM, TMCC, SNR e pacotes em tempo real |
| `-p <porta>` | 1234 | Porta UDP |
| `-d <n>` | 0 | Qual dongle, se houver mais de um |

Ajuda de qualquer comando: `.\isdbtb_rx.exe <comando> --help`. Os arquivos `channels.json` e `epg.json` são criados nesta pasta.

## Teclas
| Onde | Tecla | Ação |
|---|---|---|
| Terminal (`play`) | `▲`/`▼`, `Enter` | Escolher e sintonizar outro canal salvo |
| Terminal | `q` ou `Ctrl+C` | Sair (o MPV fecha junto) |
| MPV | `f` | Tela cheia |
| MPV | `i` | Estatísticas do vídeo |
| MPV | `m` / `9` / `0` | Mudo / volume − / volume + |
| MPV | `b` | Liga/desliga o filtro anti-blocos (deband) |
| MPV | `q` | Fecha o player |

O MPV já vem configurado para o 1-Seg (320x240): baixa latência, ampliação da imagem e filtro anti-blocos. A versão 64 bits usa ampliação de alta qualidade (EWA-Lanczos); a 32 bits usa um perfil leve para PCs antigos.

## Problemas comuns
**`probe` diz "Nenhum dispositivo RTL-SDR encontrado" / `rtlsdr_open failed`**
Reconecte o dongle. Refaça o Zadig (Interface 0 + WinUSB). Evite hub ou porta frontal; use uma porta USB traseira da placa-mãe.

**`[R82XX] PLL not locked!`**
Ligue o dongle direto na placa-mãe (queda de tensão na USB) e confira se o cabo da antena não está em curto.

**`scan` não acha canais**
Antena perto da janela ou externa (no chão ela perde muito sinal). Haste telescópica com cerca de 12–15 cm. Tente `scan -e 69`. Se souber o canal, teste direto: `play -c <canal> -g 38 --player`. Longe da torre use `-g 42` ou `-g 49.6`; perto, `-g 20`.

**MPV abre com imagem preta ou travada**
Aguarde `TMCC: LOCK` no terminal; a imagem leva 1–2 s para aparecer. Use `-v` para ver o sinal. Com muitos reflexos, tente `--ofdm 2`.

**Quero usar o VLC**
Rode `.\isdbtb_rx.exe play -c 23` (sem `--player`). No VLC: **Mídia → Abrir Fluxo de Rede** (`Ctrl+N`) → `udp://@127.0.0.1:1234`. Em "Exibir mais opções", ponha o cache de rede em 2000 ms. O título da janela do VLC mostra a URL sem o `@`; isso é normal.

## Mais informações
- Projeto e novas versões: https://github.com/lucasbrigida/isdb_t_isdb_tb_tuner_player
- Todas as opções: https://github.com/lucasbrigida/isdb_t_isdb_tb_tuner_player/blob/main/docs/user/cli_reference.md

## Licença
GPL-3.0-or-later (veja `LICENSE`). Componentes: librtlsdr/libusb (GPLv2+/LGPLv2.1+), MPV (GPLv2+), Zadig (GPLv3).
