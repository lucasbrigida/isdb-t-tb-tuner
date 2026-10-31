#include "mpegts/ts_generator.hpp"
#include <cstring>
#include <algorithm>

namespace isdbt {

// Tabela CRC32 padrao MPEG-2 (polinomio 0x04C11DB7)
uint32_t TsGenerator::crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (static_cast<uint32_t>(data[i]) << 24);
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x80000000) {
                crc = (crc << 1) ^ 0x04C11DB7;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

void TsGenerator::encode_pcr(uint8_t* out6, uint64_t pcr_ticks_27mhz) {
    uint64_t pcr_base = pcr_ticks_27mhz / 300;
    uint32_t pcr_ext = static_cast<uint32_t>(pcr_ticks_27mhz % 300);

    out6[0] = static_cast<uint8_t>((pcr_base >> 25) & 0xFF);
    out6[1] = static_cast<uint8_t>((pcr_base >> 17) & 0xFF);
    out6[2] = static_cast<uint8_t>((pcr_base >> 9) & 0xFF);
    out6[3] = static_cast<uint8_t>((pcr_base >> 1) & 0xFF);
    out6[4] = static_cast<uint8_t>(((pcr_base & 1) << 7) | 0x7E | ((pcr_ext >> 8) & 1));
    out6[5] = static_cast<uint8_t>(pcr_ext & 0xFF);
}

void TsGenerator::encode_pts(uint8_t* out5, uint64_t pts_ticks_90khz) {
    out5[0] = static_cast<uint8_t>(0x21 | (((pts_ticks_90khz >> 30) & 0x07) << 1));
    out5[1] = static_cast<uint8_t>((pts_ticks_90khz >> 22) & 0xFF);
    out5[2] = static_cast<uint8_t>(((pts_ticks_90khz >> 14) & 0xFE) | 0x01);
    out5[3] = static_cast<uint8_t>((pts_ticks_90khz >> 7) & 0xFF);
    out5[4] = static_cast<uint8_t>(((pts_ticks_90khz << 1) & 0xFE) | 0x01);
}

TsGenerator::TsGenerator() {}

void TsGenerator::reset() {
    pat_cc_ = 0;
    pmt_cc_ = 0;
    vid_cc_ = 0;
    aud_cc_ = 0;
    null_cc_ = 0;
    frame_index_ = 0;
}

void TsGenerator::get_pat_packet(uint8_t* out188, uint8_t continuity_counter) {
    std::memset(out188, 0xFF, 188);

    out188[0] = 0x47; // Sync byte
    out188[1] = 0x40; // PUSI = 1, PID = 0x0000
    out188[2] = 0x00;
    out188[3] = static_cast<uint8_t>(0x10 | (continuity_counter & 0x0F));
    out188[4] = 0x00; // Pointer field

    uint8_t* pat = out188 + 5;
    pat[0] = 0x00; // Table ID = 0x00 (PAT)
    uint16_t section_len = 13;
    pat[1] = static_cast<uint8_t>(0xB0 | ((section_len >> 8) & 0x0F));
    pat[2] = static_cast<uint8_t>(section_len & 0xFF);

    pat[3] = 0x00; // Transport Stream ID (high)
    pat[4] = 0x01; // Transport Stream ID (low) = 1
    pat[5] = 0xC1; // Version 0, current_next_indicator = 1
    pat[6] = 0x00; // Section number = 0
    pat[7] = 0x00; // Last section number = 0

    // Program 1 -> PMT PID 0x1000
    pat[8] = 0x00;
    pat[9] = 0x01;
    pat[10] = 0xF0;
    pat[11] = 0x00;

    uint32_t crc_val = crc32(pat, 12);
    pat[12] = static_cast<uint8_t>((crc_val >> 24) & 0xFF);
    pat[13] = static_cast<uint8_t>((crc_val >> 16) & 0xFF);
    pat[14] = static_cast<uint8_t>((crc_val >> 8) & 0xFF);
    pat[15] = static_cast<uint8_t>(crc_val & 0xFF);
}

void TsGenerator::get_pmt_packet(uint8_t* out188, uint8_t continuity_counter) {
    std::memset(out188, 0xFF, 188);

    out188[0] = 0x47;
    out188[1] = 0x50; // PUSI = 1, PID = 0x1000
    out188[2] = 0x00;
    out188[3] = static_cast<uint8_t>(0x10 | (continuity_counter & 0x0F));
    out188[4] = 0x00;

    uint8_t* pmt = out188 + 5;
    pmt[0] = 0x02; // Table ID = 0x02 (PMT)
    uint16_t section_len = 23;
    pmt[1] = static_cast<uint8_t>(0xB0 | ((section_len >> 8) & 0x0F));
    pmt[2] = static_cast<uint8_t>(section_len & 0xFF);

    pmt[3] = 0x00;
    pmt[4] = 0x01;
    pmt[5] = 0xC1;
    pmt[6] = 0x00;
    pmt[7] = 0x00;

    // PCR PID = 0x0100 (Video PID)
    pmt[8] = 0xE1;
    pmt[9] = 0x00;

    // Program info length = 0
    pmt[10] = 0xF0;
    pmt[11] = 0x00;

    // Stream 1: H.264 AVC Video no PID 0x0100
    pmt[12] = 0x1B;
    pmt[13] = 0xE1;
    pmt[14] = 0x00;
    pmt[15] = 0xF0;
    pmt[16] = 0x00;

    // Stream 2: AAC Audio no PID 0x0101
    pmt[17] = 0x0F;
    pmt[18] = 0xE1;
    pmt[19] = 0x01;
    pmt[20] = 0xF0;
    pmt[21] = 0x00;

    uint32_t crc_val = crc32(pmt, 22);
    pmt[22] = static_cast<uint8_t>((crc_val >> 24) & 0xFF);
    pmt[23] = static_cast<uint8_t>((crc_val >> 16) & 0xFF);
    pmt[24] = static_cast<uint8_t>((crc_val >> 8) & 0xFF);
    pmt[25] = static_cast<uint8_t>(crc_val & 0xFF);
}

void TsGenerator::get_standby_datagram(uint8_t* out1316) {
    // 1. Constantes H.264 / AVC (Baseline Profile, 320x240, 25 fps)
    static const uint8_t start_code[4] = {0x00, 0x00, 0x00, 0x01};
    static const uint8_t aud_nal[2]    = {0x09, 0x10};
    static const uint8_t sps_nal[8]    = {0x67, 0x42, 0xC0, 0x14, 0xDA, 0x05, 0x07, 0xE4};
    static const uint8_t pps_nal[4]    = {0x68, 0xCE, 0x38, 0x80};
    static const uint8_t idr_slice[191] = {
        0x65, 0x88, 0x84, 0x90, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21,
        0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08,
        0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42,
        0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10,
        0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84,
        0x21, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21,
        0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08,
        0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42,
        0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10,
        0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84,
        0x21, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x21,
        0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x84, 0x24
    };

    // 2. Quadro de Audio Silencioso AAC ADTS (48 kHz, Stereo)
    static const uint8_t adts_frame[12] = {
        0xFF, 0xF1, 0x4C, 0x80, 0x01, 0x9F, 0xFC, 0x20, 0x00, 0x00, 0x03, 0x80
    };

    // Calculo do relogio PCR (27 MHz) e PTS (90 kHz) a 25 fps
    uint64_t pcr_ticks = frame_index_ * (27000000ULL / 25ULL);
    uint64_t pts_ticks = frame_index_ * (90000ULL / 25ULL);

    // --- PACOTE 0: PAT (PID 0x0000) ---
    uint8_t* p0 = out1316;
    get_pat_packet(p0, pat_cc_++);
    pat_cc_ &= 0x0F;

    // --- PACOTE 1: PMT (PID 0x1000) ---
    uint8_t* p1 = out1316 + 188;
    get_pmt_packet(p1, pmt_cc_++);
    pmt_cc_ &= 0x0F;

    // --- PACOTES 2 e 3: Video H.264 no PID 0x0100 com PCR e PTS ---
    // Montagem do PES de video:
    // Header (6 bytes) + PES Flags/PTS (8 bytes) + Annex B (221 bytes) = 235 bytes
    uint8_t vid_pes[235];
    vid_pes[0] = 0x00;
    vid_pes[1] = 0x00;
    vid_pes[2] = 0x01;
    vid_pes[3] = 0xE0; // Stream ID = Video 0
    uint16_t vid_pes_len = 3 + 5 + 221; // 229
    vid_pes[4] = static_cast<uint8_t>((vid_pes_len >> 8) & 0xFF);
    vid_pes[5] = static_cast<uint8_t>(vid_pes_len & 0xFF);
    vid_pes[6] = 0x80;
    vid_pes[7] = 0x80; // PTS presente
    vid_pes[8] = 0x05; // Tamanho do header PTS = 5
    encode_pts(&vid_pes[9], pts_ticks);

    // Annex B payload: AUD + SPS + PPS + IDR
    uint8_t* annexb = vid_pes + 14;
    std::memcpy(annexb, start_code, 4); annexb += 4;
    std::memcpy(annexb, aud_nal, 2);    annexb += 2;
    std::memcpy(annexb, start_code, 4); annexb += 4;
    std::memcpy(annexb, sps_nal, 8);    annexb += 8;
    std::memcpy(annexb, start_code, 4); annexb += 4;
    std::memcpy(annexb, pps_nal, 4);    annexb += 4;
    std::memcpy(annexb, start_code, 4); annexb += 4;
    std::memcpy(annexb, idr_slice, 191);

    // Pacote 2: Primeiro pacote de video (PUSI=1, AFC=3, Adaptation Field com PCR dinamico)
    uint8_t* p2 = out1316 + 188 * 2;
    p2[0] = 0x47;
    p2[1] = 0x41; // PUSI=1, PID=0x0100
    p2[2] = 0x00;
    p2[3] = static_cast<uint8_t>(0x30 | (vid_cc_++ & 0x0F));
    vid_cc_ &= 0x0F;

    p2[4] = 0x07; // AF length = 7 (1 byte flags + 6 bytes PCR)
    p2[5] = 0x50; // Random access indicator (0x40) + PCR flag (0x10)
    encode_pcr(&p2[6], pcr_ticks);
    std::memcpy(&p2[12], vid_pes, 176); // 176 bytes cabem no primeiro pacote

    // Pacote 3: Segundo pacote de video (PUSI=0, AFC=3 com enchimento)
    uint8_t* p3 = out1316 + 188 * 3;
    p3[0] = 0x47;
    p3[1] = 0x01; // PUSI=0, PID=0x0100
    p3[2] = 0x00;
    p3[3] = static_cast<uint8_t>(0x30 | (vid_cc_++ & 0x0F));
    vid_cc_ &= 0x0F;

    // Restam 235 - 176 = 59 bytes.
    // Espaco util no pacote TS = 184 bytes. Enchimento = 184 - 59 = 125 bytes.
    p3[4] = 124; // AF length = 124 (1 flag + 123 bytes 0xFF)
    p3[5] = 0x00; // Flags = 0
    std::memset(&p3[6], 0xFF, 123);
    std::memcpy(&p3[129], vid_pes + 176, 59);

    // --- PACOTE 4: Audio AAC no PID 0x0101 com PTS ---
    uint8_t* p4 = out1316 + 188 * 4;
    p4[0] = 0x47;
    p4[1] = 0x41; // PUSI=1, PID=0x0101
    p4[2] = 0x01;
    p4[3] = static_cast<uint8_t>(0x30 | (aud_cc_++ & 0x0F));
    aud_cc_ &= 0x0F;

    // PES Audio (26 bytes total)
    uint8_t aud_pes[26];
    aud_pes[0] = 0x00;
    aud_pes[1] = 0x00;
    aud_pes[2] = 0x01;
    aud_pes[3] = 0xC0; // Stream ID = Audio 0
    uint16_t aud_pes_len = 3 + 5 + 12; // 20
    aud_pes[4] = static_cast<uint8_t>((aud_pes_len >> 8) & 0xFF);
    aud_pes[5] = static_cast<uint8_t>(aud_pes_len & 0xFF);
    aud_pes[6] = 0x80;
    aud_pes[7] = 0x80;
    aud_pes[8] = 0x05;
    encode_pts(&aud_pes[9], pts_ticks);
    std::memcpy(&aud_pes[14], adts_frame, 12);

    // Enchimento no pacote TS de audio: 184 - 26 = 158 bytes
    p4[4] = 157; // AF length
    p4[5] = 0x00;
    std::memset(&p4[6], 0xFF, 156);
    std::memcpy(&p4[162], aud_pes, 26);

    // --- PACOTES 5 e 6: Pacotes Nulos (PID 0x1FFF) para alcancar 1316B exatos ---
    for (int n = 0; n < 2; ++n) {
        uint8_t* pn = out1316 + 188 * (5 + n);
        pn[0] = 0x47;
        pn[1] = 0x1F;
        pn[2] = 0xFF;
        pn[3] = static_cast<uint8_t>(0x10 | (null_cc_++ & 0x0F));
        null_cc_ &= 0x0F;
        std::memset(&pn[4], 0xFF, 184);
    }

    frame_index_++;
}

void TsGenerator::get_standby_block(std::vector<std::vector<uint8_t>>& out_packets) {
    uint8_t buf[1316];
    get_standby_datagram(buf);
    for (int i = 0; i < 7; ++i) {
        std::vector<uint8_t> pkt(188);
        std::memcpy(pkt.data(), buf + i * 188, 188);
        out_packets.push_back(std::move(pkt));
    }
}

} // namespace isdbt
