ts_file = "outputs/ch28_diagnostics/stream_ch28_ofdm2.ts"

with open(ts_file, "rb") as f:
    data = f.read()

total_packets = len(data) // 188

found_pat = False
found_pmt = False

for i in range(total_packets):
    pkt = data[i*188 : (i+1)*188]
    pid = ((pkt[1] & 0x1F) << 8) | pkt[2]
    
    if pid == 0x0000 and not found_pat:
        print("=== PACOTE PAT (PID 0x0000) ===")
        print("Hex:", pkt[:32].hex(" "))
        # Parse payload
        offset = 4
        if pkt[3] & 0x20:
            offset += 1 + pkt[4]
        if pkt[1] & 0x40: # pointer field
            pointer = pkt[offset]
            offset += 1 + pointer
        table_id = pkt[offset]
        section_length = ((pkt[offset+1] & 0x0F) << 8) | pkt[offset+2]
        ts_id = (pkt[offset+3] << 8) | pkt[offset+4]
        print(f"Table ID: 0x{table_id:02X}, Section Len: {section_length}, TS ID: 0x{ts_id:04X}")
        # Program list
        pos = offset + 8
        end = offset + 3 + section_length - 4 # exclude CRC
        while pos + 4 <= end:
            prog_num = (pkt[pos] << 8) | pkt[pos+1]
            prog_pid = ((pkt[pos+2] & 0x1F) << 8) | pkt[pos+3]
            print(f"  Programa {prog_num} (0x{prog_num:04X}) -> PMT PID 0x{prog_pid:04X}")
            pos += 4
        found_pat = True
        
    if pid == 0x1FC8 and not found_pmt:
        print("\n=== PACOTE PMT (PID 0x1FC8) ===")
        print("Hex:", pkt[:48].hex(" "))
        offset = 4
        if pkt[3] & 0x20:
            offset += 1 + pkt[4]
        if pkt[1] & 0x40:
            pointer = pkt[offset]
            offset += 1 + pointer
        table_id = pkt[offset]
        section_length = ((pkt[offset+1] & 0x0F) << 8) | pkt[offset+2]
        program_number = (pkt[offset+3] << 8) | pkt[offset+4]
        pcr_pid = ((pkt[offset+8] & 0x1F) << 8) | pkt[offset+9]
        prog_info_len = ((pkt[offset+10] & 0x0F) << 8) | pkt[offset+11]
        print(f"Table ID: 0x{table_id:02X}, Program Number: {program_number} (0x{program_number:04X}), PCR PID: 0x{pcr_pid:04X}")
        pos = offset + 12 + prog_info_len
        end = offset + 3 + section_length - 4
        while pos + 5 <= end:
            stream_type = pkt[pos]
            elem_pid = ((pkt[pos+1] & 0x1F) << 8) | pkt[pos+2]
            es_info_len = ((pkt[pos+3] & 0x0F) << 8) | pkt[pos+4]
            desc = "Outro"
            if stream_type == 0x1B: desc = "H.264 Video"
            elif stream_type == 0x0F: desc = "AAC Audio (ADTS)"
            elif stream_type == 0x11: desc = "AAC Audio (LATM)"
            elif stream_type == 0x06: desc = "Data / Subtitles"
            print(f"  Stream Type 0x{stream_type:02X} ({desc}) -> Elem PID 0x{elem_pid:04X}")
            pos += 5 + es_info_len
        found_pmt = True
        
    if found_pat and found_pmt:
        break
