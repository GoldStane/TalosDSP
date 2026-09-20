#pragma once

#include <array>
#include <atomic>
#include <cstddef>

template <typename T, size_t Capacity>
class SPSCRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static constexpr size_t Mask = Capacity - 1;

    std::array<T, Capacity> buffer_;
    std::atomic<size_t> write_idx_{0};
    std::atomic<size_t> read_idx_{0};

public:
    bool try_push(const T& item) noexcept {
        size_t w = write_idx_.load(std::memory_order_relaxed);
        size_t next = (w + 1) & Mask;
        if (next == read_idx_.load(std::memory_order_acquire)) return false;
        buffer_[w] = item;
        write_idx_.store(next, std::memory_order_release);
        return true;
    }

    bool try_pop(T& item) noexcept {
        size_t r = read_idx_.load(std::memory_order_relaxed);
        if (r == write_idx_.load(std::memory_order_acquire)) return false;
        item = buffer_[r];
        read_idx_.store((r + 1) & Mask, std::memory_order_release);
        return true;
    }

    size_t available() const noexcept {
        size_t w = write_idx_.load(std::memory_order_acquire);
        size_t r = read_idx_.load(std::memory_order_acquire);
        return (w - r) & Mask;
    }

    size_t capacity() const noexcept { return Capacity; }
};

struct FrameSnapshot {
    float rms_ch[2];
    float peak_ch[2];
    float zcr_ch[2];
};