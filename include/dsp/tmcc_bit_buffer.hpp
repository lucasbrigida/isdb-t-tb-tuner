#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace isdbt {

/**
 * @brief Buffer circular estático para os 204 bits de TMCC do quadro OFDM.
 * 
 * Substitui std::deque<uint8_t> com zero alocações dinâmicas no heap.
 * Mantém capacidade fixa de 204 elementos e garante indexação direta operator[](idx)
 * onde idx=0 representa o bit mais antigo e idx=203 o mais recente.
 */
class TmccBitBuffer {
public:
    void reset(uint8_t val = 0) noexcept {
        buf_.fill(val);
        head_ = 0;
    }

    void push_back(uint8_t bit) noexcept {
        buf_[head_] = bit;
        head_ = (head_ + 1 < 204) ? (head_ + 1) : 0;
    }

    uint8_t operator[](std::size_t idx) const noexcept {
        std::size_t pos = head_ + idx;
        if (pos >= 204) {
            pos -= 204;
        }
        return buf_[pos];
    }

    constexpr std::size_t size() const noexcept {
        return 204;
    }

private:
    std::array<uint8_t, 204> buf_{};
    std::size_t head_{0};
};

} // namespace isdbt

