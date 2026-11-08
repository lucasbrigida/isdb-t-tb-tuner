#pragma once

#include <string>
#include <cstdint>
#include <cstddef>

namespace isdbt {

/**
 * @brief Decodificador de texto para padroes ISDB-T (ARIB STD-B24) e ISDB-Tb (ABNT NBR 15603-2).
 * 
 * Suporta auto-deteccao com fallback hibrido:
 * - DVB / ABNT UTF-8 (prefixo 0x15 ou UTF-8 valido)
 * - ABNT Latin-1 / ISO-8859-15 (Brasil / America Latina)
 * - ARIB STD-B24 (Japao): JIS X 0208 Kanji, Hiragana, Katakana, Alfanumerico, quebras de linha e Gaiji.
 */
class AribB24Decoder {
public:
    /**
     * @brief Decodifica uma sequencia de bytes de titulo ou descricao para uma string UTF-8.
     * @param data Ponteiro para os bytes brutos do descritor de texto MPEG-TS.
     * @param len Comprimento em bytes.
     * @return String limpa codificada em UTF-8.
     */
    static std::string decode_to_utf8(const uint8_t* data, size_t len);

    /**
     * @brief Converte caractere JIS X 0208 (b1, b2) para codepoint Unicode UTF-8.
     * @param b1 Primeiro byte JIS (0x21..0x7E, Ku 1..94).
     * @param b2 Segundo byte JIS (0x21..0x7E, Ten 1..94).
     * @return String UTF-8 correspondente.
     */
    static std::string jis_to_utf8(uint8_t b1, uint8_t b2);

private:
    static bool is_valid_utf8(const uint8_t* data, size_t len);
    static std::string decode_arib_b24(const uint8_t* data, size_t len);
    static std::string decode_latin1(const uint8_t* data, size_t len);
    static std::string arib_gaiji_to_utf8(uint8_t b1, uint8_t b2);
};

} // namespace isdbt
