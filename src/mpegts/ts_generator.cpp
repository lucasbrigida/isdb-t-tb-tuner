#include "mpegts/ts_generator.hpp"
#include <cstring>
#include "ts_stream_data.inl"

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

TsGenerator::TsGenerator() {
    cc_table_.fill(0);
}

void TsGenerator::get_next_packet(uint8_t* out188) {
    if (!out188 || NUM_TEST_PACKETS == 0) return;

    // Copia pacote base da sequencia valida
    std::memcpy(out188, &TEST_TS_PACKETS[current_packet_idx_ * 188], 188);

    uint16_t pid = static_cast<uint16_t>(((out188[1] & 0x1F) << 8) | out188[2]);
    uint8_t afc = (out188[3] >> 4) & 0x03;

    // Atualiza continuity counter se tiver payload
    if (afc == 1 || afc == 3) {
        out188[3] = static_cast<uint8_t>((out188[3] & 0xF0) | (cc_table_[pid] & 0x0F));
        cc_table_[pid] = static_cast<uint8_t>((cc_table_[pid] + 1) & 0x0F);
    }

    // Se tiver campo de adaptacao com PCR, atualiza o carimbo de tempo
    if ((afc & 0x02) && out188[4] > 0 && (out188[5] & 0x10)) {
        uint64_t pcr_base = (pcr_val_ / 300) & 0x1FFFFFFFFULL;
        uint32_t pcr_ext = static_cast<uint32_t>(pcr_val_ % 300);
        out188[6] = static_cast<uint8_t>((pcr_base >> 25) & 0xFF);
        out188[7] = static_cast<uint8_t>((pcr_base >> 17) & 0xFF);
        out188[8] = static_cast<uint8_t>((pcr_base >> 9) & 0xFF);
        out188[9] = static_cast<uint8_t>((pcr_base >> 1) & 0xFF);
        out188[10] = static_cast<uint8_t>(((pcr_base & 1) << 7) | 0x7E | ((pcr_ext >> 8) & 1));
        out188[11] = static_cast<uint8_t>(pcr_ext & 0xFF);

        pcr_val_ += 3600; // Incrementa o clock PCR 27MHz
    }

    current_packet_idx_ = (current_packet_idx_ + 1) % NUM_TEST_PACKETS;
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

static const uint8_t H264_TEST_FRAME[] = {
    0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x0A, 0xF8, 0x41, 0xA2,
    0x00, 0x00, 0x00, 0x01, 0x68, 0xCE, 0x38, 0x80,
    0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00, 0x3F, 0xFF, 0xFE, 0x00, 0x10, 0x00, 0x04
};

std::vector<std::vector<uint8_t>> TsGenerator::generate_h264_frame_packets(uint32_t frame_num, uint8_t& cc) {
    std::vector<std::vector<uint8_t>> packets;
    size_t pes_data_len = sizeof(H264_TEST_FRAME);
    size_t pes_header_len = 14;
    size_t total_pes_len = pes_header_len + pes_data_len;

    std::vector<uint8_t> pes(total_pes_len);
    pes[0] = 0x00;
    pes[1] = 0x00;
    pes[2] = 0x01;
    pes[3] = 0xE0;
    pes[4] = 0x00;
    pes[5] = 0x00;
    pes[6] = 0x80;
    pes[7] = 0x80;
    pes[8] = 0x05;

    uint64_t pts = (static_cast<uint64_t>(frame_num) * 3000) & 0x1FFFFFFFFULL;
    pes[9]  = static_cast<uint8_t>(0x21 | (((pts >> 30) & 0x07) << 1));
    pes[10] = static_cast<uint8_t>((pts >> 22) & 0xFF);
    pes[11] = static_cast<uint8_t>(0x01 | (((pts >> 15) & 0x7F) << 1));
    pes[12] = static_cast<uint8_t>((pts >> 7) & 0xFF);
    pes[13] = static_cast<uint8_t>(0x01 | ((pts & 0x7F) << 1));

    std::memcpy(pes.data() + pes_header_len, H264_TEST_FRAME, pes_data_len);

    size_t offset = 0;
    bool first = true;

    while (offset < pes.size()) {
        std::vector<uint8_t> pkt(188, 0xFF);
        pkt[0] = 0x47;
        pkt[1] = static_cast<uint8_t>((first ? 0x41 : 0x01));
        pkt[2] = 0x00;

        size_t remaining = pes.size() - offset;
        if (remaining >= 184) {
            pkt[3] = static_cast<uint8_t>(0x10 | (cc & 0x0F));
            std::memcpy(pkt.data() + 4, pes.data() + offset, 184);
            offset += 184;
        } else {
            size_t pad = 184 - remaining;
            pkt[3] = static_cast<uint8_t>(0x30 | (cc & 0x0F));
            pkt[4] = static_cast<uint8_t>(pad - 1);
            if (pad > 1) {
                pkt[5] = 0x00;
                std::memset(pkt.data() + 6, 0xFF, pad - 2);
            }
            std::memcpy(pkt.data() + 4 + pad, pes.data() + offset, remaining);
            offset += remaining;
        }

        cc = (cc + 1) & 0x0F;
        first = false;
        packets.push_back(pkt);
    }

    return packets;
}

static const uint8_t AAC_ADTS_FRAME[] = {
    0xFF, 0xF1, 0x50, 0x80, 0x02, 0x1F, 0xFC,
    0x21, 0x1B, 0x40, 0x00, 0x00
};

std::vector<std::vector<uint8_t>> TsGenerator::generate_aac_audio_packets(uint8_t& cc) {
    std::vector<std::vector<uint8_t>> packets;
    std::vector<uint8_t> pkt(188, 0xFF);
    pkt[0] = 0x47;
    pkt[1] = 0x41;
    pkt[2] = 0x01;

    size_t pes_data_len = sizeof(AAC_ADTS_FRAME);
    size_t pes_len = 9 + pes_data_len;
    size_t pad = 184 - pes_len;

    pkt[3] = static_cast<uint8_t>(0x30 | (cc & 0x0F));
    pkt[4] = static_cast<uint8_t>(pad - 1);
    if (pad > 1) {
        pkt[5] = 0x00;
        std::memset(pkt.data() + 6, 0xFF, pad - 2);
    }

    uint8_t* p = pkt.data() + 4 + pad;
    p[0] = 0x00;
    p[1] = 0x00;
    p[2] = 0x01;
    p[3] = 0xC0;
    p[4] = static_cast<uint8_t>((pes_data_len + 3) >> 8);
    p[5] = static_cast<uint8_t>((pes_data_len + 3) & 0xFF);
    p[6] = 0x80;
    p[7] = 0x00;
    p[8] = 0x00;

    std::memcpy(p + 9, AAC_ADTS_FRAME, pes_data_len);

    cc = (cc + 1) & 0x0F;
    packets.push_back(pkt);
    return packets;
}

} // namespace isdbt
