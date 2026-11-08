import csv
import statistics

csv_path = "outputs/ch28_15min_diagnostics/metrics_ch28_15min.csv"
rows = []
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

n = len(rows)
rssi_list = [r["rssi_dbfs"] for r in rows]
snr_list = [r["snr_db"] for r in rows]
corr_list = [r["peak_corr"] for r in rows]
cfo_list = [r["cfo_hz"] for r in rows]
sco_list = [r["sco_ppm"] for r in rows]

print("=== RELATÓRIO DE TELEMETRIA RF E DSP (900 SEGUNDOS / 15 MIN / CANAL 28 / OFDM 2) ===")
print(f"Total de segundos registrados: {n}")
print(f"RSSI: Méd={statistics.mean(rssi_list):.2f} dBFS | Mín={min(rssi_list):.2f} dBFS | Máx={max(rssi_list):.2f} dBFS")
print(f"SNR:  Méd={statistics.mean(snr_list):.2f} dB   | Mín={min(snr_list):.2f} dB   | Máx={max(snr_list):.2f} dB")
print(f"Peak Corr: Méd={statistics.mean(corr_list):.3f} | Mín={min(corr_list):.3f} | Máx={max(corr_list):.3f}")

ofdm_lock_count = sum(1 for r in rows if r["ofdm_locked"] == 1)
tmcc_lock_count = sum(1 for r in rows if r["tmcc_locked"] == 1)
print(f"OFDM Lock: {ofdm_lock_count/n*100:.1f}% ({ofdm_lock_count}/{n} s)")
print(f"TMCC Lock: {tmcc_lock_count/n*100:.1f}% ({tmcc_lock_count}/{n} s)")

print(f"CFO (Hz): Inicial={cfo_list[0]:.1f} Hz | Final={cfo_list[-1]:.1f} Hz | Mín={min(cfo_list):.1f} Hz | Máx={max(cfo_list):.1f} Hz")
print(f"SCO (ppm): Inicial={sco_list[0]:.2f} ppm | Final={sco_list[-1]:.2f} ppm | Mín={min(sco_list):.2f} ppm | Máx={max(sco_list):.2f} ppm")

print(f"Fila SPSC Drops: {rows[-1]['spsc_drops']}")
print(f"Pacotes TS emitidos: {rows[-1]['valid_ts_packets']}")
print(f"Reed-Solomon Corrigidos: {rows[-1]['rs_corrected']}")
print(f"Reed-Solomon Descartados (Drop): {rows[-1]['rs_dropped']}")

# Identificar momentos de transição de fase
print("\n--- Linha do Tempo de Degradação e Falhas ---")
first_drop_sec = None
first_tmcc_unlock_sec = None
snr_drop_sec = None

for i, r in enumerate(rows):
    if snr_drop_sec is None and r["snr_db"] < 5.5:
        snr_drop_sec = r["timestamp_s"]
    if first_drop_sec is None and r["rs_dropped"] > 0:
        first_drop_sec = r["timestamp_s"]
    if first_tmcc_unlock_sec is None and r["tmcc_locked"] == 0 and r["timestamp_s"] > 10:
        first_tmcc_unlock_sec = r["timestamp_s"]

print(f"Momento em que o SNR caiu abaixo do limiar (5.5 dB): t = {snr_drop_sec}s")
print(f"Momento em que o primeiro pacote foi descartado por RS: t = {first_drop_sec}s")
print(f"Momento em que o TMCC perdeu Lock: t = {first_tmcc_unlock_sec}s")

# Analisar a janela de colapso
if first_drop_sec:
    idx_drop = next(i for i, r in enumerate(rows) if r["timestamp_s"] == first_drop_sec)
    print("\nJanela de 10 segundos antes e 15 segundos depois do primeiro descarte de Reed-Solomon:")
    start_idx = max(0, idx_drop - 10)
    end_idx = min(n, idx_drop + 15)
    for r in rows[start_idx:end_idx]:
        print(f"  t={r['timestamp_s']:3d}s | RSSI={r['rssi_dbfs']:5.1f} dBFS | SNR={r['snr_db']:4.1f} dB | Corr={r['peak_corr']:.2f} | TMCC={r['tmcc_locked']} | RS_Corr={r['rs_corrected']:5d} | RS_Drop={r['rs_dropped']:5d} | TS_pkts={r['valid_ts_packets']:6d}")
