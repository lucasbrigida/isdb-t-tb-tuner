import sys

ts_file = "outputs/ch28_diagnostics/stream_ch28_ofdm2.ts"

with open(ts_file, "rb") as f:
    data = f.read()

total_bytes = len(data)
total_packets = total_bytes // 188
print(f"Total de bytes gravados: {total_bytes} ({total_bytes / (1024*1024):.2f} MB)")
print(f"Total de pacotes TS de 188B: {total_packets}")

pids = {}
cc_per_pid = {}
cc_errors = {}
sync_errors = 0

for i in range(total_packets):
    pkt = data[i*188 : (i+1)*188]
    if pkt[0] != 0x47:
        sync_errors += 1
        continue
    
    pid = ((pkt[1] & 0x1F) << 8) | pkt[2]
    cc = pkt[3] & 0x0F
    has_adaptation = (pkt[3] & 0x20) != 0
    has_payload = (pkt[3] & 0x10) != 0
    
    pids[pid] = pids.get(pid, 0) + 1
    
    if has_payload:
        if pid in cc_per_pid:
            expected_cc = (cc_per_pid[pid] + 1) & 0x0F
            if cc != expected_cc:
                # Checar se não é duplicação de pacote
                if cc != cc_per_pid[pid]:
                    cc_errors[pid] = cc_errors.get(pid, 0) + 1
        cc_per_pid[pid] = cc

print(f"\nErros de byte de sincronismo (0x47): {sync_errors}")
print("\nDistribuicao de Pacotes por PID:")
for pid, count in sorted(pids.items()):
    name = "Desconhecido"
    if pid == 0x0000: name = "PAT (Program Association Table)"
    elif pid == 0x1FC8: name = "PMT 1-Seg (Program Map Table)"
    elif pid == 0x0111: name = "Vídeo H.264"
    elif pid == 0x0112: name = "Áudio AAC"
    elif pid == 0x1FFF: name = "Pacotes Nulos (Padding)"
    elif pid == 0x0011: name = "SDT (Service Description Table)"
    elif pid == 0x0012: name = "EIT (Event Information Table)"
    
    errors = cc_errors.get(pid, 0)
    pct = (count / total_packets) * 100
    print(f"  PID 0x{pid:04X} ({pid:5d}): {count:6d} pkts ({pct:5.2f}%) | CC Errors: {errors} | {name}")
