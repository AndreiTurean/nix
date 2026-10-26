#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <optional>

namespace nix {

// A fixed-size, lock-free SPSC ring buffer designed for shared memory or thread-to-thread IPC.
// T must be a Trivially Copyable type (POD) to safely live in shared memory.
template <typename T, std::uint64_t Capacity>
class SpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

public:
    SpscRingBuffer() : _head(0), _tail(0) {}

    // Push an item into the buffer (Called by the Producer node)
    bool push(const T& item) {
        const std::uint64_t currentTail = _tail.load(std::memory_order_relaxed);
        const std::uint64_t currentHead = _head.load(std::memory_order_acquire);

        // Check if the ring buffer is full
        if ((currentTail - currentHead) >= Capacity) {
            return false; // Full
        }

        // Write the data payload
        _buffer[currentTail & _mask] = item;

        // Publish the new tail position with release semantics
        _tail.store(currentTail + 1, std::memory_order_release);
        return true;
    }

    // Pop an item from the buffer (Called by the Consumer node)
    std::optional<T> pop() {
        const std::uint64_t currentHead = _head.load(std::memory_order_relaxed);
        const std::uint64_t currentTail = _tail.load(std::memory_order_acquire);

        // Check if the ring buffer is empty
        if (currentHead == currentTail) {
            return std::nullopt; // Empty
        }

        // Read the data payload
        T item = _buffer[currentHead & _mask];

        // Publish the new head position with release semantics
        _head.store(currentHead + 1, std::memory_order_release);
        return item;
    }

    bool empty() const {
        return _head.load(std::memory_order_acquire) == _tail.load(std::memory_order_acquire);
    }

    std::uint64_t size() const {
        std::uint64_t tailVal = _tail.load(std::memory_order_acquire);
        std::uint64_t headVal = _head.load(std::memory_order_acquire);
        return (tailVal >= headVal) ? (tailVal - headVal) : 0;
    }

    constexpr std::uint64_t capacity() const { return Capacity; }

private:
    static constexpr std::uint64_t _mask = Capacity - 1;

    // Align atomics to cache-line boundaries (typically 64 bytes) to avoid false sharing
    // when producer and consumer run on different CPU cores.
    alignas(64) std::atomic<std::uint64_t> _head;
    alignas(64) std::atomic<std::uint64_t> _tail;

    alignas(64) std::array<T, Capacity> _buffer;
};

} // namespace nix
