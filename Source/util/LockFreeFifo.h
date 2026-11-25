// Mystery Box Synthesizer by Raygun
// Wait-free single-producer / single-consumer FIFO for trivially copyable
// messages between the message thread and the audio thread.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace mbx
{
template <typename T, size_t Capacity>
class LockFreeFifo
{
    static_assert (std::is_trivially_copyable_v<T>, "messages must be trivially copyable");
    static_assert ((Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");

public:
    /** Producer side. Returns false (dropping the message) if the queue is full. */
    bool push (const T& item) noexcept
    {
        const size_t w = write.load (std::memory_order_relaxed);
        const size_t r = read.load (std::memory_order_acquire);
        if (w - r >= Capacity) return false;
        slots[w & (Capacity - 1)] = item;
        write.store (w + 1, std::memory_order_release);
        return true;
    }
    /** Consumer side. */
    bool pop (T& out) noexcept
    {
        const size_t r = read.load (std::memory_order_relaxed);
        const size_t w = write.load (std::memory_order_acquire);
        if (r == w) return false;
        out = slots[r & (Capacity - 1)];
        read.store (r + 1, std::memory_order_release);
        return true;
    }
    size_t size() const noexcept { return write.load (std::memory_order_acquire) - read.load (std::memory_order_acquire); }

private:
    std::array<T, Capacity> slots {};
    alignas (64) std::atomic<size_t> write { 0 };
    alignas (64) std::atomic<size_t> read { 0 };
};

} // namespace mbx
