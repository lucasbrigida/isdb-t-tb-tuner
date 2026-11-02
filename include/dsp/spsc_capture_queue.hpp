#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace isdbt {

/**
 * @brief Bloco de captura de amostras I/Q brutas (65536 bytes = 32768 amostras I/Q de 8 bits).
 * Inclui metadados de sequencia e marcadores de descontinuidade.
 */
struct CaptureBlock {
    static constexpr std::size_t CapacityBytes = 32768 * 2;

    std::array<std::uint8_t, CapacityBytes> raw{};
    std::size_t size{0};
    std::uint64_t sequence{0};
    bool discontinuity_before{false};
};

/**
 * @brief Fila SPSC (Single Producer, Single Consumer) lock-free baseada em ring buffer.
 * O produtor (reader_thread) escreve em tail_ e o consumidor (DSP loop) le em head_.
 * Garante zero alocacoes e zero copias desnecessarias no consumidor.
 */
template <std::size_t SlotCount = 64>
class SpscCaptureQueue {
public:
    static_assert((SlotCount & (SlotCount - 1)) == 0, "SlotCount deve ser uma potencia de 2");

    struct WriteView {
        std::uint64_t position{0};
        CaptureBlock* block{nullptr};
    };

    struct ReadView {
        std::uint64_t position{0};
        const CaptureBlock* block{nullptr};
    };

    bool try_acquire_write(WriteView& view) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        const auto head = head_.load(std::memory_order_acquire);

        if ((tail - head) >= SlotCount) {
            return false;
        }

        view.position = tail;
        view.block = &slots_[tail & (SlotCount - 1)];
        return true;
    }

    void record_drop() noexcept {
        dropped_.fetch_add(1, std::memory_order_relaxed);
    }

    void publish_write(const WriteView& view) noexcept {
        tail_.store(view.position + 1, std::memory_order_release);
    }

    bool try_acquire_read(ReadView& view) const noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto tail = tail_.load(std::memory_order_acquire);

        if (head == tail) {
            return false;
        }

        view.position = head;
        view.block = &slots_[head & (SlotCount - 1)];
        return true;
    }

    void release_read(const ReadView& view) noexcept {
        head_.store(view.position + 1, std::memory_order_release);
    }

    std::size_t size() const noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto tail = tail_.load(std::memory_order_relaxed);
        return (tail >= head) ? static_cast<std::size_t>(tail - head) : 0;
    }

    bool empty() const noexcept {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

    std::uint64_t dropped_blocks() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

    void reset() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        dropped_.store(0, std::memory_order_relaxed);
    }

private:
    std::array<CaptureBlock, SlotCount> slots_{};
    alignas(64) std::atomic<std::uint64_t> tail_{0};
    alignas(64) mutable std::atomic<std::uint64_t> head_{0};
    alignas(64) std::atomic<std::uint64_t> dropped_{0};
};

} // namespace isdbt
