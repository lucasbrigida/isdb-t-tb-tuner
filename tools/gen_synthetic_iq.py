#!/usr/bin/env python3
"""
gen_synthetic_iq.py - Gera amostra sintetica de 10s de sinal ISDB-T Modo 3 a 2031746 S/s
para testes deterministas em ambientes sem gravacao real do ar.
"""

import sys
import os
import math
import struct
import numpy as np

def generate_synthetic_iq(output_path, duration_sec=10.0, sample_rate=2031746):
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    
    fft_len = 2048
    gi_len = 128  # GI 1/16
    sym_len = fft_len + gi_len
    total_samples = int(duration_sec * sample_rate)
    
    print(f"[gen_synthetic_iq] Gerando {total_samples} amostras ({duration_sec}s @ {sample_rate} S/s)...")
    
    # Gerador pseudo-aleatorio deterministico
    rng = np.random.RandomState(42)
    
    # Gera simbolos OFDM no dominio da frequencia
    num_symbols = (total_samples + sym_len - 1) // sym_len
    
    raw_bytes = bytearray()
    
    # Portadoras ativas 1-Seg (Segmento 0: bins 808..1239 centralizados)
    for sym_idx in range(num_symbols):
        freq_data = np.zeros(fft_len, dtype=np.complex64)
        
        # Bits QPSK pseudo-aleatorios
        bits_i = rng.choice([-1.0, 1.0], size=fft_len)
        bits_q = rng.choice([-1.0, 1.0], size=fft_len)
        freq_data = (bits_i + 1j * bits_q) * (1.0 / np.sqrt(2.0))
        
        # Converte para dominio do tempo via IFFT
        time_sym = np.fft.ifft(np.fft.ifftshift(freq_data)) * np.sqrt(fft_len)
        
        # Adiciona prefixo ciclico
        cp = time_sym[-gi_len:]
        full_sym = np.concatenate([cp, time_sym])
        
        # Normaliza e quantiza para uint8 (0..255 com DC em 127.5)
        scale = 35.0  # amplitude confortavel dentro do range uint8
        i_uint8 = np.clip(np.round(np.real(full_sym) * scale + 127.5), 0, 255).astype(np.uint8)
        q_uint8 = np.clip(np.round(np.imag(full_sym) * scale + 127.5), 0, 255).astype(np.uint8)
        
        # Interleave I e Q
        interleaved = np.empty((sym_len * 2,), dtype=np.uint8)
        interleaved[0::2] = i_uint8
        interleaved[1::2] = q_uint8
        
        raw_bytes.extend(interleaved.tobytes())
        if len(raw_bytes) >= total_samples * 2:
            break
            
    raw_bytes = raw_bytes[:total_samples * 2]
    with open(output_path, "wb") as f:
        f.write(raw_bytes)
        
    print(f"[gen_synthetic_iq] Salvo com sucesso em {output_path} ({len(raw_bytes)} bytes)")

if __name__ == "__main__":
    out = "tests/fixtures/synthetic_iq_10s.cu8"
    if len(sys.argv) > 1:
        out = sys.argv[1]
    generate_synthetic_iq(out, 10.0)

