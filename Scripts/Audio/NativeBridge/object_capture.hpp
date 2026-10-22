#pragma once
#include "object_queue.hpp"
#include <cstdio>
#include <filesystem>
#include <string>

// Raw observations only. No dereference of GameObject*, no guessed ownership,
// no pre-F9 cache treated as current, and no file IO/allocation in the detour.
class AudioObjectCapture {
public:
    using KeyFunction = std::uint64_t (*)(void*, std::uint32_t);
    NativeObjectQueue events;
    KeyFunction original = nullptr;
    bool ready = false, trigger_ready = false;
    std::atomic<std::uint64_t> triggers{0}, hit_contexts{0};
    std::string trigger_error;
    std::string error;
    std::uint64_t rows = 0;
    NativeAudioEvent last{};

    std::uint64_t key(void* object, std::uint32_t sub_id) noexcept {
        const auto result = original(object, sub_id); // exact verified two-argument ABI
        events.push(sub_id, result, 0, 1, 0, 0, 0, reinterpret_cast<std::uintptr_t>(object));
        return result; // do not substitute the numeric ID or caller's object
    }
    void lifecycle(std::uint64_t id, bool registered) noexcept {
        events.push(0, id, 0, registered ? 2 : 3);
    }
    void trigger(std::uint64_t container, std::uint64_t owner, std::uint32_t trigger_id,
                 std::uint64_t emitter, std::uint64_t hit, std::uint64_t context, std::uint64_t origin) noexcept {
        triggers.fetch_add(1, std::memory_order_relaxed);
        if (hit && context && origin) hit_contexts.fetch_add(1, std::memory_order_relaxed);
        events.push(trigger_id,0,0,4,0,0,0,emitter,container,owner,hit,context,origin);
    }
    void drain(const LARGE_INTEGER& anchor, const LARGE_INTEGER& frequency, double capture_anchor) {
        std::array<NativeAudioEvent, 256> batch;
        for (std::size_t pass = 0; pass < NativeAudioQueue::capacity / batch.size(); ++pass) {
            const auto count = events.take(batch.data(), batch.size());
            if (!count) break;
            std::string body;
            for (std::size_t i = 0; i < count; ++i) {
                const auto& e = batch[i];
                if (e.kind == 1) last = e;
                if (file_ == INVALID_HANDLE_VALUE) continue;
                const double delta = e.qpc >= static_cast<std::uint64_t>(anchor.QuadPart) ?
                    static_cast<double>(e.qpc - anchor.QuadPart) / frequency.QuadPart :
                    -static_cast<double>(anchor.QuadPart - e.qpc) / frequency.QuadPart;
                char line[768];
                const bool trigger = e.kind == 4;
                const auto ptr = (e.kind == 1 || trigger) ? std::to_string(e.object_address) : "";
                const auto sub = (e.kind == 1 || trigger) ? std::to_string(e.event_id) : "";
                const auto id = trigger ? "" : std::to_string(e.game_object_id);
                const auto container = trigger ? std::to_string(e.container_address) : "";
                const auto owner = trigger ? std::to_string(e.owner_address) : "";
                const auto hit = trigger && e.hit_info_address ? std::to_string(e.hit_info_address) : "";
                const auto context = trigger && e.thread_context_address ? std::to_string(e.thread_context_address) : "";
                const auto origin = trigger && e.caller_rva ? std::to_string(e.caller_rva) : "";
                std::snprintf(line, sizeof(line), "%llu,%.9f,%llu,%llu,%u,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n",
                    static_cast<unsigned long long>(rows + i + 1), capture_anchor + delta,
                    static_cast<unsigned long long>(e.qpc), static_cast<unsigned long long>(e.native_frame), e.thread_id,
                    trigger ? "container_trigger_context" : e.kind == 1 ? "object_key_return" : e.kind == 2 ? "register_requested" : "unregister_requested",
                    id.c_str(), ptr.c_str(), sub.c_str(), trigger ? "native_call_arguments;trigger_id_NOT_event_id;pool_identity_not_unique_transaction" : "raw_engine_key;owner_unknown",
                    container.c_str(),owner.c_str(),hit.c_str(),context.c_str(),origin.c_str());
                body += line;
            }
            if (!body.empty()) {
                if (!write(body)) { error = "object_csv_write_failed"; close(); }
                else rows += count;
            }
        }
        const auto dropped = events.dropped.load();
        if (dropped != last_drops_) {
            last_drops_ = dropped;
            if (file_ != INVALID_HANDLE_VALUE && !write(marker("queue_gap;no_safe_complete_join"))) {
                error = "object_csv_gap_write_failed"; close();
            }
        }
    }
    void start(const std::filesystem::path& path) {
        close(); rows = 0; error.clear(); last_drops_ = events.dropped.load();
        if (!ready) { error = "object_hook_unavailable"; return; }
        file_ = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) { error = "object_csv_create_failed_no_overwrite"; return; }
        if (!write("row,capture_time_estimate_s,qpc,native_present_frame,thread_id,kind,game_object_id,engine_gameobject_address,sub_id,semantics,container_address,owner_gameobject_address,hit_info_address,thread_context_address,caller_rva\n") ||
            !write(marker("initial_coverage_unknown;no_pref9_baseline"))) { error = "object_csv_header_failed"; close(); }
    }
    void close() noexcept {
        if (file_ != INVALID_HANDLE_VALUE) { FlushFileBuffers(file_); CloseHandle(file_); file_ = INVALID_HANDLE_VALUE; }
    }
    bool recording() const noexcept { return file_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE file_ = INVALID_HANDLE_VALUE;
    std::uint64_t last_drops_ = 0;
    std::string marker(const char* text) {
        return std::to_string(++rows) + ",,,,,unknown,,,," + text + ",,,,,\n";
    }
    bool write(const std::string& body) {
        DWORD written = 0;
        return WriteFile(file_, body.data(), static_cast<DWORD>(body.size()), &written, nullptr) && written == body.size();
    }
};
