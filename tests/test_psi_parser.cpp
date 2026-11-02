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

int main() {
    test_build_pat_and_crc();
    test_parse_pat_roundtrip();
    test_parse_sdt_service_name();
    test_error_handling();

    std::cout << "\033[1;32m[PASS] Todos os testes de PsiParser passaram com sucesso!\033[0m\n";
    return 0;
}
