#include "mpegts/psi_parser.hpp"
#include <iostream>
#include <algorithm>
#include <cstring>

namespace isdbt {

static uint32_t mpeg2_crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (static_cast<uint32_t>(data[i]) << 24);
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x80000000)
                crc = (crc << 1) ^ 0x04C11DB7;
            else
                crc <<= 1;
        }
    }
    return crc;
}

PsiParser::PsiParser() {}

std::string PsiParser::get_primary_service_name() const {
    for (const auto& [id, svc] : services_) {
        if (!svc.service_name.empty()) {
            return svc.service_name;
        }
    }
    return "";
}

std::vector<uint16_t> PsiParser::get_discontinuity_pids() const {
    std::vector<uint16_t> pids;
    for (const auto& [id, service] : services_) {
        (void)id;
        for (uint16_t pid : {service.pcr_pid, service.video_pid, service.audio_pid}) {
            if (pid != 0 && pid != 0x1FFF &&
                std::find(pids.begin(), pids.end(), pid) == pids.end()) {
                pids.push_back(pid);
            }
        }
    }
    return pids;
}

bool PsiParser::parse_packet(const uint8_t* ts188) {
    if (!ts188 || ts188[0] != 0x47) return false;

    bool tei = (ts188[1] & 0x80) != 0;
    if (tei) return false; // Erro de transporte indicado

    bool pusi = (ts188[1] & 0x40) != 0;
    uint16_t pid = ((static_cast<uint16_t>(ts188[1] & 0x1F)) << 8) | ts188[2];
    uint8_t afc = (ts188[3] >> 4) & 0x03;

    if (afc == 0 || afc == 2) return false; // Sem payload

    size_t payload_offset = 4;
    if (afc == 3) {
        uint8_t adaptation_len = ts188[4];
        payload_offset += 1 + adaptation_len;
        if (payload_offset >= 188) return false;
    }

    const uint8_t* payload = ts188 + payload_offset;
    size_t payload_len = 188 - payload_offset;

    if (pusi && payload_len > 0) {
        size_t pointer_field = payload[0];
        payload += 1 + pointer_field;
        if (1 + pointer_field >= payload_len) return false;
        payload_len -= (1 + pointer_field);
    }

    if (pid == 0x0000) {
        // PAT - Program Association Table
        parse_pat(payload, payload_len);
    } else if (pid == 0x0011) {
        // SDT - Service Description Table
        parse_sdt(payload, payload_len);
        return true;
    } else if ((pid >= 0x1FC8 && pid <= 0x1FCF) || pmt_pids_.find(pid) != pmt_pids_.end()) {
        // PMT - Program Map Table (em 1-Seg ISDB-Tb, o PMT é transmitido no PID fixo 0x1FC8..0x1FCF)
        parse_pmt(pid, payload, payload_len);
        return true;
    }

    return false;
}

bool PsiParser::build_pat_packet(uint8_t* out_ts188) {
    if (!out_ts188) return false;

    uint16_t pmt_pid = 0x1FC8;
    uint16_t prog_num = 0x0001;
    if (!pmt_pids_.empty()) {
        pmt_pid = pmt_pids_.begin()->first;
        prog_num = pmt_pids_.begin()->second;
    }

    std::memset(out_ts188, 0xFF, 188);
    out_ts188[0] = 0x47;
    out_ts188[1] = 0x40; // PUSI = 1, PID = 0x0000
    out_ts188[2] = 0x00;
    out_ts188[3] = 0x10 | (pat_cc_ & 0x0F);
    pat_cc_ = (pat_cc_ + 1) & 0x0F;
    out_ts188[4] = 0x00; // Pointer field = 0

    uint8_t* sec = &out_ts188[5];
    sec[0] = 0x00; // table_id = 0x00 (PAT)
    sec[1] = 0xB0; // section_syntax_indicator = 1, section_length = 13 (0x00D)
    sec[2] = 0x0D;
    sec[3] = static_cast<uint8_t>((prog_num >> 8) & 0xFF); // transport_stream_id
    sec[4] = static_cast<uint8_t>(prog_num & 0xFF);
    sec[5] = 0xC0 | static_cast<uint8_t>((pat_version_ & 0x1F) << 1) | 0x01; // version dinamica (notifica o demuxer em troca de canal)
    sec[6] = 0x00; // section_number = 0
    sec[7] = 0x00; // last_section_number = 0
    sec[8] = static_cast<uint8_t>((prog_num >> 8) & 0xFF); // program_number
    sec[9] = static_cast<uint8_t>(prog_num & 0xFF);
    sec[10] = 0xE0 | static_cast<uint8_t>((pmt_pid >> 8) & 0x1F);
    sec[11] = static_cast<uint8_t>(pmt_pid & 0xFF);

    uint32_t crc = mpeg2_crc32(sec, 12);
    sec[12] = static_cast<uint8_t>((crc >> 24) & 0xFF);
    sec[13] = static_cast<uint8_t>((crc >> 16) & 0xFF);
    sec[14] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    sec[15] = static_cast<uint8_t>(crc & 0xFF);
    return true;
}

void PsiParser::reset() {
    services_.clear();
    pmt_pids_.clear();
    pat_version_ = (pat_version_ + 1) & 0x1F;
}

void PsiParser::parse_pat(const uint8_t* payload, size_t len) {
    if (len < 8) return;
    uint8_t table_id = payload[0];
    if (table_id != 0x00) return;

    size_t section_len = (((static_cast<size_t>(payload[1] & 0x0F)) << 8) | payload[2]);
    if (section_len + 3 > len) section_len = len - 3;

    size_t idx = 8;
    while (section_len + 3 >= 4 && idx + 4 <= section_len + 3 - 4) { // desconta os 4 bytes finais de CRC32
        uint16_t program_num = (static_cast<uint16_t>(payload[idx]) << 8) | payload[idx + 1];
        uint16_t program_pid = ((static_cast<uint16_t>(payload[idx + 2] & 0x1F)) << 8) | payload[idx + 3];

        if (program_num != 0) { // Program 0 eh NIT
            services_[program_num].service_id = program_num;
            services_[program_num].pmt_pid = program_pid;
            pmt_pids_[program_pid] = program_num;
        }
        idx += 4;
    }
}

void PsiParser::parse_pmt(uint16_t pmt_pid, const uint8_t* payload, size_t len) {
    if (len < 12) return;
    uint8_t table_id = payload[0];
    if (table_id != 0x02) return; // PMT table_id = 0x02

    uint16_t service_id = (static_cast<uint16_t>(payload[3]) << 8) | payload[4];
    bool is_new = (pmt_pids_.find(pmt_pid) == pmt_pids_.end() || pmt_pids_[pmt_pid] != service_id);
    services_[service_id].service_id = service_id;
    services_[service_id].pmt_pid = pmt_pid;
    services_[service_id].pcr_pid =
        ((static_cast<uint16_t>(payload[8] & 0x1F)) << 8) | payload[9];
    pmt_pids_[pmt_pid] = service_id;
    if (is_new) {
        pat_version_ = (pat_version_ + 1) & 0x1F;
    }

    size_t program_info_len = (((static_cast<size_t>(payload[10] & 0x0F)) << 8) | payload[11]);

    size_t idx = 12 + program_info_len;
    size_t section_len = (((static_cast<size_t>(payload[1] & 0x0F)) << 8) | payload[2]);
    if (section_len + 3 > len) section_len = len - 3;

    while (section_len + 3 >= 4 && idx + 5 <= section_len + 3 - 4) {
        uint8_t stream_type = payload[idx];
        uint16_t elem_pid = ((static_cast<uint16_t>(payload[idx + 1] & 0x1F)) << 8) | payload[idx + 2];
        size_t es_info_len = (((static_cast<size_t>(payload[idx + 3] & 0x0F)) << 8) | payload[idx + 4]);

        if (stream_type == 0x1B || stream_type == 0x02) {
            // 0x1B = H.264 / AVC video, 0x02 = MPEG-2 video
            services_[service_id].video_pid = elem_pid;
        } else if (stream_type == 0x0F || stream_type == 0x11 || stream_type == 0x03 || stream_type == 0x04) {
            // 0x0F = AAC ADTS, 0x11 = LATM/LOAS AAC
            services_[service_id].audio_pid = elem_pid;
        }

        idx += 5 + es_info_len;
    }
}

void PsiParser::parse_sdt(const uint8_t* payload, size_t len) {
    if (len < 11) return;
    uint8_t table_id = payload[0];
    if (table_id != 0x42 && table_id != 0x46) return; // 0x42 = actual TS, 0x46 = other TS

    size_t section_len = (((static_cast<size_t>(payload[1] & 0x0F)) << 8) | payload[2]);
    if (section_len + 3 > len) section_len = len - 3;

    size_t idx = 11;
    while (section_len + 3 >= 4 && idx + 5 <= section_len + 3 - 4) {
        uint16_t service_id = (static_cast<uint16_t>(payload[idx]) << 8) | payload[idx + 1];
        size_t loop_len = (((static_cast<size_t>(payload[idx + 3] & 0x0F)) << 8) | payload[idx + 4]);

        size_t d_idx = idx + 5;
        size_t d_end = d_idx + loop_len;

        while (d_idx + 2 <= d_end && d_idx + 2 <= len) {
            uint8_t desc_tag = payload[d_idx];
            uint8_t desc_len = payload[d_idx + 1];

            if (desc_tag == 0x48 && d_idx + 2 + desc_len <= len) { // Service descriptor
                size_t p = d_idx + 3; // pula tag, len, service_type
                if (p < len) {
                    uint8_t provider_name_len = payload[p];
                    p += 1 + provider_name_len;
                    if (p < len) {
                        uint8_t svc_name_len = payload[p];
                        p += 1;
                        if (p + svc_name_len <= len) {
                            std::string clean_name;
                            for (size_t k = 0; k < svc_name_len; ++k) {
                                uint8_t c = payload[p + k];
                                // Descarta caracteres de controle ARIB/DVB (< 0x20 como 0x0E Shift-Out e 0x7F DEL)
                                if (c >= 0x20 && c != 0x7F) {
                                    clean_name.push_back(static_cast<char>(c));
                                }
                            }
                            size_t start_pos = clean_name.find_first_not_of(" \t");
                            if (start_pos != std::string::npos && start_pos > 0) {
                                clean_name = clean_name.substr(start_pos);
                            }
                            if (!clean_name.empty()) {
                                services_[service_id].service_id = service_id;
                                services_[service_id].service_name = clean_name;
                            }
                        }
                    }
                }
            }
            d_idx += 2 + desc_len;
        }

        idx += 5 + loop_len;
    }
}

} // namespace isdbt
