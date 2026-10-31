#!/usr/bin/env python3
"""
Validador Automatizado de Todos os Executaveis em build/
Testa cada binario unitario e todos os subcomandos do isdbtb_rx.exe.
"""
import os
import sys
import subprocess
import time
import socket

# Cores ANSI para o terminal
GREEN = "\033[1;32m"
RED = "\033[1;31m"
YELLOW = "\033[1;33m"
CYAN = "\033[1;36m"
RESET = "\033[0m"

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_DIR = os.path.join(PROJECT_ROOT, "build")

# O teste executa estritamente no ambiente padrao do usuario (SEM injecao de DLLs externas no PATH)
# Isso garante que a pasta build/ seja 100% autocontida e portavel.
clean_env = os.environ.copy()
# Remove qualquer referencia a deps/ ou build-setup/ do PATH para simular fielmente o terminal do usuario
current_path = clean_env.get("PATH", "")
filtered_paths = [p for p in current_path.split(os.pathsep) if "deps" not in p.lower() and "build-setup" not in p.lower()]
clean_env["PATH"] = os.pathsep.join(filtered_paths)

total_tests = 0
passed_tests = 0
failed_tests = 0

def run_test(name, cmd, timeout=15, expected_exit=0, check_fn=None):
    global total_tests, passed_tests, failed_tests
    total_tests += 1
    print(f"{CYAN}[TEST {total_tests:02d}]{RESET} {name} ... ", end="", flush=True)

    start_time = time.time()
    try:
        proc = subprocess.run(
            cmd,
            cwd=PROJECT_ROOT,
            env=clean_env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            text=True,
            encoding="utf-8",
            errors="replace"
        )
        elapsed = time.time() - start_time

        exit_ok = (proc.returncode == expected_exit) if expected_exit is not None else True
        output_ok = True
        err_msg = ""

        if not exit_ok:
            output_ok = False
            err_msg = f"Codigo de saida incorreto: {proc.returncode} (esperado {expected_exit})"

        if exit_ok and check_fn:
            output_ok, err_msg = check_fn(proc.stdout, proc.stderr)

        if output_ok:
            passed_tests += 1
            print(f"{GREEN}PASS{RESET} ({elapsed:.2f}s)")
            return True
        else:
            failed_tests += 1
            print(f"{RED}FAIL{RESET} ({elapsed:.2f}s)")
            print(f"  {RED}-> {err_msg}{RESET}")
            if proc.stdout.strip():
                print(f"  [Stdout]: {proc.stdout.strip()[:200]}...")
            if proc.stderr.strip():
                print(f"  [Stderr]: {proc.stderr.strip()[:200]}...")
            return False

    except subprocess.TimeoutExpired:
        elapsed = time.time() - start_time
        failed_tests += 1
        print(f"{RED}TIMEOUT{RESET} ({elapsed:.2f}s)")
        print(f"  {RED}-> Processo excedeu o limite de {timeout}s{RESET}")
        return False
    except Exception as e:
        failed_tests += 1
        print(f"{RED}ERROR{RESET}")
        print(f"  {RED}-> Excecao: {e}{RESET}")
        return False

def main():
    print(f"\n{YELLOW}================================================================{RESET}")
    print(f"{YELLOW}  SUITE DE TESTES INTEGRADA: TODOS OS EXECUTAVEIS EM build/     {RESET}")
    print(f"{YELLOW}================================================================{RESET}\n")

    # 1. Testes Unitarios Deterministas
    exe_reed_solomon = os.path.join(BUILD_DIR, "test_reed_solomon.exe")
    run_test(
        "test_reed_solomon.exe (Outer FEC RS 204,188)",
        [exe_reed_solomon],
        check_fn=lambda out, err: ("Todos os testes do Reed-Solomon passaram" in out, "Mensagem de sucesso nao encontrada")
    )

    exe_descrambler = os.path.join(BUILD_DIR, "test_descrambler.exe")
    run_test(
        "test_descrambler.exe (Dispersao de Energia PRBS)",
        [exe_descrambler],
        check_fn=lambda out, err: ("Todos os testes do Descrambler passaram" in out, "Mensagem de sucesso nao encontrada")
    )

    exe_viterbi = os.path.join(BUILD_DIR, "test_viterbi.exe")
    run_test(
        "test_viterbi.exe (Inner FEC Convolucional K=7)",
        [exe_viterbi],
        check_fn=lambda out, err: ("Todos os testes do Viterbi Decoder passaram" in out, "Mensagem de sucesso nao encontrada")
    )

    exe_fec_pipeline = os.path.join(BUILD_DIR, "test_fec_pipeline.exe")
    run_test(
        "test_fec_pipeline.exe (Cadeia FEC Integrada)",
        [exe_fec_pipeline],
        check_fn=lambda out, err: ("Pacote MPEG-TS de 188 bytes recuperado intacto" in out, "Cadeia FEC falhou")
    )

    exe_ofdm_sync = os.path.join(BUILD_DIR, "test_ofdm_sync.exe")
    run_test(
        "test_ofdm_sync.exe (Sincronismo OFDM Prefixo Ciclico)",
        [exe_ofdm_sync],
        check_fn=lambda out, err: ("SIM!" in out and "Pico:" in out, "Sincronismo nao travou nas amostras reais")
    )

    # 2. Testes dos Subcomandos de isdbtb_rx.exe
    exe_main = os.path.join(BUILD_DIR, "isdbtb_rx.exe")

    # Subcomando --help
    run_test(
        "isdbtb_rx.exe --help (Exibicao do menu principal)",
        [exe_main, "--help"],
        check_fn=lambda out, err: ("Subcommands:" in out and "probe" in out and "play" in out, "Menu de ajuda incompleto")
    )

    # Subcomando sem argumentos (deve falhar amigavelmente)
    run_test(
        "isdbtb_rx.exe sem argumentos (Validacao de exigencia de subcomando)",
        [exe_main],
        expected_exit=None,
        check_fn=lambda out, err: (True if ("A subcommand is required" in out or "A subcommand is required" in err or "Usage:" in out) else False, "Deveria exigir subcomando")
    )

    # Subcomando probe
    run_test(
        "isdbtb_rx.exe probe (Inspecao de RTL-SDR e Aceleracao DSP)",
        [exe_main, "probe"],
        check_fn=lambda out, err: ("Dispositivos conectados:" in out and "VOLK Versao" in out, "Probe nao retornou status do hardware/DSP")
    )

    # Subcomando scan (faixa reduzida de teste: canais 28 a 30)
    run_test(
        "isdbtb_rx.exe scan --start 28 --end 30 (Varredura de frequencias)",
        [exe_main, "scan", "--start", "28", "--end", "30"],
        timeout=25,
        check_fn=lambda out, err: ("Varredura concluida" in out, "Varredura nao foi concluida")
    )

    # Subcomando list
    run_test(
        "isdbtb_rx.exe list (Listagem de canais em channels.json)",
        [exe_main, "list"],
        check_fn=lambda out, err: ("Lista de Canais ISDB-T Armazenados" in out, "Tabela de canais nao exibida")
    )

    # Subcomando export (M3U playlist)
    m3u_test_file = os.path.join(BUILD_DIR, "test_channels.m3u")
    def check_export(out, err):
        if not os.path.exists(m3u_test_file):
            return False, "Arquivo M3U nao foi criado no disco"
        with open(m3u_test_file, "r") as f:
            content = f.read()
        if "#EXTM3U" not in content or "udp://@" not in content:
            return False, "Conteudo M3U invalido"
        return True, ""

    run_test(
        "isdbtb_rx.exe export (Geracao de Playlist M3U)",
        [exe_main, "export", "-o", m3u_test_file],
        check_fn=check_export
    )

    # Subcomando play (streaming UDP ao vivo com receptor de socket para validacao)
    global total_tests, passed_tests, failed_tests
    total_tests += 1
    test_name = "isdbtb_rx.exe play -c 30 (Streaming UDP e emissao de pacotes TS)"
    print(f"{CYAN}[TEST {total_tests:02d}]{RESET} {test_name} ... ", end="", flush=True)

    test_port = 12399
    # Abrir socket UDP local para escutar os pacotes emitidos pelo isdbtb_rx
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", test_port))
    sock.settimeout(4.0)

    start_t = time.time()
    proc = subprocess.Popen(
        [exe_main, "play", "-c", "30", "-p", str(test_port), "-t"],
        cwd=PROJECT_ROOT,
        env=clean_env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace"
    )

    received_data = False
    try:
        data, addr = sock.recvfrom(2048)
        if len(data) == 1316 and data[0] == 0x47:
            received_data = True
    except socket.timeout:
        received_data = False
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
        sock.close()

    elapsed = time.time() - start_t
    if received_data:
        passed_tests += 1
        print(f"{GREEN}PASS{RESET} ({elapsed:.2f}s) [Datagrama 1316B recebido com sync 0x47]")
    else:
        failed_tests += 1
        print(f"{RED}FAIL{RESET} ({elapsed:.2f}s) [Nao recebeu datagrama UDP de 1316B valido]")

    # Resumo Final
    print(f"\n{YELLOW}================================================================{RESET}")
    print(f"Total de Testes: {total_tests} | {GREEN}Aprovados: {passed_tests}{RESET} | {RED}Falhas: {failed_tests}{RESET}")
    print(f"{YELLOW}================================================================{RESET}\n")

    if failed_tests == 0:
        print(f"{GREEN}>>> SUCESSO TOTAL: 100% dos executaveis e funcoes validados! <<<{RESET}\n")
        return 0
    else:
        print(f"{RED}>>> ATENCAO: Houve {failed_tests} falha(s) na suite de testes! <<<{RESET}\n")
        return 1

if __name__ == "__main__":
    sys.exit(main())
