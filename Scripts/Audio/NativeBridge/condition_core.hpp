#pragma once
#include "capture_core.hpp"
#include <cmath>
#include <cstring>
#include <map>
#include <tuple>

// These are observations of numeric API REQUESTS, not Wwise's final voice/media
// selection or interpolated RTPC values. Unknown pre-hook state is never zero.
enum class ContextKind : std::uint32_t { State=1, Switch=2, Rtpc=3, ResetRtpc=4, Register=5, Unregister=6 };
inline const char* context_name(std::uint32_t kind) {
    switch (static_cast<ContextKind>(kind)) {
    case ContextKind::State: return "state_requested";
    case ContextKind::Switch: return "switch_requested";
    case ContextKind::Rtpc: return "rtpc_requested";
    case ContextKind::ResetRtpc: return "rtpc_reset_requested";
    case ContextKind::Register: return "register_requested";
    case ContextKind::Unregister: return "unregister_requested";
    default: return "unknown";
    }
}
inline bool context_global(const NativeAudioEvent& e) {
    return e.kind == static_cast<std::uint32_t>(ContextKind::State) || e.game_object_id == UINT64_MAX;
}
inline float context_float(const NativeAudioEvent& e) {
    float value; std::memcpy(&value, &e.callback_flags, sizeof(value)); return value;
}
inline void record_state(NativeAudioQueue& q, std::uint32_t group, std::uint32_t value) noexcept {
    q.push(group, UINT64_MAX, value, static_cast<std::uint32_t>(ContextKind::State));
}
inline void record_switch(NativeAudioQueue& q, std::uint32_t group, std::uint32_t value, std::uint64_t object) noexcept {
    q.push(group, object, value, static_cast<std::uint32_t>(ContextKind::Switch));
}
inline void record_rtpc(NativeAudioQueue& q, std::uint32_t id, float value, std::uint64_t object,
                        std::uint32_t duration, std::uint32_t curve, bool bypass) noexcept {
    std::uint32_t bits; std::memcpy(&bits, &value, sizeof(bits));
    q.push(id, object, bits, static_cast<std::uint32_t>(ContextKind::Rtpc), duration, curve, bypass ? 1U : 0U);
}

class ContextHistory {
public:
    static constexpr std::size_t capacity = 4096;
    using Key = std::tuple<std::uint32_t, std::uint64_t, std::uint32_t>;
    std::map<Key, NativeAudioEvent> values; // Present thread only, never audio hooks.
    std::uint64_t gaps = 0;
    std::uint64_t qpc_barrier = 0;

    void invalidate(std::uint64_t barrier = 0) {
        values.clear(); ++gaps;
        if (barrier > qpc_barrier) qpc_barrier = barrier;
    }
    void apply(const NativeAudioEvent& e) {
        if (qpc_barrier && e.qpc <= qpc_barrier) return; // late queued pre-loss records cannot revalidate history
        const auto kind = static_cast<ContextKind>(e.kind);
        if (kind == ContextKind::Register || kind == ContextKind::Unregister) {
            for (auto it = values.begin(); it != values.end();) {
                if (!context_global(it->second) && (e.game_object_id == UINT64_MAX ||
                                                    it->second.game_object_id == e.game_object_id)) it = values.erase(it);
                else ++it;
            }
            return;
        }
        if (kind == ContextKind::ResetRtpc) {
            values.erase(Key{static_cast<std::uint32_t>(ContextKind::Rtpc), e.game_object_id, e.event_id});
            // Global reset may change inherited object values. Do not guess the new value.
            if (e.game_object_id == UINT64_MAX) {
                for (auto it = values.begin(); it != values.end();) {
                    if (std::get<0>(it->first) == static_cast<std::uint32_t>(ContextKind::Rtpc) &&
                        std::get<2>(it->first) == e.event_id) it = values.erase(it);
                    else ++it;
                }
            }
            return;
        }
        if (kind != ContextKind::State && kind != ContextKind::Switch && kind != ContextKind::Rtpc) return;
        const Key key{e.kind, e.game_object_id, e.event_id};
        auto it = values.find(key);
        if (kind == ContextKind::Rtpc && !std::isfinite(context_float(e))) { values.erase(key); return; }
        if (it == values.end() && values.size() == capacity) invalidate(); // never silently evict old truth
        values[key] = e;
    }
};
