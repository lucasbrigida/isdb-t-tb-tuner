#!/usr/bin/env python3
"""
Quick RF spectrum check on Brazilian UHF channels (14 to 45)
"""
import os
import sys
import time
import ctypes
import numpy as np

project_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
possible_dirs = [
    os.path.join(project_dir, "tools", "rtl-sdr"),
    os.path.join(project_dir, "deps", "rtlsdr", "x64")
]

dll_dir = None
dll_path = None
for d in possible_dirs:
    for name in ["librtlsdr.dll", "rtlsdr.dll"]:
        candidate = os.path.join(d, name)
        if os.path.exists(candidate):
            dll_dir = d
            dll_path = candidate
            break
    if dll_path:
        break

if not dll_path:
    print("[ERRO] Nenhuma DLL do RTL-SDR encontrada.")
    sys.exit(1)

if hasattr(os, "add_dll_directory"):
    os.add_dll_directory(dll_dir)
os.environ["PATH"] = dll_dir + os.pathsep + os.environ.get("PATH", "")

lib = ctypes.CDLL(dll_path)

lib.rtlsdr_get_device_count.restype = ctypes.c_uint32
lib.rtlsdr_open.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_uint32]
lib.rtlsdr_open.restype = ctypes.c_int
lib.rtlsdr_close.argtypes = [ctypes.c_void_p]
lib.rtlsdr_close.restype = ctypes.c_int
lib.rtlsdr_set_center_freq.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
lib.rtlsdr_set_center_freq.restype = ctypes.c_int
lib.rtlsdr_set_sample_rate.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
lib.rtlsdr_set_sample_rate.restype = ctypes.c_int
lib.rtlsdr_set_tuner_gain_mode.argtypes = [ctypes.c_void_p, ctypes.c_int]
lib.rtlsdr_reset_buffer.argtypes = [ctypes.c_void_p]
lib.rtlsdr_read_sync.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(ctypes.c_int)]

def scan_rf():
    dev = ctypes.c_void_p()
    if lib.rtlsdr_open(ctypes.byref(dev), 0) != 0:
        print("[ERRO] Nao foi possivel abrir RTL-SDR")
        return

    try:
        sample_rate = 2031746
        lib.rtlsdr_set_sample_rate(dev, sample_rate)
        lib.rtlsdr_set_tuner_gain_mode(dev, 0) # AGC

        buf_size = 65536 * 2
        raw_buf = ctypes.create_string_buffer(buf_size)
        n_read = ctypes.c_int(0)

        print("\n=== Varredura Rapida de Potencia RF (Canais 14 a 45) ===")
        print("Canal | Frequencia (MHz) | Potencia (dBFS) | Status")
        print("------+------------------+-----------------+-----------")

        best_channel = None
        best_pwr = -999.0
        best_freq = 0

        for ch in range(14, 46):
            freq_hz = int((470 + (ch - 14) * 6 + 3 + 1/7) * 1e6)
            lib.rtlsdr_reset_buffer(dev)
            lib.rtlsdr_set_center_freq(dev, freq_hz)
            lib.rtlsdr_reset_buffer(dev)
            time.sleep(0.1)  # 100 ms PLL stabilization delay (Erro 10, Erro 14, ADR 016)
            lib.rtlsdr_read_sync(dev, raw_buf, buf_size, ctypes.byref(n_read))

            data = np.frombuffer(raw_buf.raw[:n_read.value], dtype=np.uint8).astype(np.float32)
            data = (data - 127.5) / 127.5
            iq = data[0::2] + 1j * data[1::2]
            pwr = float(10 * np.log10(np.mean(np.abs(iq)**2) + 1e-12))

            status = "SINAL FORTE!" if pwr > -12.0 else ("Ativo" if pwr > -18.0 else "Ruido")
            print(f" {ch:2d}   |    {freq_hz/1e6:7.3f} MHz    |   {pwr:6.2f} dBFS   | {status}")

            if pwr > best_pwr:
                best_pwr = pwr
                best_channel = ch
                best_freq = freq_hz

        print("-------------------------------------------------------")
        print(f"Melhor canal detectado: Canal {best_channel} ({best_freq/1e6:.3f} MHz) com {best_pwr:.2f} dBFS\n")

    finally:
        lib.rtlsdr_close(dev)

if __name__ == "__main__":
    scan_rf()

