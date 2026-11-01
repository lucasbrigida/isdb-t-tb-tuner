#!/usr/bin/env python3
"""
ISDB-T / ISDB-Tb Receiver - RTL-SDR Pre-flight Calibration (ctypes direct wrapper)
Zero fragile dependencies, works directly with rtlsdr.dll
"""
import os
import sys
import ctypes
import numpy as np

# Procura DLL tanto em tools/rtl-sdr quanto em deps
script_dir = os.path.dirname(os.path.abspath(__file__))
project_dir = os.path.dirname(script_dir)
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
    print("[ERRO] Nenhuma DLL do RTL-SDR encontrada.", file=sys.stderr)
    sys.exit(1)

if hasattr(os, "add_dll_directory"):
    os.add_dll_directory(dll_dir)
os.environ["PATH"] = dll_dir + os.pathsep + os.environ.get("PATH", "")

lib = ctypes.CDLL(dll_path)

# ctypes signatures
p_dev = ctypes.c_void_p()
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
lib.rtlsdr_set_tuner_gain_mode.restype = ctypes.c_int
lib.rtlsdr_reset_buffer.argtypes = [ctypes.c_void_p]
lib.rtlsdr_reset_buffer.restype = ctypes.c_int
lib.rtlsdr_read_sync.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
lib.rtlsdr_read_sync.restype = ctypes.c_int

def calibrate(device_index=0, sample_rate=2048000, freq_hz=473142857, num_samples=131072):
    count = lib.rtlsdr_get_device_count()
    if count == 0:
        print("[ERRO] Nenhum RTL-SDR encontrado.", file=sys.stderr)
        sys.exit(1)

    dev = ctypes.c_void_p()
    ret = lib.rtlsdr_open(ctypes.byref(dev), device_index)
    if ret != 0:
        print(f"[ERRO] Falha ao abrir RTL-SDR dispositivo {device_index} (codigo {ret}).", file=sys.stderr)
        sys.exit(1)

    try:
        lib.rtlsdr_set_sample_rate(dev, sample_rate)
        lib.rtlsdr_set_center_freq(dev, freq_hz)
        lib.rtlsdr_set_tuner_gain_mode(dev, 0) # AGC auto
        lib.rtlsdr_reset_buffer(dev)

        buf_size = num_samples * 2 # 2 bytes per complex sample (I and Q, uint8)
        raw_buf = ctypes.create_string_buffer(buf_size)
        n_read = ctypes.c_int(0)

        ret = lib.rtlsdr_read_sync(dev, raw_buf, buf_size, ctypes.byref(n_read))
        if ret != 0 or n_read.value < buf_size:
            print("[AVISO] Leitura incompleta de amostras.", file=sys.stderr)

        # Converter uint8 [0, 255] para float [-1.0, +1.0]
        data = np.frombuffer(raw_buf.raw[:n_read.value], dtype=np.uint8).astype(np.float32)
        data = (data - 127.5) / 127.5
        i_samples = data[0::2]
        q_samples = data[1::2]
        iq_complex = i_samples + 1j * q_samples

        dc_i = float(np.mean(i_samples))
        dc_q = float(np.mean(q_samples))
        dc_mag = np.sqrt(dc_i**2 + dc_q**2)

        amp_i = float(np.mean(np.abs(i_samples)))
        amp_q = float(np.mean(np.abs(q_samples)))
        iq_ratio = float(amp_i / (amp_q + 1e-12))

        pwr = float(np.mean(np.abs(iq_complex)**2))
        rssi_dbfs = 10 * np.log10(pwr + 1e-12)

        print("[*] Calibracao concluida com sucesso!", file=sys.stderr)
        print(f"[*] DC Offset: I={dc_i:+.5f}, Q={dc_q:+.5f} (Mag={dc_mag:.5f})", file=sys.stderr)
        print(f"[*] IQ Amplitude Ratio: {iq_ratio:.4f}", file=sys.stderr)
        print(f"[*] Potencia media (RSSI): {rssi_dbfs:.2f} dBFS", file=sys.stderr)

        print(f"PPM=0;DC_I={dc_i:.5f};DC_Q={dc_q:.5f};RSSI={rssi_dbfs:.2f}")

    finally:
        lib.rtlsdr_close(dev)

if __name__ == "__main__":
    calibrate()
