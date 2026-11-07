#include "mpegts/psi_parser.hpp"
#include "mpegts/arib_b24_decoder.hpp"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <cstdio>
#include <cmath>

namespace isdbt {

static std::string clean_text_string(const uint8_t* data, size_t len) {
    std::string s = AribB24Decoder::decode_to_utf8(data, len);
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, (last - first + 1));
}

static std::chrono::system_clock::time_point parse_mjd_utc(uint16_t mjd, uint8_t bcd_h, uint8_t bcd_m, uint8_t bcd_s) {
    if (mjd == 0xFFFF || mjd == 0x0000) return {};
    int y_prime = static_cast<int>((static_cast<double>(mjd) - 15078.2) / 365.25);
    int m_prime = static_cast<int>((static_cast<double>(mjd) - 14956.1 - static_cast<int>(y_prime * 365.25)) / 30.6001);
    int day = static_cast<int>(mjd) - 14956 - static_cast<int>(y_prime * 365.25) - static_cast<int>(m_prime * 30.6001);
    int k = (m_prime == 14 || m_prime == 15) ? 1 : 0;
    int year = y_prime + k + 1900;
    int month = m_prime - 1 - k * 12;

    int hour = ((bcd_h >> 4) * 10) + (bcd_h & 0x0F);
    int min = ((bcd_m >> 4) * 10) + (bcd_m & 0x0F);
    int sec = ((bcd_s >> 4) * 10) + (bcd_s & 0x0F);

    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59) {
        return {};
    }

    std::tm tm_local{};
    tm_local.tm_year = year - 1900;
    tm_local.tm_mon = month - 1;
    tm_local.tm_mday = day;
    tm_local.tm_hour = hour;
    tm_local.tm_min = min;
    tm_local.tm_sec = sec;
    tm_local.tm_isdst = -1;

    time_t t = mktime(&tm_local);
    if (t == -1) return {};
    return std::chrono::system_clock::from_time_t(t);
}

static std::string format_time_local(const std::chrono::system_clock::time_point& tp, bool include_date) {
    if (tp == std::chrono::system_clock::time_point{}) return "--:--";
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_local{};
#if defined(_WIN32)
    localtime_s(&tm_local, &t);
#else
    localtime_r(&t, &tm_local);
#endif
    char buf[64];
    if (include_date) {
        std::snprintf(buf, sizeof(buf), "%02d/%02d %02d:%02d",
                      tm_local.tm_mday, tm_local.tm_mon + 1,
                      tm_local.tm_hour, tm_local.tm_min);
    } else {
        std::snprintf(buf, sizeof(buf), "%02d:%02d",
                      tm_local.tm_hour, tm_local.tm_min);
    }
    return std::string(buf);
}

static std::string content_nibble_to_string(uint8_t nibble1) {
    switch (nibble1) {
        case 0x1: return "Jornalismo";
        case 0x2: return "Esporte";
        case 0x3: return "Educativo";
        case 0x4: return "Novela/Serie";
        case 0x5: return "Show/Musica";
        case 0x6: return "Variedades";
        case 0x7: return "Reality Show";
        case 0x8: return "Infantil";
        case 0x9: return "Humor";
        case 0xA: return "Filme";
        case 0xB: return "Sorteio/Televendas";
        case 0xC: return "Debate/Entrevista";
        default:  return "";
    }
}

static std::string parental_rating_to_string(uint8_t rating_val) {
    uint8_t level = rating_val & 0x0F;
    if (level == 0) level = (rating_val >> 4) & 0x0F;
    switch (level) {
        case 0x01: return "Livre";
        case 0x02: return "10 anos";
        case 0x03: return "12 anos";
        case 0x04: return "14 anos";
        case 0x05: return "16 anos";
        case 0x06: return "18 anos";
        default:   return "";
    }
}

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

void PsiParser::SectionAssembler::push_packet(bool pusi, const uint8_t* payload, size_t len,
                                              std::vector<std::vector<uint8_t>>& completed) {
    if (!payload || len == 0) return;

    if (pusi) {
        size_t pointer_field = payload[0];
        if (1 + pointer_field > len) return;

        // Se tínhamos uma seção pendente sendo montada e há bytes antes do pointer_field
        if (pointer_field > 0 && expected_length > 0 && buffer.size() < expected_length) {
            size_t fill = std::min(pointer_field, expected_length - buffer.size());
            buffer.insert(buffer.end(), payload + 1, payload + 1 + fill);
            if (buffer.size() >= expected_length) {
                completed.push_back(std::move(buffer));
            }
        }
        buffer.clear();
        expected_length = 0;

        // Itera sobre TODAS as seções consecutivas empacotadas neste payload TS
        size_t offset = 1 + pointer_field;
        while (offset < len) {
            if (payload[offset] == 0xFF) {
                // Preenchimento (stuffing) até o fim do pacote TS
                break;
            }
            size_t avail = len - offset;
            if (avail < 3) {
                // Cabeçalho de seção fragmentado (menos de 3 bytes para ler section_length)
                buffer.assign(payload + offset, payload + len);
                expected_length = 3; // aguarda o restante do cabeçalho
                break;
            }

            size_t s_len = (((static_cast<size_t>(payload[offset + 1] & 0x0F)) << 8) | payload[offset + 2]) + 3;
            if (s_len > 4096) {
                // Tamanho ilegal de seção PSI/SI
                break;
            }

            if (avail >= s_len) {
                // Seção completa cabe inteiramente neste pacote TS
                completed.emplace_back(payload + offset, payload + offset + s_len);
                offset += s_len;
            } else {
                // Seção inicia aqui e continua no próximo pacote TS
                buffer.assign(payload + offset, payload + len);
                expected_length = s_len;
                break;
            }
        }
    } else {
        // Continuação de seção fragmentada entre pacotes TS consecutivos (PUSI=0)
        if (expected_length > 0 && !buffer.empty()) {
            if (buffer.size() < 3) {
                size_t needed_hdr = 3 - buffer.size();
                size_t to_copy_hdr = std::min(len, needed_hdr);
                buffer.insert(buffer.end(), payload, payload + to_copy_hdr);
                payload += to_copy_hdr;
                len -= to_copy_hdr;

                if (buffer.size() >= 3) {
                    expected_length = (((static_cast<size_t>(buffer[1] & 0x0F)) << 8) | buffer[2]) + 3;
                    if (expected_length > 4096) {
                        buffer.clear();
                        expected_length = 0;
                        return;
                    }
                } else {
                    return;
                }
            }

            if (expected_length > 0 && buffer.size() < expected_length && len > 0) {
                size_t needed = expected_length - buffer.size();
                size_t to_copy = std::min(len, needed);
                buffer.insert(buffer.end(), payload, payload + to_copy);
                if (buffer.size() >= expected_length) {
                    completed.push_back(std::move(buffer));
                    buffer.clear();
                    expected_length = 0;
                }
            }
        }
    }
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

    bool is_psi_pid = (pid == 0x0000 || pid == 0x0011 || pid == 0x0012 ||
                       pid == 0x0026 || pid == 0x0027 ||
                       (pid >= 0x1FC8 && pid <= 0x1FCF) ||
                       pmt_pids_.find(pid) != pmt_pids_.end());
    if (!is_psi_pid) return false;

    bool updated = false;
    std::vector<std::vector<uint8_t>> completed_sections;
    assemblers_[pid].push_packet(pusi, payload, payload_len, completed_sections);

    for (const auto& sec : completed_sections) {
        if (sec.empty()) continue;
        if (pid == 0x0000) {
            parse_pat(sec.data(), sec.size());
        } else if (pid == 0x0011) {
            parse_sdt(sec.data(), sec.size());
            updated = true;
        } else if (pid == 0x0012 || pid == 0x0026 || pid == 0x0027) {
            parse_eit(sec.data(), sec.size());
            updated = true;
        } else if ((pid >= 0x1FC8 && pid <= 0x1FCF) || pmt_pids_.find(pid) != pmt_pids_.end()) {
            parse_pmt(pid, sec.data(), sec.size());
            updated = true;
        }
    }

    return updated;
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
    present_events_.clear();
    following_events_.clear();
    schedule_events_.clear();
    assemblers_.clear();
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

void PsiParser::parse_eit(const uint8_t* payload, size_t len) {
    if (len < 14) return;
    uint8_t table_id = payload[0];
    // 0x4E = actual TS present/following
    // 0x4F = other TS present/following
    // 0x50..0x5F = actual TS schedule
    // 0x60..0x6F = other TS schedule
    if (table_id != 0x4E && (table_id < 0x50 || table_id > 0x5F)) return;

    size_t section_len = (((static_cast<size_t>(payload[1] & 0x0F)) << 8) | payload[2]);
    if (section_len + 3 > len) section_len = len - 3;

    uint16_t service_id = (static_cast<uint16_t>(payload[3]) << 8) | payload[4];
    uint8_t section_number = payload[6];

    size_t idx = 14;
    while (section_len + 3 >= 4 && idx + 12 <= section_len + 3 - 4) {
        uint16_t event_id = (static_cast<uint16_t>(payload[idx]) << 8) | payload[idx + 1];
        uint16_t mjd = (static_cast<uint16_t>(payload[idx + 2]) << 8) | payload[idx + 3];
        uint8_t bcd_h = payload[idx + 4];
        uint8_t bcd_m = payload[idx + 5];
        uint8_t bcd_s = payload[idx + 6];

        uint8_t dur_h = payload[idx + 7];
        uint8_t dur_m = payload[idx + 8];
        uint8_t dur_s = payload[idx + 9];

        size_t desc_loop_len = (((static_cast<size_t>(payload[idx + 10] & 0x0F)) << 8) | payload[idx + 11]);

        EpgEvent evt;
        evt.event_id = event_id;
        evt.service_id = service_id;

        int duration_sec = (((dur_h >> 4) * 10 + (dur_h & 0x0F)) * 3600) +
                           (((dur_m >> 4) * 10 + (dur_m & 0x0F)) * 60) +
                           ((dur_s >> 4) * 10 + (dur_s & 0x0F));
        evt.duration_sec = duration_sec;

        if (mjd != 0xFFFF && mjd != 0x0000) {
            evt.start_tp = parse_mjd_utc(mjd, bcd_h, bcd_m, bcd_s);
            if (evt.start_tp != std::chrono::system_clock::time_point{}) {
                evt.end_tp = evt.start_tp + std::chrono::seconds(duration_sec);
                evt.start_time_str = format_time_local(evt.start_tp, table_id >= 0x50);
                evt.end_time_str = format_time_local(evt.end_tp, false);
            }
        }

        if (table_id == 0x4E) {
            if (section_number == 0) evt.is_now = true;
            else if (section_number == 1) evt.is_next = true;
        }

        size_t d_idx = idx + 12;
        size_t d_end = d_idx + desc_loop_len;
        if (d_end > section_len + 3 - 4) d_end = section_len + 3 - 4;

        while (d_idx + 2 <= d_end && d_idx + 2 <= len) {
            uint8_t desc_tag = payload[d_idx];
            uint8_t desc_len = payload[d_idx + 1];
            size_t next_d = d_idx + 2 + desc_len;
            if (next_d > len) break;

            if (desc_tag == 0x4D && desc_len >= 4) { // Short event descriptor
                // lang (3 bytes)
                uint8_t name_len = payload[d_idx + 5];
                if (d_idx + 6 + name_len <= next_d) {
                    evt.title = clean_text_string(&payload[d_idx + 6], name_len);
                    size_t p = d_idx + 6 + name_len;
                    if (p < next_d) {
                        uint8_t text_len = payload[p];
                        if (p + 1 + text_len <= next_d) {
                            evt.short_desc = clean_text_string(&payload[p + 1], text_len);
                        }
                    }
                }
            } else if (desc_tag == 0x4E && desc_len >= 5) { // Extended event descriptor
                size_t p = d_idx + 6;
                if (p < next_d) {
                    uint8_t length_of_items = payload[p++];
                    p += length_of_items;
                    if (p < next_d) {
                        uint8_t text_len = payload[p++];
                        if (p + text_len <= next_d) {
                            std::string ext = clean_text_string(&payload[p], text_len);
                            if (!ext.empty()) {
                                if (!evt.extended_desc.empty()) evt.extended_desc += " ";
                                evt.extended_desc += ext;
                            }
                        }
                    }
                }
            } else if (desc_tag == 0x54 && desc_len >= 1) { // Content descriptor
                uint8_t nibble1 = (payload[d_idx + 2] >> 4) & 0x0F;
                evt.genre = content_nibble_to_string(nibble1);
            } else if (desc_tag == 0xD6 && desc_len >= 4) { // Parental rating (ABNT NBR 15603)
                uint8_t rating_val = payload[d_idx + 5];
                evt.rating = parental_rating_to_string(rating_val);
            } else if (desc_tag == 0x55 && desc_len >= 4) { // Parental rating (DVB)
                uint8_t rating_val = payload[d_idx + 5];
                if (rating_val > 0 && rating_val <= 0x0F) {
                    evt.rating = std::to_string(rating_val + 3) + " anos";
                }
            }

            d_idx = next_d;
        }

        if (evt.is_now) {
            present_events_[service_id] = evt;
        } else if (evt.is_next) {
            following_events_[service_id] = evt;
        } else {
            bool updated = false;
            for (auto& s : schedule_events_) {
                if (s.service_id == service_id && s.event_id == event_id) {
                    s = evt;
                    updated = true;
                    break;
                }
            }
            if (!updated) {
                schedule_events_.push_back(evt);
            }
        }

        idx += 12 + desc_loop_len;
    }
}

std::optional<EpgEvent> PsiParser::get_present_event(uint16_t service_id) const {
    if (service_id != 0) {
        auto it = present_events_.find(service_id);
        if (it != present_events_.end()) return it->second;
        return std::nullopt;
    }
    if (!present_events_.empty()) {
        return present_events_.begin()->second;
    }
    return std::nullopt;
}

std::optional<EpgEvent> PsiParser::get_following_event(uint16_t service_id) const {
    if (service_id != 0) {
        auto it = following_events_.find(service_id);
        if (it != following_events_.end()) return it->second;
        return std::nullopt;
    }
    if (!following_events_.empty()) {
        return following_events_.begin()->second;
    }
    return std::nullopt;
}

std::vector<EpgEvent> PsiParser::get_all_events(uint16_t service_id) const {
    std::vector<EpgEvent> all;
    auto pres = get_present_event(service_id);
    if (pres) all.push_back(*pres);
    auto foll = get_following_event(service_id);
    if (foll) all.push_back(*foll);

    for (const auto& evt : schedule_events_) {
        if (service_id == 0 || evt.service_id == service_id) {
            all.push_back(evt);
        }
    }
    return all;
}

} // namespace isdbt
