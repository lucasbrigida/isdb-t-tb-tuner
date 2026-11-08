import csv
import statistics
import os

def load_metrics(csv_path):
    rows = []
    if not os.path.exists(csv_path):
        return rows
    with open(csv_path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append({
                "timestamp_s": int(r["timestamp_s"]),
                "rssi_dbfs": float(r["rssi_dbfs"]),
                "peak_corr": float(r["peak_corr"]),
                "ofdm_locked": int(r["ofdm_locked"]),
                "tmcc_locked": int(r["tmcc_locked"]),
                "snr_db": float(r["snr_db"]),
                "evm": float(r["evm"]),
                "valid_ts_packets": int(r["valid_ts_packets"]),
                "rs_corrected": int(r["rs_corrected"]),
                "rs_dropped": int(r["rs_dropped"]),
                "spsc_drops": int(r["spsc_drops"]),
                "cfo_hz": float(r["cfo_hz"]),
                "sco_ppm": float(r["sco_ppm"]),
                "sync_state": r["sync_state"]
            })
    return rows

def parse_mpv_log(log_path):
    stats = {
        "corrupt_packets": 0,
        "pes_mismatch": 0,
        "audio_errors": 0,
        "total_lines": 0
    }
    if not os.path.exists(log_path):
        return stats
    with open(log_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            stats["total_lines"] += 1
            if "Packet corrupt" in line:
                stats["corrupt_packets"] += 1
            if "PES packet size mismatch" in line:
                stats["pes_mismatch"] += 1
            if "Error decoding audio" in line or "channel element" in line:
                stats["audio_errors"] += 1
    return stats

old_csv = "outputs/ch28_15min_diagnostics/metrics_ch28_15min.csv"
new_csv = "outputs/ch28_15min_sp_cir_eval/metrics_ch28_15min.csv"

old_rows = load_metrics(old_csv)
new_rows = load_metrics(new_csv)

old_mpv = parse_mpv_log("outputs/ch28_15min_diagnostics/mpv_decoder_15min.log")
new_mpv = parse_mpv_log("outputs/ch28_15min_sp_cir_eval/mpv_decoder_15min.log")

print("================================================================================")
print("  AVALIAÇÃO COMPARATIVA DE 15 MINUTOS NO CANAL 28 (OFDM 2: ANTES vs DEPOIS)")
print("================================================================================")

def summarize(name, rows, mpv_stats):
    n = len(rows)
    if n == 0:
        print(f"Sem dados para {name}")
        return
    rssi = [r["rssi_dbfs"] for r in rows]
    snr = [r["snr_db"] for r in rows]
    corr = [r["peak_corr"] for r in rows]
    ofdm_lock = sum(1 for r in rows if r["ofdm_locked"] == 1)
    tmcc_lock = sum(1 for r in rows if r["tmcc_locked"] == 1)
    
    first_drop = None
    for r in rows:
        if r["rs_dropped"] > 0:
            first_drop = r["timestamp_s"]
            break
            
    print(f"--- {name} ---")
    print(f"  Amostras registradas:        {n} segundos ({n/60:.1f} min)")
    print(f"  RSSI (dBFS):                 Méd: {statistics.mean(rssi):.2f} | Mín: {min(rssi):.2f} | Máx: {max(rssi):.2f}")
    print(f"  SNR (dB):                    Méd: {statistics.mean(snr):.2f} | Mín: {min(snr):.2f} | Máx: {max(snr):.2f}")
    print(f"  Pico de Correlação CP:       Méd: {statistics.mean(corr):.3f} | Mín: {min(corr):.3f} | Máx: {max(corr):.3f}")
    print(f"  OFDM Lock:                   {ofdm_lock/n*100:.1f}% ({ofdm_lock}/{n}s)")
    print(f"  TMCC Lock:                   {tmcc_lock/n*100:.1f}% ({tmcc_lock}/{n}s)")
    print(f"  Pacotes TS Emitidos:         {rows[-1]['valid_ts_packets']:,} pkts")
    print(f"  Reed-Solomon Corrigidos:     {rows[-1]['rs_corrected']:,} pkts")
    print(f"  Reed-Solomon Descartados:    {rows[-1]['rs_dropped']:,} pkts")
    print(f"  Drops Fila USB (SPSC):       {rows[-1]['spsc_drops']}")
    print(f"  Primeiro Descarte de RS:     {first_drop}s (após {first_drop/60:.1f} min)" if first_drop else "  Primeiro Descarte de RS:     NENHUM (Zero descartes!)")
    print(f"  Erros MPV (Packet corrupt):  {mpv_stats['corrupt_packets']}")
    print(f"  Erros MPV (PES mismatch):    {mpv_stats['pes_mismatch']}")
    print(f"  Erros MPV (Áudio decodif.):  {mpv_stats['audio_errors']}")
    print()

summarize("BASELINE ANTERIOR (--ofdm 2 sem refinamento SP/CIR)", old_rows, old_mpv)
summarize("NOVO MOTOR (--ofdm 2 COM REFINAMENTO SP/CIR + PAPR >= 12)", new_rows, new_mpv)

# Cálculo de ganhos percentuais
if old_rows and new_rows:
    old_drop = old_rows[-1]["rs_dropped"]
    new_drop = new_rows[-1]["rs_dropped"]
    old_pkts = old_rows[-1]["valid_ts_packets"]
    new_pkts = new_rows[-1]["valid_ts_packets"]
    print("================================================================================")
    print("  RESULTADO DO GANHO REAL (BENEFÍCIO QUANTITATIVO)")
    print("================================================================================")
    if old_drop > 0:
        red_drop = ((old_drop - new_drop) / old_drop) * 100.0
        print(f"  Variação de descartes RS:    {old_drop:,} -> {new_drop:,} ({red_drop:+.2f}%)")
    print(f"  Variação de pacotes TS:      {old_pkts:,} -> {new_pkts:,} ({((new_pkts-old_pkts)/old_pkts)*100.0:+.2f}%)")
    print(f"  Variação erros corrupt MPV:  {old_mpv['corrupt_packets']} -> {new_mpv['corrupt_packets']}")
    print(f"  Variação erros PES mismatch: {old_mpv['pes_mismatch']} -> {new_mpv['pes_mismatch']}")
    print("================================================================================")
