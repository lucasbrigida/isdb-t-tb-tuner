# Referência da CLI (`isdbtb_rx.exe`)
Todas as opções de cada comando. Para o passo a passo de instalação e uso, veja o [Guia do Usuário](../USER_GUIDE.md).

```text
isdbtb_rx.exe <comando> [opções]
isdbtb_rx.exe <comando> --help
```

| Comando | Para que serve |
|---|---|
| [`probe`](#probe) | Verifica se o dongle RTL-SDR foi reconhecido e mostra ganhos suportados e aceleração SIMD |
| [`scan`](#scan) | Procura emissoras na faixa UHF e salva em `channels.json` (com o nome de cada uma) |
| [`list`](#list) | Mostra os canais salvos, com frequência, força do sinal e nome |
| [`play`](#play) | Sintoniza um canal e transmite o vídeo para o player (MPV integrado ou VLC) |
| [`epg`](#epg) | Mostra a programação (guia eletrônico) das emissoras |

Opção comum: `-d, --device <n>` escolhe o dongle quando há mais de um (padrão `0`; não existe em `probe` e `list`).

## probe
Sem opções.
```powershell
.\isdbtb_rx.exe probe
```

## scan
Varredura em dois passos: triagem rápida de RF (~35 ms/canal) e depois leitura do nome da emissora (tabela SDT). O resultado vai para `./channels.json`.

| Opção | Padrão | Descrição |
|---|---|---|
| `-s, --start <canal>` | 14 | Canal UHF inicial |
| `-e, --end <canal>` | 45 | Canal UHF final (o Brasil usa até o 51; para ir além, ex.: `-e 69`) |
| `-t, --timeout <s>` | 30 | Tempo máximo para descobrir o nome de cada canal com sinal |
| `-d, --device <n>` | 0 | Índice do dongle |

```powershell
.\isdbtb_rx.exe scan
.\isdbtb_rx.exe scan -s 20 -e 30
.\isdbtb_rx.exe scan -e 69
```

## list
Sem opções. Força do sinal: `[#####]` Alto ≥ -6 dBFS, `[#### ]` Normal ≥ -12 dBFS, `[###  ]` Moderado, `[##   ]` Fraco.
```powershell
.\isdbtb_rx.exe list
```

## play
Sintoniza e transmite o MPEG-TS via UDP para `udp://@127.0.0.1:<porta>`.
Qual canal toca: `-c` (número) ou `-n` (nome salvo). Sem os dois, toca o primeiro canal de `channels.json`. Um `-c` que não está salvo é sintonizado mesmo assim, pela tabela de frequências UHF.

| Opção | Padrão | Descrição |
|---|---|---|
| `-c, --channel <n>` | — | Número do canal UHF (ex.: `23`) |
| `-n, --name <nome>` | — | Nome da emissora salva (ex.: `GLOBO`) |
| `--player` | desligado | Abre o MPV integrado automaticamente |
| `-g, --gain <dB>` | 28.0 | Ganho do sintonizador. Mais alto para sinal fraco (38–49.6); mais baixo perto da torre (20) |
| `--ofdm <1\|2>` | 1 | `1` = leve (padrão). `2` = reforçado: sinal fraco, reflexos ou em movimento; usa mais CPU. Aceita `--ofdm=2` |
| `-v, --verbose` | desligado | Mostra a linha de status em tempo real (RSSI, OFDM, TMCC, SNR, pacotes TS, correções RS) |
| `-p, --port <porta>` | 1234 | Porta UDP local |
| `-d, --device <n>` | 0 | Índice do dongle |

Opções avançadas (diagnóstico e testes):

| Opção | Descrição |
|---|---|
| `-i, --input <arquivo.cu8>` | Reproduz uma gravação IQ em vez do dongle |
| `--output-ts <arquivo.ts>` | Grava o fluxo MPEG-TS decodificado em arquivo |
| `--duration <s>` | Encerra após N segundos (`0` = contínuo) |
| `--headless` | Sem interface interativa (scripts e benchmarks) |
| `--metrics-csv <arquivo.csv>` | Grava a telemetria segundo a segundo |
| `-t, --test-pattern` | Envia um padrão de teste para validar a rede e o player |

Teclas no terminal: `▲`/`▼` (ou `k`/`j`) escolhe outro canal salvo, `Enter` sintoniza, `q` ou `Ctrl+C` sai. O MPV aberto com `--player` fecha junto com o programa.

```powershell
.\isdbtb_rx.exe play -c 23 --player
.\isdbtb_rx.exe play -n GLOBO --player
.\isdbtb_rx.exe play -c 23 --ofdm 2 --player -v
.\isdbtb_rx.exe play -c 23 -g 42 --player
.\isdbtb_rx.exe play -i captura.cu8 --output-ts saida.ts --headless --duration 10
```

Sem `--player`, abra no VLC: **Mídia → Abrir Fluxo de Rede** (`Ctrl+N`) → `udp://@127.0.0.1:1234`.

## epg
Captura a programação (EIT Present/Following e Schedule) e guarda em `./epg.json`. Sem `-c`/`-n`, percorre todos os canais salvos.

| Opção | Padrão | Descrição |
|---|---|---|
| `-c, --channel <n>` | todos | Canal UHF |
| `-n, --name <nome>` | — | Nome da emissora salva |
| `--view` | desligado | Só mostra a programação já salva, sem usar o rádio |
| `-f, --refresh` | desligado | Ignora o cache válido e captura de novo |
| `-t, --timeout <s>` | 30 | Tempo de captura por canal |
| `--json` | desligado | Saída em JSON |
| `-d, --device <n>` | 0 | Índice do dongle |

Teclas na grade: `▲`/`▼` (ou `k`/`j`) seleciona o programa, `q` sai.

```powershell
.\isdbtb_rx.exe epg
.\isdbtb_rx.exe epg -c 23
.\isdbtb_rx.exe epg --view
.\isdbtb_rx.exe epg -c 23 --view --json
```

## Arquivos gerados
Ficam na pasta de onde o programa é executado (modo portátil): `channels.json` (canais do `scan`) e `epg.json` (cache de programação).
