#include "mpegts/psi_parser.hpp"
#include <iostream>

namespace isdbt {

PsiParser::PsiParser() {}

std::string PsiParser::get_primary_service_name() const {
    for (const auto& [id, svc] : services_) {
        if (!svc.service_name.empty()) {
            return svc.service_name;
        }
    }
    return "";
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
        uint8_t pointer_field = payload[0];
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
    } else if (pmt_pids_.find(pid) != pmt_pids_.end()) {
        // PMT - Program Map Table
        parse_pmt(pid, payload, payload_len);
    }

    return false;
}

void PsiParser::parse_pat(const uint8_t* payload, size_t len) {
    if (len < 8) return;
    uint8_t table_id = payload[0];
    if (table_id != 0x00) return;

    uint16_t section_len = (((static_cast<uint16_t>(payload[1] & 0x0F)) << 8) | payload[2]);
    if (section_len + 3 > len) section_len = static_cast<uint16_t>(len - 3);

    size_t idx = 8;
    while (idx + 4 <= section_len + 3 - 4) { // desconta os 4 bytes finais de CRC32
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
    uint16_t program_info_len = (((static_cast<uint16_t>(payload[10] & 0x0F)) << 8) | payload[11]);

    size_t idx = 12 + program_info_len;
    uint16_t section_len = (((static_cast<uint16_t>(payload[1] & 0x0F)) << 8) | payload[2]);
    if (section_len + 3 > len) section_len = static_cast<uint16_t>(len - 3);

    while (idx + 5 <= section_len + 3 - 4) {
        uint8_t stream_type = payload[idx];
        uint16_t elem_pid = ((static_cast<uint16_t>(payload[idx + 1] & 0x1F)) << 8) | payload[idx + 2];
        uint16_t es_info_len = (((static_cast<uint16_t>(payload[idx + 3] & 0x0F)) << 8) | payload[idx + 4]);

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

    uint16_t section_len = (((static_cast<uint16_t>(payload[1] & 0x0F)) << 8) | payload[2]);
    if (section_len + 3 > len) section_len = static_cast<uint16_t>(len - 3);

    size_t idx = 11;
    while (idx + 5 <= section_len + 3 - 4) {
        uint16_t service_id = (static_cast<uint16_t>(payload[idx]) << 8) | payload[idx + 1];
        uint16_t loop_len = (((static_cast<uint16_t>(payload[idx + 3] & 0x0F)) << 8) | payload[idx + 4]);

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
                            std::string svc_name(reinterpret_cast<const char*>(payload + p), svc_name_len);
                            services_[service_id].service_id = service_id;
                            services_[service_id].service_name = svc_name;
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

