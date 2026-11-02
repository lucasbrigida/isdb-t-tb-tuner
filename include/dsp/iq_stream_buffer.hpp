#pragma once

#include <complex>
#include <vector>
#include <cstddef>
#include <cstring>

namespace isdbt {

/**
 * @brief Buffer contíguo de amostras IQ com offset deslizante e compactação amortizada.
 * 
 * Elimina chamadas frequentes a std::vector::erase() no loop de recepção.
 * As amostras são acessadas em O(1) relativas ao offset ativo. Apenas quando
 * mais da metade do buffer e pelo menos 32768 amostras foram consumidas, uma
 * única compactação residual é realizada via std::memmove, amortizando o custo
 * de cópia de memória para praticamente zero.
 */
class IqStreamBuffer {
public:
    explicit IqStreamBuffer(std::size_t reserve_capacity = 131072) {
        buf_.reserve(reserve_capacity);
    }

    void clear() noexcept {
        buf_.clear();
        offset_ = 0;
    }

    void assign(const std::complex<float>* data, std::size_t count) {
        buf_.assign(data, data + count);
        offset_ = 0;
    }

    void append(const std::complex<float>* data, std::size_t count) {
        buf_.insert(buf_.end(), data, data + count);
    }

    std::size_t available() const noexcept {
        return (buf_.size() >= offset_) ? (buf_.size() - offset_) : 0;
    }

    bool empty() const noexcept {
        return available() == 0;
    }

    const std::complex<float>& operator[](std::size_t index) const noexcept {
        return buf_[offset_ + index];
    }

    const std::complex<float>* data_at(std::size_t index) const noexcept {
        return buf_.data() + offset_ + index;
    }

    void consume(std::size_t count) {
        offset_ += count;
        if (offset_ > 32768 && offset_ > buf_.size() / 2) {
            std::size_t rem = buf_.size() - offset_;
            if (rem > 0) {
                std::memmove(buf_.data(), buf_.data() + offset_, rem * sizeof(std::complex<float>));
                buf_.resize(rem);
            } else {
                buf_.clear();
            }
            offset_ = 0;
        }
    }

    std::size_t offset() const noexcept { return offset_; }
    std::size_t raw_size() const noexcept { return buf_.size(); }

private:
    std::vector<std::complex<float>> buf_;
    std::size_t offset_{0};
};

} // namespace isdbt

