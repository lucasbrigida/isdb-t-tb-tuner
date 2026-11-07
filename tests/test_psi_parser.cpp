#include <iostream>
#include <cstdlib>
#include <vector>
#include <cstring>
#include "mpegts/psi_parser.hpp"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while(0)

// Helper: calcula CRC32 MPEG-2 para verificacao independente
static uint32_t compute_mpeg2_crc32(const uint8_t* data, size_t len) {
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

void test_build_pat_and_crc() {
    std::cout << "[Test] PsiParser::build_pat_packet e validacao CRC32..." << std::endl;

    isdbt::PsiParser parser;
    uint8_t pat_pkt[188];
    std::memset(pat_pkt, 0, sizeof(pat_pkt));

    TEST_ASSERT(parser.build_pat_packet(pat_pkt));

    // Valida cabecalho MPEG-TS
    TEST_ASSERT(pat_pkt[0] == 0x47); // Sync byte
    TEST_ASSERT((pat_pkt[1] & 0x80) == 0); // TEI = 0
    TEST_ASSERT((pat_pkt[1] & 0x40) != 0); // PUSI = 1
    uint16_t pid = ((static_cast<uint16_t>(pat_pkt[1] & 0x1F)) << 8) | pat_pkt[2];
    TEST_ASSERT(pid == 0x0000); // PAT PID = 0x0000
    TEST_ASSERT((pat_pkt[3] & 0x30) == 0x10); // Payload only

    // Pointer field
    TEST_ASSERT(pat_pkt[4] == 0x00);

    // Section header
    const uint8_t* sec = &pat_pkt[5];
    TEST_ASSERT(sec[0] == 0x00); // table_id = 0 (PAT)
    TEST_ASSERT((sec[1] & 0x80) != 0); // section_syntax_indicator = 1
    size_t section_len = (((static_cast<size_t>(sec[1] & 0x0F)) << 8) | sec[2]);
    TEST_ASSERT(section_len == 13); // 13 bytes

    // Validacao estrita do CRC32 MPEG-2 embutido
    uint32_t crc_calc = compute_mpeg2_crc32(sec, 12);
    uint32_t crc_embedded = (static_cast<uint32_t>(sec[12]) << 24) |
                            (static_cast<uint32_t>(sec[13]) << 16) |
                            (static_cast<uint32_t>(sec[14]) << 8)  |
                            static_cast<uint32_t>(sec[15]);
    TEST_ASSERT(crc_calc == crc_embedded);

    // Teste de continuidade: gerar proxima PAT incrementa o continuity counter
    uint8_t pat_pkt2[188];
    TEST_ASSERT(parser.build_pat_packet(pat_pkt2));
    uint8_t cc1 = pat_pkt[3] & 0x0F;
    uint8_t cc2 = pat_pkt2[3] & 0x0F;
    TEST_ASSERT(cc2 == ((cc1 + 1) & 0x0F));

    // Teste de reset e incremento de versao
    parser.reset();
    uint8_t pat_pkt3[188];
    TEST_ASSERT(parser.build_pat_packet(pat_pkt3));
    TEST_ASSERT((pat_pkt3[5 + 5] & 0x3E) == (2 << 1)); // version 2
    TEST_ASSERT(compute_mpeg2_crc32(&pat_pkt3[5], 12) ==
                ((static_cast<uint32_t>(pat_pkt3[5 + 12]) << 24) |
                 (static_cast<uint32_t>(pat_pkt3[5 + 13]) << 16) |
                 (static_cast<uint32_t>(pat_pkt3[5 + 14]) << 8)  |
                 static_cast<uint32_t>(pat_pkt3[5 + 15])));
}

void test_parse_pat_roundtrip() {
    std::cout << "[Test] PsiParser parse_packet com PAT sintetizada..." << std::endl;

    isdbt::PsiParser generator;
    uint8_t pat_pkt[188];
    generator.build_pat_packet(pat_pkt);

    // Alimenta outra instancia de PsiParser com o pacote PAT
    isdbt::PsiParser receiver;
    receiver.parse_packet(pat_pkt);

    const auto& svcs = receiver.get_services();
    TEST_ASSERT(!svcs.empty());
    auto it = svcs.find(0x0001);
    TEST_ASSERT(it != svcs.end());
    TEST_ASSERT(it->second.pmt_pid == 0x1FC8);
}

void test_pmt_discontinuity_pids() {
    std::cout << "[Test] PsiParser identifica PCR, video e audio para marcar descontinuidade..." << std::endl;

    uint8_t pmt_pkt[188];
    std::memset(pmt_pkt, 0xFF, sizeof(pmt_pkt));
    pmt_pkt[0] = 0x47;
    pmt_pkt[1] = 0x5F; // PUSI e PID 0x1FC8
    pmt_pkt[2] = 0xC8;
    pmt_pkt[3] = 0x10;
    pmt_pkt[4] = 0x00; // pointer field

    uint8_t* sec = &pmt_pkt[5];
    sec[0] = 0x02; // PMT
    sec[1] = 0xB0;
    sec[2] = 23; // section length
    sec[3] = 0x00;
    sec[4] = 0x01; // service id
    sec[5] = 0xC1;
    sec[6] = 0x00;
    sec[7] = 0x00;
    sec[8] = 0xE1;
    sec[9] = 0x00; // PCR PID 0x0100
    sec[10] = 0xF0;
    sec[11] = 0x00; // no program descriptors
    sec[12] = 0x1B; // H.264
    sec[13] = 0xE1;
    sec[14] = 0x01; // video PID 0x0101
    sec[15] = 0xF0;
    sec[16] = 0x00; // no ES descriptors
    sec[17] = 0x0F; // AAC
    sec[18] = 0xE1;
    sec[19] = 0x02; // audio PID 0x0102
    sec[20] = 0xF0;
    sec[21] = 0x00; // no ES descriptors

    isdbt::PsiParser parser;
    TEST_ASSERT(parser.parse_packet(pmt_pkt));
    const auto pids = parser.get_discontinuity_pids();
    TEST_ASSERT(pids.size() == 3);
    TEST_ASSERT(pids[0] == 0x0100);
    TEST_ASSERT(pids[1] == 0x0101);
    TEST_ASSERT(pids[2] == 0x0102);

    parser.reset();
    TEST_ASSERT(parser.get_discontinuity_pids().empty());
}

void test_parse_sdt_service_name() {
    std::cout << "[Test] PsiParser parsing de SDT com nome de emissora..." << std::endl;

    // Sintetiza um pacote TS com tabela SDT (PID 0x0011)
    uint8_t sdt_pkt[188];
    std::memset(sdt_pkt, 0xFF, sizeof(sdt_pkt));
    sdt_pkt[0] = 0x47; // Sync
    sdt_pkt[1] = 0x40 | 0x00; // PUSI = 1, PID high = 0x00
    sdt_pkt[2] = 0x11;        // PID low = 0x11 (SDT)
    sdt_pkt[3] = 0x10;        // Payload only, CC = 0
    sdt_pkt[4] = 0x00;        // Pointer field = 0

    uint8_t* sec = &sdt_pkt[5];
    sec[0] = 0x42; // table_id = 0x42 (SDT actual TS)

    std::string test_name = "GLOBO SP HD";
    std::string provider = "GLOBO";
    // Service descriptor 0x48: tag(1) + len(1) + type(1) + prov_len(1) + prov + name_len(1) + name
    size_t desc_body_len = 3 + provider.length() + test_name.length();
    size_t desc_total_len = 2 + desc_body_len;
    size_t loop_len = desc_total_len;
    size_t services_len = 5 + loop_len;
    size_t total_sec_len = 8 + services_len + 4; // header(8) + services + CRC(4)

    sec[1] = 0x80 | static_cast<uint8_t>((total_sec_len >> 8) & 0x0F);
    sec[2] = static_cast<uint8_t>(total_sec_len & 0xFF);
    sec[3] = 0x01; // transport_stream_id
    sec[4] = 0x02;
    sec[5] = 0xC1; // version, current_next
    sec[6] = 0x00; // section_number
    sec[7] = 0x00; // last_section_number
    sec[8] = 0x01; // original_network_id
    sec[9] = 0x03;
    sec[10] = 0xFF; // reserved

    // Service item: service_id = 0x07A1 (5 bytes)
    sec[11] = 0x07;
    sec[12] = 0xA1;
    sec[13] = 0xFC; // EIT flags
    sec[14] = static_cast<uint8_t>((loop_len >> 8) & 0x0F); // descriptors_loop_length (12 bits)
    sec[15] = static_cast<uint8_t>(loop_len & 0xFF);

    // Service descriptor (0x48) no indice 16
    sec[16] = 0x48; // tag
    sec[17] = static_cast<uint8_t>(desc_body_len); // desc len (excluindo os 2 bytes de tag e len)
    sec[18] = 0x01; // digital tv service
    sec[19] = static_cast<uint8_t>(provider.length());
    std::memcpy(&sec[20], provider.data(), provider.length());
    size_t p = 20 + provider.length();
    sec[p] = static_cast<uint8_t>(test_name.length());
    std::memcpy(&sec[p + 1], test_name.data(), test_name.length());

    // Calcula CRC
    size_t body_len = 3 + total_sec_len - 4;
    uint32_t crc = compute_mpeg2_crc32(sec, body_len);
    sec[body_len + 0] = static_cast<uint8_t>((crc >> 24) & 0xFF);
    sec[body_len + 1] = static_cast<uint8_t>((crc >> 16) & 0xFF);
    sec[body_len + 2] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    sec[body_len + 3] = static_cast<uint8_t>(crc & 0xFF);

    isdbt::PsiParser parser;
    bool updated = parser.parse_packet(sdt_pkt);
    TEST_ASSERT(updated);
    TEST_ASSERT(parser.get_primary_service_name() == test_name);

    const auto& svcs = parser.get_services();
    TEST_ASSERT(svcs.count(0x07A1) == 1);
    TEST_ASSERT(svcs.at(0x07A1).service_name == test_name);
}

void test_error_handling() {
    std::cout << "[Test] PsiParser deteccao de pacotes invalidos..." << std::endl;

    isdbt::PsiParser parser;
    TEST_ASSERT(!parser.parse_packet(nullptr));

    uint8_t bad_sync[188] = {0x00};
    TEST_ASSERT(!parser.parse_packet(bad_sync));

    uint8_t tei_pkt[188] = {0x47, 0x80, 0x00, 0x10}; // TEI bit set
    TEST_ASSERT(!parser.parse_packet(tei_pkt));

    uint8_t no_payload[188] = {0x47, 0x00, 0x00, 0x00}; // AFC = 0
    TEST_ASSERT(!parser.parse_packet(no_payload));
}

void test_parse_eit_epg() {
    std::cout << "[Test] PsiParser::parse_eit e extracao de EPG (Present/Following)..." << std::endl;

    isdbt::PsiParser parser;
    uint8_t eit_pkt[188];
    std::memset(eit_pkt, 0xFF, sizeof(eit_pkt));

    eit_pkt[0] = 0x47;
    eit_pkt[1] = 0x40; // PUSI = 1, PID 0x0012
    eit_pkt[2] = 0x12;
    eit_pkt[3] = 0x10; // payload only
    eit_pkt[4] = 0x00; // pointer field

    uint8_t* sec = &eit_pkt[5];
    sec[0] = 0x4E; // table_id: present/following actual TS
    sec[3] = 0x00; // service_id: 1
    sec[4] = 0x01;
    sec[5] = 0xC1;
    sec[6] = 0x00; // section_number = 0 (Present / Agora)
    sec[7] = 0x01; // last_section_number = 1
    sec[8] = 0x00; // ts_id = 1
    sec[9] = 0x01;
    sec[10] = 0x00; // on_id = 1
    sec[11] = 0x01;
    sec[12] = 0x00;
    sec[13] = 0x4E;

    // Event 1
    uint8_t* evt = &sec[14];
    evt[0] = 0x12; // event_id = 0x1234
    evt[1] = 0x34;
    evt[2] = 0xE6; // MJD = 59000 (0xE678)
    evt[3] = 0x78;
    evt[4] = 0x14; // BCD 14:30:00
    evt[5] = 0x30;
    evt[6] = 0x00;
    evt[7] = 0x01; // Duration BCD 01:15:00 (1h15m = 4500s)
    evt[8] = 0x15;
    evt[9] = 0x00;
    evt[10] = 0x00; // running_status=0, free_CA=0
    evt[11] = 0;    // desc_loop_len preenchido abaixo

    size_t d_idx = 12;
    // Descriptor 0x4D (Short event descriptor)
    evt[d_idx + 0] = 0x4D;
    const std::string title = "Jornal da Noite";
    const std::string desc = "Principais noticias";
    uint8_t d_len = static_cast<uint8_t>(3 + 1 + title.size() + 1 + desc.size());
    evt[d_idx + 1] = d_len;
    evt[d_idx + 2] = 'p';
    evt[d_idx + 3] = 'o';
    evt[d_idx + 4] = 'r';
    evt[d_idx + 5] = static_cast<uint8_t>(title.size());
    std::memcpy(&evt[d_idx + 6], title.data(), title.size());
    size_t desc_offset = d_idx + 6 + title.size();
    evt[desc_offset] = static_cast<uint8_t>(desc.size());
    std::memcpy(&evt[desc_offset + 1], desc.data(), desc.size());
    d_idx += 2 + d_len;

    // Descriptor 0x54 (Content descriptor - Jornalismo)
    evt[d_idx + 0] = 0x54;
    evt[d_idx + 1] = 0x02;
    evt[d_idx + 2] = 0x10; // nibble 1 = 0x1 (Jornalismo)
    evt[d_idx + 3] = 0x00;
    d_idx += 4;

    // Descriptor 0xD6 (Parental rating - Livre)
    evt[d_idx + 0] = 0xD6;
    evt[d_idx + 1] = 0x04;
    evt[d_idx + 2] = 'B';
    evt[d_idx + 3] = 'R';
    evt[d_idx + 4] = 'A';
    evt[d_idx + 5] = 0x01; // Livre
    d_idx += 6;

    size_t total_desc_len = d_idx - 12;
    evt[10] = static_cast<uint8_t>((total_desc_len >> 8) & 0x0F);
    evt[11] = static_cast<uint8_t>(total_desc_len & 0xFF);

    size_t total_sec_len = 11 + 12 + total_desc_len + 4; // header + event + descriptors + CRC
    sec[1] = static_cast<uint8_t>(0xB0 | ((total_sec_len >> 8) & 0x0F));
    sec[2] = static_cast<uint8_t>(total_sec_len & 0xFF);

    TEST_ASSERT(parser.parse_packet(eit_pkt));

    auto pres = parser.get_present_event(1);
    TEST_ASSERT(pres.has_value());
    TEST_ASSERT(pres->event_id == 0x1234);
    TEST_ASSERT(pres->title == "Jornal da Noite");
    TEST_ASSERT(pres->short_desc == "Principais noticias");
    TEST_ASSERT(pres->genre == "Jornalismo");
    TEST_ASSERT(pres->rating == "Livre");
    TEST_ASSERT(pres->duration_sec == 4500);
    TEST_ASSERT(pres->is_now == true);
    TEST_ASSERT(!pres->start_time_str.empty());
    TEST_ASSERT(pres->start_time_str == "14:30");
    TEST_ASSERT(pres->end_time_str == "15:45");
}

void test_packed_sections_same_payload() {
    std::cout << "[Test] PsiParser::SectionAssembler multiplas secoes empacotadas no mesmo pacote TS..." << std::endl;

    isdbt::PsiParser parser;
    uint8_t pkt[188];
    std::memset(pkt, 0xFF, sizeof(pkt));

    pkt[0] = 0x47;
    pkt[1] = 0x40; // PUSI = 1, PID 0x0012
    pkt[2] = 0x12;
    pkt[3] = 0x10; // payload only
    pkt[4] = 0x00; // pointer field = 0

    // Constrói Seção 1 (Section 0 - Present: "Programa Agora")
    uint8_t* sec1 = &pkt[5];
    sec1[0] = 0x4E;
    sec1[3] = 0x00; // service_id: 1
    sec1[4] = 0x01;
    sec1[5] = 0xC1;
    sec1[6] = 0x00; // section 0 = present
    sec1[7] = 0x01;
    sec1[8] = 0x00;
    sec1[9] = 0x01;
    sec1[10] = 0x00;
    sec1[11] = 0x01;
    sec1[12] = 0x00;
    sec1[13] = 0x4E;

    uint8_t* evt1 = &sec1[14];
    evt1[0] = 0x01; // event_id = 0x0100
    evt1[1] = 0x00;
    evt1[2] = 0xE6; evt1[3] = 0x78; // MJD
    evt1[4] = 0x10; evt1[5] = 0x00; evt1[6] = 0x00; // 10:00:00
    evt1[7] = 0x01; evt1[8] = 0x00; evt1[9] = 0x00; // 1h00m
    evt1[10] = 0; evt1[11] = 0; // desc_loop_len

    size_t d_idx1 = 12;
    evt1[d_idx1 + 0] = 0x4D;
    const std::string title1 = "Programa Agora";
    uint8_t d_len1 = static_cast<uint8_t>(3 + 1 + title1.size() + 1);
    evt1[d_idx1 + 1] = d_len1;
    evt1[d_idx1 + 2] = 'p'; evt1[d_idx1 + 3] = 'o'; evt1[d_idx1 + 4] = 'r';
    evt1[d_idx1 + 5] = static_cast<uint8_t>(title1.size());
    std::memcpy(&evt1[d_idx1 + 6], title1.data(), title1.size());
    evt1[d_idx1 + 6 + title1.size()] = 0;
    d_idx1 += 2 + d_len1;

    size_t total_desc1 = d_idx1 - 12;
    evt1[10] = static_cast<uint8_t>((total_desc1 >> 8) & 0x0F);
    evt1[11] = static_cast<uint8_t>(total_desc1 & 0xFF);

    size_t sec1_body_len = 11 + 12 + total_desc1 + 4; // header + event + descriptors + CRC
    sec1[1] = static_cast<uint8_t>(0xB0 | ((sec1_body_len >> 8) & 0x0F));
    sec1[2] = static_cast<uint8_t>(sec1_body_len & 0xFF);
    size_t total_sec1_bytes = 3 + sec1_body_len;

    // Constrói Seção 2 imediatamente a seguir (Section 1 - Following: "Programa A Seguir")
    uint8_t* sec2 = sec1 + total_sec1_bytes;
    sec2[0] = 0x4E;
    sec2[3] = 0x00; // service_id: 1
    sec2[4] = 0x01;
    sec2[5] = 0xC1;
    sec2[6] = 0x01; // section 1 = following
    sec2[7] = 0x01;
    sec2[8] = 0x00;
    sec2[9] = 0x01;
    sec2[10] = 0x00;
    sec2[11] = 0x01;
    sec2[12] = 0x00;
    sec2[13] = 0x4E;

    uint8_t* evt2 = &sec2[14];
    evt2[0] = 0x02; // event_id = 0x0200
    evt2[1] = 0x00;
    evt2[2] = 0xE6; evt2[3] = 0x78;
    evt2[4] = 0x11; evt2[5] = 0x00; evt2[6] = 0x00; // 11:00:00
    evt2[7] = 0x01; evt2[8] = 0x00; evt2[9] = 0x00; // 1h00m
    evt2[10] = 0; evt2[11] = 0;

    size_t d_idx2 = 12;
    evt2[d_idx2 + 0] = 0x4D;
    const std::string title2 = "Programa A Seguir";
    uint8_t d_len2 = static_cast<uint8_t>(3 + 1 + title2.size() + 1);
    evt2[d_idx2 + 1] = d_len2;
    evt2[d_idx2 + 2] = 'p'; evt2[d_idx2 + 3] = 'o'; evt2[d_idx2 + 4] = 'r';
    evt2[d_idx2 + 5] = static_cast<uint8_t>(title2.size());
    std::memcpy(&evt2[d_idx2 + 6], title2.data(), title2.size());
    evt2[d_idx2 + 6 + title2.size()] = 0;
    d_idx2 += 2 + d_len2;

    size_t total_desc2 = d_idx2 - 12;
    evt2[10] = static_cast<uint8_t>((total_desc2 >> 8) & 0x0F);
    evt2[11] = static_cast<uint8_t>(total_desc2 & 0xFF);

    size_t sec2_body_len = 11 + 12 + total_desc2 + 4;
    sec2[1] = static_cast<uint8_t>(0xB0 | ((sec2_body_len >> 8) & 0x0F));
    sec2[2] = static_cast<uint8_t>(sec2_body_len & 0xFF);
    size_t total_sec2_bytes = 3 + sec2_body_len;

    size_t used = 5 + total_sec1_bytes + total_sec2_bytes;
    TEST_ASSERT(used <= 188);

    TEST_ASSERT(parser.parse_packet(pkt));

    auto pres = parser.get_present_event(1);
    TEST_ASSERT(pres.has_value());
    TEST_ASSERT(pres->title == "Programa Agora");
    TEST_ASSERT(pres->is_now == true);

    auto foll = parser.get_following_event(1);
    TEST_ASSERT(foll.has_value());
    TEST_ASSERT(foll->title == "Programa A Seguir");
    TEST_ASSERT(foll->is_next == true);
}

int main() {
    test_build_pat_and_crc();
    test_parse_pat_roundtrip();
    test_pmt_discontinuity_pids();
    test_parse_sdt_service_name();
    test_parse_eit_epg();
    test_packed_sections_same_payload();
    test_error_handling();

    std::cout << "\033[1;32m[PASS] Todos os testes de PsiParser passaram com sucesso!\033[0m\n";
    return 0;
}
