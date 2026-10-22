#pragma once
#include "capture_core.hpp"

// Bounded MPSC sequence ring for object/key/trigger observations only. A
// consumer never holds a lock needed by a hook. No allocation, object reads,
// waits for a consumer, or unbounded retry in a producer. start() requires
// quiescent producers (production calls it once, BEFORE enabling the hooks).
class NativeObjectQueue {
public:
    static constexpr std::size_t capacity = NativeAudioQueue::capacity;
    std::atomic<std::uint64_t> observed{0}, dropped{0}, frame{0};
    std::atomic<std::uint64_t> full_dropped{0}, reservation_dropped{0};

    void start(std::uint64_t generation) noexcept {
        generation_.store(0, std::memory_order_release);
        enqueue_.store(0, std::memory_order_relaxed); dequeue_ = 0;
        for (std::size_t i = 0; i < capacity; ++i) slots_[i].sequence.store(i, std::memory_order_relaxed);
        dropped.store(0); full_dropped.store(0); reservation_dropped.store(0);
        generation_.store(generation, std::memory_order_release);
    }
    void stop() noexcept { generation_.store(0, std::memory_order_release); }

    void push(std::uint32_t id, std::uint64_t object, std::uint32_t flags,
              std::uint32_t kind = 0, std::uint32_t duration = 0,
              std::uint32_t curve = 0, std::uint32_t bypass = 0,
              std::uint64_t object_address = 0, std::uint64_t container = 0,
              std::uint64_t owner = 0, std::uint64_t hit = 0,
              std::uint64_t context = 0, std::uint64_t origin = 0) noexcept {
        observed.fetch_add(1, std::memory_order_relaxed);
        if (!generation_.load(std::memory_order_acquire)) return;
        // Observation time precedes reservation/publication; offline readers
        // sort by QPC instead of assuming cross-thread publication order.
        LARGE_INTEGER time{}; QueryPerformanceCounter(&time);
        auto pos = enqueue_.load(std::memory_order_relaxed);
        for (unsigned attempt = 0; attempt < 256; ++attempt) {
            auto& slot = slots_[pos % capacity];
            const auto seq = slot.sequence.load(std::memory_order_acquire);
            const auto difference = static_cast<std::int64_t>(seq - pos);
            if (difference == 0) {
                if (!enqueue_.compare_exchange_strong(pos, pos + 1, std::memory_order_relaxed)) continue;
                // Always publish a reserved cell, even if stop races with us.
                slot.event = {static_cast<std::uint64_t>(time.QuadPart), object,
                    frame.load(std::memory_order_relaxed), id, flags, GetCurrentThreadId(),
                    kind, duration, curve, bypass, object_address, container, owner, hit, context, origin};
                slot.sequence.store(pos + 1, std::memory_order_release);
                return;
            }
            if (difference < 0) {
                full_dropped.fetch_add(1, std::memory_order_relaxed);
                dropped.fetch_add(1, std::memory_order_relaxed); return;
            }
            pos = enqueue_.load(std::memory_order_relaxed);
        }
        reservation_dropped.fetch_add(1, std::memory_order_relaxed);
        dropped.fetch_add(1, std::memory_order_relaxed);
    }

    std::size_t take(NativeAudioEvent* out, std::size_t max) noexcept {
        std::size_t count = 0;
        while (count < max) {
            auto& slot = slots_[dequeue_ % capacity];
            if (slot.sequence.load(std::memory_order_acquire) != dequeue_ + 1) break;
            out[count++] = slot.event;
            slot.sequence.store(dequeue_ + capacity, std::memory_order_release);
            ++dequeue_;
        }
        return count;
    }
private:
    struct Cell { std::atomic<std::uint64_t> sequence{0}; NativeAudioEvent event{}; };
    std::array<Cell, capacity> slots_{};
    std::atomic<std::uint64_t> enqueue_{0};
    std::uint64_t dequeue_ = 0;
    std::atomic<std::uint64_t> generation_{0};
};
