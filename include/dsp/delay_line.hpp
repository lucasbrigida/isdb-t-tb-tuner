#pragma once

#include <array>
#include <vector>
#include <cstddef>
#include <algorithm>

namespace isdbt {

/**
 * @brief Linha de atraso com capacidade estatica em std::array (zero alocacao no heap).
 * Utilizada no desentrelaçador de bytes Forney e no desentrelaçador de bits.
 */
template <typename T, std::size_t MaxCapacity>
class FixedDelayLine {
public:
    FixedDelayLine() = default;

    void init(std::size_t delay, const T& initial_val = T{}) {
        delay_ = std::min(delay, MaxCapacity);
        index_ = 0;
        data_.fill(initial_val);
    }

    void reset(const T& initial_val = T{}) {
        index_ = 0;
        data_.fill(initial_val);
    }

    std::size_t delay() const noexcept { return delay_; }

    inline T push_and_get(const T& val) noexcept {
        if (delay_ == 0) return val;
        T out = data_[index_];
        data_[index_] = val;
        index_++;
        if (index_ >= delay_) index_ = 0;
        return out;
    }

private:
    std::array<T, MaxCapacity> data_{};
    std::size_t delay_{0};
    std::size_t index_{0};
};

/**
 * @brief Linha de atraso com capacidade configuravel (alocacao unica na inicializacao).
 * Utilizada no desentrelaçador no tempo (384 portadoras).
 */
template <typename T>
class DynamicDelayLine {
public:
    DynamicDelayLine() = default;

    void init(std::size_t delay, const T& initial_val = T{}) {
        delay_ = delay;
        index_ = 0;
        if (delay > 0) {
            data_.assign(delay, initial_val);
        } else {
            data_.clear();
        }
    }

    void reset(const T& initial_val = T{}) {
        index_ = 0;
        std::fill(data_.begin(), data_.end(), initial_val);
    }

    std::size_t delay() const noexcept { return delay_; }

    inline T push_and_get(const T& val) noexcept {
        if (delay_ == 0) return val;
        T out = data_[index_];
        data_[index_] = val;
        index_++;
        if (index_ >= delay_) index_ = 0;
        return out;
    }

private:
    std::vector<T> data_;
    std::size_t delay_{0};
    std::size_t index_{0};
};

} // namespace isdbt
