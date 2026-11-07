#include "mpegts/arib_b24_decoder.hpp"
#include <vector>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace isdbt {

static std::string codepoint_to_utf8(uint32_t cp) {
    std::string out;
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

bool AribB24Decoder::is_valid_utf8(const uint8_t* data, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t c = data[i];
        if (c <= 0x7F) {
            i++;
        } else if ((c & 0xE0) == 0xC0) {
            if (i + 1 >= len || (data[i + 1] & 0xC0) != 0x80 || c < 0xC2) return false;
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 >= len || (data[i + 1] & 0xC0) != 0x80 || (data[i + 2] & 0xC0) != 0x80) return false;
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            if (i + 3 >= len || (data[i + 1] & 0xC0) != 0x80 || (data[i + 2] & 0xC0) != 0x80 || (data[i + 3] & 0xC0) != 0x80) return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

std::string AribB24Decoder::decode_latin1(const uint8_t* data, size_t len) {
    std::string s;
    s.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        uint8_t c = data[i];
        if (c < 0x20 || c == 0x7F) {
            continue;
        }
        if (c < 0x80) {
            s.push_back(static_cast<char>(c));
        } else {
            switch (c) {
                case 0xA4: s += "\xE2\x82\xAC"; break; // €
                case 0xA6: s += "\xC5\xA0"; break;     // Š
                case 0xA8: s += "\xC5\xA1"; break;     // š
                case 0xB4: s += "\xC5\xBD"; break;     // Ž
                case 0xB8: s += "\xC5\xBE"; break;     // ž
                case 0xBC: s += "\xC5\x92"; break;     // Œ
                case 0xBD: s += "\xC5\x93"; break;     // œ
                case 0xBE: s += "\xC5\xB8"; break;     // Ÿ
                default:
                    s.push_back(static_cast<char>(0xC0 | (c >> 6)));
                    s.push_back(static_cast<char>(0x80 | (c & 0x3F)));
                    break;
            }
        }
    }
    return s;
}

std::string AribB24Decoder::arib_gaiji_to_utf8(uint8_t b1, uint8_t b2) {
    if (b1 == 0x7A) { // Ku 90
        switch (b2) {
            case 0x4D: return "[手]";
            case 0x4E: return "[字]";
            case 0x4F: return "[双]";
            case 0x50: return "[デ]";
            case 0x51: return "[S]";
            case 0x52: return "[二]";
            case 0x53: return "[多]";
            case 0x54: return "[解]";
            case 0x55: return "[SS]";
            case 0x56: return "[B]";
            case 0x57: return "[N]";
            case 0x58: return "[天]";
            case 0x59: return "[交]";
            case 0x5A: return "[映]";
            case 0x5B: return "[無]";
            case 0x5C: return "[料]";
            case 0x5D: return "[前]";
            case 0x5E: return "[後]";
            case 0x5F: return "[再]";
            case 0x60: return "[新]";
            case 0x61: return "[初]";
            case 0x62: return "[終]";
            case 0x63: return "[生]";
            case 0x64: return "[販]";
            case 0x65: return "[声]";
            case 0x66: return "[吹]";
            case 0x67: return "[PPV]";
            case 0x68: return "[HV]";
            case 0x69: return "[SD]";
            case 0x6A: return "[P]";
            case 0x6B: return "[W]";
            case 0x6C: return "[MV]";
            case 0x6D: return "[手話]";
            case 0x6E: return "[字内]";
            case 0x6F: return "[双方向]";
            case 0x70: return "[データ]";
            case 0x71: return "[音声1]";
            case 0x72: return "[音声2]";
            case 0x73: return "[多重]";
            case 0x74: return "[解説]";
            default: break;
        }
    } else if (b1 == 0x7C) { // Ku 92
        if (b2 >= 0x31 && b2 <= 0x44) {
            // Numbers in circles 1..20
            int n = b2 - 0x30;
            return "(" + std::to_string(n) + ")";
        }
    }
    return "";
}

std::string AribB24Decoder::jis_to_utf8(uint8_t b1, uint8_t b2) {
    if (b1 < 0x21 || b1 > 0x7E || b2 < 0x21 || b2 > 0x7E) return "";

    // 1. Símbolos Gaiji da ARIB (Ku 90..94)
    if (b1 >= 0x7A) {
        std::string gaiji = arib_gaiji_to_utf8(b1, b2);
        if (!gaiji.empty()) return gaiji;
    }

    uint8_t ku = b1 - 0x20;
    uint8_t ten = b2 - 0x20;

    // 2. Ku 4: Hiragana (contíguo U+3041 .. U+3093)
    if (ku == 4) {
        if (ten >= 1 && ten <= 83) {
            return codepoint_to_utf8(0x3040 + ten);
        } else if (ten == 84) {
            return codepoint_to_utf8(0x309B); // ゛
        } else if (ten == 85) {
            return codepoint_to_utf8(0x309C); // ゜
        }
    }

    // 3. Ku 5: Katakana (contíguo U+30A1 .. U+30F6)
    if (ku == 5) {
        if (ten >= 1 && ten <= 86) {
            return codepoint_to_utf8(0x30A0 + ten);
        } else if (ten == 87) {
            return codepoint_to_utf8(0x309B); // ゛
        } else if (ten == 88) {
            return codepoint_to_utf8(0x309C); // ゜
        }
    }

    // 4. Ku 1: Pontuação comum
    if (ku == 1) {
        switch (ten) {
            case 1:  return " ";      // Espaço ideográfico
            case 2:  return "\xE3\x80\x81"; // 、
            case 3:  return "\xE3\x80\x82"; // 。
            case 4:  return ",";
            case 5:  return ".";
            case 6:  return "\xE3\x83\xBB"; // ・
            case 7:  return ":";
            case 8:  return ";";
            case 9:  return "?";
            case 10: return "!";
            case 28: return "-";
            case 33: return "/";
            case 44: return "\xE3\x80\xBC"; // ー
            case 46: return "\xE3\x80\x8C"; // 「
            case 47: return "\xE3\x80\x8D"; // 」
            case 48: return "\xE3\x80\x8E"; // 『
            case 49: return "\xE3\x80\x8F"; // 』
            case 50: return "(";
            case 51: return ")";
            case 52: return "[";
            case 53: return "]";
            case 60: return "\xE3\x80\x9C"; // 〜
            default: break;
        }
    }

    // 5. Ku 3: Alfanumérico fullwidth mapeado para ASCII padrão
    if (ku == 3) {
        if (ten >= 16 && ten <= 25) {
            return std::string(1, static_cast<char>('0' + (ten - 16)));
        } else if (ten >= 33 && ten <= 58) {
            return std::string(1, static_cast<char>('A' + (ten - 33)));
        } else if (ten >= 65 && ten <= 90) {
            return std::string(1, static_cast<char>('a' + (ten - 65)));
        }
    }

    // 6. Conversão de JIS X 0208 para Shift-JIS
    uint8_t s1 = static_cast<uint8_t>(((ku - 1) >> 1) + 0x81);
    if (s1 >= 0xA0) s1 += 0x40;
    uint8_t s2 = 0;
    if (ku & 1) {
        s2 = static_cast<uint8_t>(ten + 0x3F);
        if (ten >= 64) s2 += 1;
    } else {
        s2 = static_cast<uint8_t>(ten + 0x9E);
    }

#if defined(_WIN32)
    char sjis[2] = { static_cast<char>(s1), static_cast<char>(s2) };
    wchar_t wbuf[4] = { 0 };
    int wlen = MultiByteToWideChar(932, 0, sjis, 2, wbuf, 4);
    if (wlen > 0) {
        char utf8[16] = { 0 };
        int ulen = WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen, utf8, sizeof(utf8), NULL, NULL);
        if (ulen > 0) {
            return std::string(utf8, ulen);
        }
    }
#endif

    return "";
}

std::string AribB24Decoder::decode_arib_b24(const uint8_t* data, size_t len) {
    enum class GraphicSet {
        Kanji,        // 2-byte JIS X 0208
        Alphanumeric, // 1-byte ASCII / JIS Roman
        Hiragana,     // 1-byte Hiragana
        Katakana      // 1-byte Katakana
    };

    GraphicSet g[4] = {
        GraphicSet::Kanji,
        GraphicSet::Alphanumeric,
        GraphicSet::Hiragana,
        GraphicSet::Katakana
    };

    int gl = 0; // Aponta inicialmente para G0 (Kanji)
    int gr = 2; // Aponta inicialmente para G2 (Hiragana)
    int single_shift = -1; // -1: sem shift temporario, 2: G2, 3: G3

    std::string out;
    out.reserve(len * 2);

    size_t i = 0;
    while (i < len) {
        uint8_t c = data[i++];

        // Caracteres de controle C0 e trocas de conjunto
        if (c == 0x0E) { // LS1 / SO (G1 -> GL)
            gl = 1;
            continue;
        } else if (c == 0x0F) { // LS0 / SI (G0 -> GL)
            gl = 0;
            continue;
        } else if (c == 0x19) { // SS2 (Single shift G2)
            single_shift = 2;
            continue;
        } else if (c == 0x1D) { // SS3 (Single shift G3)
            single_shift = 3;
            continue;
        } else if (c == 0x0D) { // APR (Quebra de linha)
            out.push_back('\n');
            continue;
        } else if (c == 0x20) { // SP (Espaço)
            out.push_back(' ');
            continue;
        } else if (c == 0x1B) { // Sequência de Escape (ESC)
            if (i >= len) break;
            uint8_t esc1 = data[i++];
            if (esc1 == 0x6E) { // LS2 (G2 -> GL)
                gl = 2;
            } else if (esc1 == 0x6F) { // LS3 (G3 -> GL)
                gl = 3;
            } else if (esc1 == 0x7E) { // LS1R (G1 -> GR)
                gr = 1;
            } else if (esc1 == 0x7D) { // LS2R (G2 -> GR)
                gr = 2;
            } else if (esc1 == 0x7C) { // LS3R (G3 -> GR)
                gr = 3;
            } else if (esc1 == 0x24) { // Designação de 2-byte set
                if (i >= len) break;
                uint8_t esc2 = data[i++];
                if (esc2 >= 0x28 && esc2 <= 0x2B) {
                    if (i >= len) break;
                    uint8_t f = data[i++];
                    int target = esc2 - 0x28;
                    (void)f;
                    g[target] = GraphicSet::Kanji;
                } else {
                    g[0] = GraphicSet::Kanji;
                }
            } else if (esc1 >= 0x28 && esc1 <= 0x2B) { // Designação de 1-byte set
                if (i >= len) break;
                uint8_t f = data[i++];
                int target = esc1 - 0x28;
                if (f == 0x30) {
                    g[target] = GraphicSet::Hiragana;
                } else if (f == 0x31) {
                    g[target] = GraphicSet::Katakana;
                } else {
                    g[target] = GraphicSet::Alphanumeric;
                }
            }
            continue;
        }

        // Códigos de controle C1 (0x80..0x9F)
        if (c >= 0x80 && c <= 0x9F) {
            if (c >= 0x88 && c <= 0x8B) {
                // Controles de tamanho (SSZ, MSZ, NSZ, SZX) - avanço de 0 parâmetros
                continue;
            } else if (c == 0x90 || c == 0x91 || c == 0x93 || c == 0x94 || c == 0x97 || c == 0x98) {
                // Controles com 1 byte de parâmetro (cor, realce, flash, etc.)
                if (i < len) i++;
                continue;
            } else if (c == 0x9B) { // CSI (Control Sequence Introducer)
                while (i < len && (data[i] < 0x40 || data[i] > 0x7E)) {
                    i++;
                }
                if (i < len) i++; // descarta byte final delimitador
                continue;
            }
            continue;
        }

        // Processamento de caracteres gráficos em GL (0x21..0x7E)
        if (c >= 0x21 && c <= 0x7E) {
            GraphicSet active = (single_shift >= 0) ? g[single_shift] : g[gl];
            single_shift = -1;

            if (active == GraphicSet::Kanji) {
                if (i >= len) break;
                uint8_t c2 = data[i++];
                out += jis_to_utf8(c, c2);
            } else if (active == GraphicSet::Alphanumeric) {
                out.push_back(static_cast<char>(c));
            } else if (active == GraphicSet::Hiragana) {
                // Hiragana 1-byte mapeia para Ku 4
                out += jis_to_utf8(0x24, c);
            } else if (active == GraphicSet::Katakana) {
                // Katakana 1-byte mapeia para Ku 5
                out += jis_to_utf8(0x25, c);
            }
            continue;
        }

        // Processamento de caracteres gráficos em GR (0xA1..0xFE)
        if (c >= 0xA1 && c <= 0xFE) {
            uint8_t c_mapped = c & 0x7F;
            GraphicSet active = g[gr];

            if (active == GraphicSet::Kanji) {
                if (i >= len) break;
                uint8_t c2 = data[i++] & 0x7F;
                out += jis_to_utf8(c_mapped, c2);
            } else if (active == GraphicSet::Alphanumeric) {
                out.push_back(static_cast<char>(c_mapped));
            } else if (active == GraphicSet::Hiragana) {
                out += jis_to_utf8(0x24, c_mapped);
            } else if (active == GraphicSet::Katakana) {
                out += jis_to_utf8(0x25, c_mapped);
            }
            continue;
        }
    }

    return out;
}

std::string AribB24Decoder::decode_to_utf8(const uint8_t* data, size_t len) {
    if (!data || len == 0) return "";

    size_t start_idx = 0;
    if (data[0] == 0x15) { // Marcador DVB/ABNT UTF-8
        start_idx = 1;
        const uint8_t* p = data + start_idx;
        size_t rem = len - start_idx;
        if (is_valid_utf8(p, rem)) {
            std::string s;
            s.reserve(rem);
            for (size_t i = 0; i < rem; ++i) {
                if (p[i] >= 0x20 && p[i] != 0x7F) s.push_back(static_cast<char>(p[i]));
            }
            return s;
        }
    } else if (data[0] < 0x20 && data[0] != 0x0E && data[0] != 0x0F && data[0] != 0x1B) {
        start_idx = 1;
    }

    const uint8_t* p = data + start_idx;
    size_t rem = (len > start_idx) ? (len - start_idx) : 0;
    if (rem == 0) return "";

    // 1. Verifica marcadores/escapes explícitos de ARIB STD-B24
    bool has_arib_markers = false;
    for (size_t i = 0; i < rem; ++i) {
        if (p[i] == 0x1B || p[i] == 0x0E || p[i] == 0x0F || p[i] == 0x19 || p[i] == 0x1D) {
            has_arib_markers = true;
            break;
        }
    }

    if (has_arib_markers) {
        return decode_arib_b24(p, rem);
    }

    // 2. Se for UTF-8 válido e contiver caracteres multibyte UTF-8 válidos, limpa e retorna
    if (is_valid_utf8(p, rem)) {
        std::string s;
        s.reserve(rem);
        for (size_t i = 0; i < rem; ++i) {
            if (p[i] >= 0x20 && p[i] != 0x7F) {
                s.push_back(static_cast<char>(p[i]));
            }
        }
        return s;
    }

    // 3. Fallback: Latin-1 / ISO-8859-15 (Brasil ABNT NBR 15603-2)
    return decode_latin1(p, rem);
}

} // namespace isdbt
