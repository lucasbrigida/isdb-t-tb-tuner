import csv
import math
import statistics

csv_path = "outputs/ch28_diagnostics/metrics_ch28_ofdm2.csv"
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

print("=== RELATÓRIO DE TELEMETRIA RF E DSP (300 SEGUNDOS / CANAL 28 / OFDM 2) ===")
print(f"Total de amostras (segundos): {n}")
print(f"RSSI: Méd={statistics.mean(rssi_list):.2f} dBFS | Mín={min(rssi_list):.2f} dBFS | Máx={max(rssi_list):.2f} dBFS | Desvio={statistics.stdev(rssi_list):.2f} dB")
print(f"SNR:  Méd={statistics.mean(snr_list):.2f} dB   | Mín={min(snr_list):.2f} dB   | Máx={max(snr_list):.2f} dB   | Desvio={statistics.stdev(snr_list):.2f} dB")
print(f"Peak Corr: Méd={statistics.mean(corr_list):.3f} | Mín={min(corr_list):.3f} | Máx={max(corr_list):.3f}")

ofdm_lock_count = sum(1 for r in rows if r["ofdm_locked"] == 1)
tmcc_lock_count = sum(1 for r in rows if r["tmcc_locked"] == 1)
print(f"OFDM Lock: {ofdm_lock_count/n*100:.1f}% ({ofdm_lock_count}/{n} s)")
print(f"TMCC Lock: {tmcc_lock_count/n*100:.1f}% ({tmcc_lock_count}/{n} s)")

print(f"CFO (Hz): Inicial={cfo_list[0]:.1f} Hz | Final={cfo_list[-1]:.1f} Hz | Mín={min(cfo_list):.1f} Hz | Máx={max(cfo_list):.1f} Hz")
print(f"SCO (ppm): Inicial={sco_list[0]:.2f} ppm | Final={sco_list[-1]:.2f} ppm | Mín={min(sco_list):.2f} ppm | Máx={max(sco_list):.2f} ppm")

print(f"Fila SPSC Drops Totais: {rows[-1]['spsc_drops']}")
print(f"Pacotes TS emitidos: {rows[-1]['valid_ts_packets']} (taxa média: {rows[-1]['valid_ts_packets']/n:.1f} pkts/s)")
print(f"Reed-Solomon Corrigidos: {rows[-1]['rs_corrected']}")
print(f"Reed-Solomon Descartados (Drop): {rows[-1]['rs_dropped']}")

states = {}
for r in rows:
    st = r["sync_state"]
    states[st] = states.get(st, 0) + 1
print("\nDistribuicao de Estados de Sincronismo (FSM):")
for st, cnt in states.items():
    print(f"  {st}: {cnt}s ({cnt/n*100:.1f}%)")

print("\n--- Eventos Notáveis no Tempo ---")
rs_corr_rows = [r for r in rows if r["rs_corrected"] > 0]
if rs_corr_rows:
    print(f"Primeiro segundo com RS corrigido: t = {rs_corr_rows[0]['timestamp_s']}s (SNR={rs_corr_rows[0]['snr_db']:.1f} dB, RSSI={rs_corr_rows[0]['rssi_dbfs']:.1f} dBFS)")
    print("Últimos 15 segundos da corrida:")
    for r in rows[-15:]:
        print(f"  t={r['timestamp_s']:3d}s | RSSI={r['rssi_dbfs']:5.1f} dBFS | SNR={r['snr_db']:4.1f} dB | RS_Corr={r['rs_corrected']:4d} | RS_Drop={r['rs_dropped']:2d} | TS_pkts={r['valid_ts_packets']:6d}")
else:
    print("Nenhum erro de RS corrigido durante a captura.")
