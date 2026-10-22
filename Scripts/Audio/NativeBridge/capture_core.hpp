#pragma once
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>

// Every audio callback is scalar-only, allocation-free and never waits for a
// consumer lock. Full/contention drops are counted, never silently overwritten.
struct NativeAudioEvent {
    std::uint64_t qpc;
    std::uint64_t game_object_id;
    std::uint64_t native_frame;
    std::uint32_t event_id;
    std::uint32_t callback_flags;
    std::uint32_t thread_id;
    std::uint32_t kind = 0, duration = 0, curve = 0, bypass = 0;
    std::uint64_t object_address = 0;
    std::uint64_t container_address = 0, owner_address = 0, hit_info_address = 0, thread_context_address = 0, caller_rva = 0;
};

class NativeAudioQueue {
public:
    static constexpr std::size_t capacity = 8192;
    std::atomic<std::uint64_t> observed{0}, dropped{0}, frame{0};

    void start(std::uint64_t generation) noexcept {
        AcquireSRWLockExclusive(&lock_);
        head_ = count_ = 0;
        dropped.store(0);
        generation_.store(generation, std::memory_order_release);
        ReleaseSRWLockExclusive(&lock_);
    }
    void stop() noexcept { generation_.store(0, std::memory_order_release); }

    void push(std::uint32_t id, std::uint64_t object, std::uint32_t flags,
              std::uint32_t kind = 0, std::uint32_t duration = 0,
              std::uint32_t curve = 0, std::uint32_t bypass = 0,
              std::uint64_t object_address = 0, std::uint64_t container_address = 0,
              std::uint64_t owner_address = 0, std::uint64_t hit_info_address = 0,
              std::uint64_t thread_context_address = 0, std::uint64_t caller_rva = 0) noexcept {
        observed.fetch_add(1, std::memory_order_relaxed);
        const auto epoch = generation_.load(std::memory_order_acquire);
        if (!epoch) return;
        if (!TryAcquireSRWLockExclusive(&lock_)) {
            dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (epoch == generation_.load(std::memory_order_acquire)) {
            if (count_ == capacity) dropped.fetch_add(1, std::memory_order_relaxed);
            else {
                LARGE_INTEGER time{};
                QueryPerformanceCounter(&time);
                slots_[(head_ + count_) % capacity] = {
                    static_cast<std::uint64_t>(time.QuadPart), object,
                    frame.load(std::memory_order_relaxed), id, flags, GetCurrentThreadId(), kind, duration, curve, bypass,
                    object_address, container_address, owner_address, hit_info_address, thread_context_address, caller_rva};
                ++count_;
            }
        }
        ReleaseSRWLockExclusive(&lock_);
    }

    std::size_t take(NativeAudioEvent* out, std::size_t max) noexcept {
        AcquireSRWLockExclusive(&lock_);
        const auto count = count_ < max ? count_ : max;
        for (std::size_t i = 0; i != count; ++i) out[i] = slots_[(head_ + i) % capacity];
        head_ = (head_ + count) % capacity;
        count_ -= count;
        ReleaseSRWLockExclusive(&lock_);
        return count;
    }

private:
    SRWLOCK lock_ = SRWLOCK_INIT;
    std::array<NativeAudioEvent, capacity> slots_{};
    std::size_t head_ = 0, count_ = 0;
    std::atomic<std::uint64_t> generation_{0};
};
