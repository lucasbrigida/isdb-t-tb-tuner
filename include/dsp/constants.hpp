#pragma once

#include <cstddef>
#include <cstdint>

namespace isdbt::constants {

// Taxa de amostragem exata padrao (1/4 da taxa base de chip ISDB-T: 512 / (63 * 4) MHz)
constexpr float SAMPLE_RATE_HZ = 2031746.031746f;
constexpr uint32_t SAMPLE_RATE = 2031746;

// Escala e niveis neutros para decisoes suaves (soft decision)
constexpr float SOFT_NEUTRAL = 128.0f;
constexpr float SOFT_SCALE = 64.0f;
constexpr float MMSE_NOISE_FACTOR = 0.05f;

// Dimensoes de blocos e pacotes MPEG-TS / FEC
constexpr std::size_t RS_BLOCK_SIZE = 204;
constexpr std::size_t TS_PACKET_SIZE = 188;
constexpr std::size_t FORNEY_MAX_DELAY = 187;
constexpr std::size_t UDP_PACKET_SIZE = 1316; // 7 * 188 bytes

// Estrutura de subportadoras ISDB-T Camada A / 1-Seg (Modo 3)
constexpr std::size_t ACTIVE_CARRIERS = 432;
constexpr std::size_t DATA_CARRIERS = 384;
constexpr std::size_t MAX_BITS_PER_CARRIER = 6;

} // namespace isdbt::constants
