#include "mpegts/ts_generator.hpp"
#include <cstring>

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

TsGenerator::TsGenerator() {}

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

    // PCR PID = 0x0100
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

} // namespace isdbt
