#!/usr/bin/env python3
"""
Teste de Integracao End-to-End: Varredura (Scan) e Validacao do Comando Play
Executa a varredura de frequencias, identifica todos os canais com sinal alto
('Alto (Forte)', RSSI >= -6.0 dBFS) e valida a recepcao do streaming UDP real
atraves do comando 'play' para cada um desses canais.

Criterio de Aprovacao:
Se ao menos 1 canal com sinal alto funcionar, o teste tera tido sucesso!
"""

import os
import sys
import json
import time
import socket
import argparse
import subprocess

# Cores ANSI
GREEN = "\033[1;32m"
RED = "\033[1;31m"
YELLOW = "\033[1;33m"
CYAN = "\033[1;36m"
RESET = "\033[0m"

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_DIR = os.path.join(PROJECT_ROOT, "build")
EXE_MAIN = os.path.join(BUILD_DIR, "isdbtb_rx.exe")

# Ambiente limpo simulando o terminal nativo do usuario
clean_env = os.environ.copy()
current_path = clean_env.get("PATH", "")
filtered_paths = [p for p in current_path.split(os.pathsep) if "deps" not in p.lower() and "tools/rtl-sdr" not in p.lower()]
clean_env["PATH"] = os.pathsep.join(filtered_paths)

def get_channels_json_path():
    if os.name == 'nt':
        appdata = os.environ.get("APPDATA", "")
        return os.path.join(appdata, "isdbtb_rx", "channels.json")
    else:
        home = os.environ.get("HOME", "")
        return os.path.join(home, ".config", "isdbtb_rx", "channels.json")

def validate_channel_play(channel_num, freq_mhz, rssi_dbfs, timeout_sec=5.0):
    """
    Inicia o comando 'play' para o canal especificado e valida se datagramas UDP
    MPEG-TS validos (1316 bytes, sync byte 0x47) sao recebidos na porta de teste,
    verificando rigorosamente a presenca de pacotes de video e integridade do stream.
    """
    test_port = 12380 + (channel_num % 100)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("127.0.0.1", test_port))
    sock.settimeout(timeout_sec)

    cmd = [EXE_MAIN, "play", "-c", str(channel_num), "-p", str(test_port)]
    
    start_t = time.time()
    proc = subprocess.Popen(
        cmd,
        cwd=PROJECT_ROOT,
        env=clean_env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace"
    )

    received_ok = False
    details = ""
    try:
        total_dgrams = 0
        sync_errors_total = 0
        video_pkts_found = 0
        pcr_found = False

        while time.time() - start_t < timeout_sec:
            try:
                data, _ = sock.recvfrom(2048)
            except socket.timeout:
                break

            total_dgrams += 1
            if len(data) != 1316 or data[0] != 0x47:
                sync_errors_total += 1
                continue

            for i in range(0, 1316, 188):
                pkt = data[i:i+188]
                if pkt[0] != 0x47:
                    sync_errors_total += 1
                    continue
                pid = ((pkt[1] & 0x1F) << 8) | pkt[2]
                afc = (pkt[3] >> 4) & 0x03
                if afc in (2, 3) and pkt[4] > 0:
                    af_flags = pkt[5]
                    if af_flags & 0x10:
                        pcr_found = True
                if pid == 0x0100:
                    video_pkts_found += 1

            if video_pkts_found > 0 and total_dgrams >= 1 and sync_errors_total == 0:
                received_ok = True
                elapsed = time.time() - start_t
                pcr_str = "com PCR 27MHz" if pcr_found else "sem PCR"
                details = f"Stream integro: {total_dgrams} datagramas 1316B recebidos em {elapsed:.2f}s | {video_pkts_found} pacotes de video ({pcr_str})"
                break

        if not received_ok:
            if total_dgrams == 0:
                details = f"Timeout ({timeout_sec:.1f}s) aguardando datagrama UDP"
            elif sync_errors_total > 0:
                details = f"Falha de integridade: {sync_errors_total} pacotes desalinhados"
            else:
                details = f"Recebeu {total_dgrams} datagramas mas nenhum pacote de video (PID 0x0100)"

    except Exception as e:
        details = f"Excecao no socket: {e}"
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            proc.kill()
        sock.close()

    return received_ok, details

def main():
    parser = argparse.ArgumentParser(description="Teste de Varredura e Validacao Play para Canais de Sinal Alto")
    parser.add_argument("--start", type=int, default=20, help="Canal inicial da varredura (padrao: 20)")
    parser.add_argument("--end", type=int, default=32, help="Canal final da varredura (padrao: 32)")
    parser.add_argument("--device", type=int, default=0, help="Indice do dongle RTL-SDR (padrao: 0)")
    args = parser.parse_args()

    print(f"\n{YELLOW}======================================================================{RESET}")
    print(f"{YELLOW}  TESTE E2E: SCAN DE CANAIS E VALIDACAO DO COMANDO PLAY (SINAL ALTO)  {RESET}")
    print(f"{YELLOW}======================================================================{RESET}\n")

    if not os.path.exists(EXE_MAIN):
        print(f"{RED}[ERRO] Executavel '{EXE_MAIN}' nao encontrado. Compile o projeto primeiro.{RESET}\n")
        return 1

    # 1. Executar Scan
    print(f"{CYAN}[PASSO 1/2]{RESET} Executando varredura (scan) nos canais {args.start} a {args.end}...")
    scan_cmd = [EXE_MAIN, "scan", "--start", str(args.start), "--end", str(args.end), "--device", str(args.device)]
    
    t0 = time.time()
    scan_res = subprocess.run(
        scan_cmd,
        cwd=PROJECT_ROOT,
        env=clean_env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace"
    )
    t_scan = time.time() - t0

    if scan_res.returncode != 0:
        print(f"{RED}[FALHA] O comando scan falhou com codigo {scan_res.returncode}!{RESET}")
        print(scan_res.stderr)
        return 1

    print(f"{GREEN}[OK]{RESET} Varredura concluida em {t_scan:.2f}s.\n")

    # 2. Carregar canais detectados de channels.json
    json_path = get_channels_json_path()
    if not os.path.exists(json_path):
        print(f"{RED}[FALHA] channels.json nao foi encontrado em '{json_path}'.{RESET}\n")
        return 1

    with open(json_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    channels = data.get("channels", [])
    print(f"Total de canais sintonizados no channels.json: {len(channels)}")

    # Filtrar canais com sinal alto: RSSI >= -6.0 dBFS ('Alto (Forte)')
    high_signal_channels = [
        c for c in channels
        if c.get("rssi_dbfs", -99.0) >= -6.0
    ]

    print(f"Canais com Sinal Alto ('Alto (Forte)', RSSI >= -6.0 dBFS): {len(high_signal_channels)}\n")

    if not high_signal_channels:
        print(f"{YELLOW}[AVISO] Nenhum canal com sinal alto (>= -6.0 dBFS) foi detectado na faixa testada.{RESET}")
        print("Tente expandir a faixa de varredura (ex: --start 14 --end 45) ou posicionar melhor a antena.")
        return 0

    # 3. Validar comando play para cada canal de sinal alto
    print(f"{CYAN}[PASSO 2/2]{RESET} Validando comando 'play' para cada canal de sinal alto:")
    print("----------------------------------------------------------------------")

    successful_channels = []
    failed_channels = []

    for c in high_signal_channels:
        ch_num = c["channel"]
        freq_mhz = c["frequency_hz"] / 1e6
        rssi = c.get("rssi_dbfs", 0.0)
        name = c.get("name", f"Canal {ch_num}")

        print(f"  Validando Canal {ch_num:02d} ({name} | {freq_mhz:.3f} MHz | RSSI: {rssi:.1f} dBFS) ... ", end="", flush=True)

        ok, msg = validate_channel_play(ch_num, freq_mhz, rssi, timeout_sec=5.0)

        if ok:
            print(f"{GREEN}PASS{RESET}")
            print(f"    -> {GREEN}{msg}{RESET}")
            successful_channels.append(ch_num)
        else:
            print(f"{RED}FAIL{RESET}")
            print(f"    -> {RED}{msg}{RESET}")
            failed_channels.append(ch_num)

    # 4. Avaliacao do Resultado Final
    print(f"\n{YELLOW}======================================================================{RESET}")
    print(f"Canais Testados (Sinal Alto) : {len(high_signal_channels)}")
    print(f"Canais com Play Funcional    : {GREEN}{len(successful_channels)}{RESET} {successful_channels}")
    print(f"Canais com Falha no Play     : {RED}{len(failed_channels)}{RESET} {failed_channels}")
    print(f"{YELLOW}======================================================================{RESET}\n")

    # Regra definida pelo usuario: "se ao menos 1 funcionar o teste terá tido sucesso"
    if len(successful_channels) >= 1:
        print(f"{GREEN}>>> [SUCESSO TOTAL] Ao menos 1 canal com sinal alto funcionou perfeitamente! <<<{RESET}\n")
        return 0
    else:
        print(f"{RED}>>> [FALHA] Nenhum canal com sinal alto teve transmissao funcional no play! <<<{RESET}\n")
        return 1

if __name__ == "__main__":
    sys.exit(main())

