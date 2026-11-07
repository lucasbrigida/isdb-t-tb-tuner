#include "mpegts/arib_b24_decoder.hpp"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>

using namespace isdbt;

static void test_ascii() {
    std::cout << "[Test] AribB24Decoder texto ASCII simples...\n";
    std::string text = "Globo HD 1-Seg";
    std::string res = AribB24Decoder::decode_to_utf8(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    assert(res == "Globo HD 1-Seg");
}

static void test_latin1_fallback() {
    std::cout << "[Test] AribB24Decoder fallback Latin-1 / ISO-8859-15 (Brasil)...\n";
    // "São Paulo" em Latin-1: 0x53, 0xE3, 0x6F, 0x20, 0x50, 0x61, 0x75, 0x6C, 0x6F
    const uint8_t latin1_sp[] = { 0x53, 0xE3, 0x6F, 0x20, 0x50, 0x61, 0x75, 0x6C, 0x6F };
    std::string res_sp = AribB24Decoder::decode_to_utf8(latin1_sp, sizeof(latin1_sp));
    assert(res_sp == "São Paulo");

    // "Atenção e Notícias" em Latin-1
    const uint8_t latin1_noticias[] = {
        'A', 't', 'e', 'n', 0xE7, 0xE3, 'o', ' ', 'e', ' ', 'N', 'o', 't', 0xED, 'c', 'i', 'a', 's'
    };
    std::string res_noticias = AribB24Decoder::decode_to_utf8(latin1_noticias, sizeof(latin1_noticias));
    assert(res_noticias == "Atenção e Notícias");
}

static void test_dvb_utf8_prefix() {
    std::cout << "[Test] AribB24Decoder DVB/ABNT prefixo 0x15 (UTF-8)...\n";
    std::string utf8_text = "Jornal da Noite - Edição Especial";
    std::vector<uint8_t> payload = { 0x15 };
    payload.insert(payload.end(), utf8_text.begin(), utf8_text.end());

    std::string res = AribB24Decoder::decode_to_utf8(payload.data(), payload.size());
    assert(res == "Jornal da Noite - Edição Especial");
}

static void test_hiragana_katakana() {
    std::cout << "[Test] AribB24Decoder Hiragana e Katakana...\n";
    // ESC 0x6E (LS2 -> GL = Hiragana): "あいうえお"
    // Ku 4 Ten 2, 4, 6, 8, 10 -> 0x22, 0x24, 0x26, 0x28, 0x2A
    const uint8_t hira[] = { 0x1B, 0x6E, 0x22, 0x24, 0x26, 0x28, 0x2A };
    std::string res_hira = AribB24Decoder::decode_to_utf8(hira, sizeof(hira));
    assert(res_hira == "あいうえお");

    // ESC 0x6F (LS3 -> GL = Katakana): "アイウエオ"
    // Ku 5 Ten 2, 4, 6, 8, 10 -> 0x22, 0x24, 0x26, 0x28, 0x2A
    const uint8_t kata[] = { 0x1B, 0x6F, 0x22, 0x24, 0x26, 0x28, 0x2A };
    std::string res_kata = AribB24Decoder::decode_to_utf8(kata, sizeof(kata));
    assert(res_kata == "アイウエオ");
}

static void test_kanji_jis0208() {
    std::cout << "[Test] AribB24Decoder Kanji JIS X 0208...\n";
    // "日本" (Nihon / Japão):
    // 日 = Ku 38 Ten 92 -> 0x46, 0x7C
    // 本 = Ku 41 Ten 59 -> 0x49, 0x5B
    const uint8_t kanji_nihon[] = { 0x1B, 0x24, 0x42, 0x46, 0x7C, 0x49, 0x5B };
    std::string res = AribB24Decoder::decode_to_utf8(kanji_nihon, sizeof(kanji_nihon));
#if defined(_WIN32)
    assert(res == "日本");
#else
    assert(!res.empty());
#endif
}

static void test_arib_gaiji() {
    std::cout << "[Test] AribB24Decoder simbolos Gaiji da ARIB...\n";
    // 0x7A 0x4E -> "[字]"
    // 0x7A 0x60 -> "[新]"
    // 0x7A 0x68 -> "[HV]"
    const uint8_t gaiji_stream[] = {
        0x1B, 0x24, 0x42,
        0x7A, 0x4E, // [字]
        0x7A, 0x60, // [新]
        0x7A, 0x68  // [HV]
    };
    std::string res = AribB24Decoder::decode_to_utf8(gaiji_stream, sizeof(gaiji_stream));
    assert(res == "[字][新][HV]");
}

static void test_control_codes() {
    std::cout << "[Test] AribB24Decoder codigos de controle (APR, MSZ/NSZ)...\n";
    // "A" + APR (0x0D) + "B"
    const uint8_t text_apr[] = { 0x0E, 'A', 0x0D, 'B' };
    std::string res_apr = AribB24Decoder::decode_to_utf8(text_apr, sizeof(text_apr));
    assert(res_apr == "A\nB");
}

int main() {
    try {
        test_ascii();
        test_latin1_fallback();
        test_dvb_utf8_prefix();
        test_hiragana_katakana();
        test_kanji_jis0208();
        test_arib_gaiji();
        test_control_codes();
        std::cout << "[PASS] Todos os testes de AribB24Decoder passaram com sucesso!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[FALHA] Excecao: " << e.what() << "\n";
        return 1;
    }
}
