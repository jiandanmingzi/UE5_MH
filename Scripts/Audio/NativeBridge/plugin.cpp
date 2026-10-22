#include "capture_core.hpp"
#include "condition_core.hpp"
#include "object_capture.hpp"
#include "game_profile.hpp"
#include "protocol.hpp"
#include "reframework_abi.hpp"
#include <MinHook.h>
#include <bcrypt.h>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <vector>

extern "C" void MhgzPostEntry();
extern "C" void MhgzStateEntry();
extern "C" void MhgzSwitchEntry();
extern "C" void MhgzRtpcEntry();
extern "C" void MhgzResetEntry();
extern "C" void MhgzRegisterEntry();
extern "C" void MhgzUnregisterEntry();
extern "C" void MhgzTriggerEntry();
extern "C" {
void* MhgzOriginalPost = nullptr;
void* MhgzOriginalState = nullptr;
void* MhgzOriginalSwitch = nullptr;
void* MhgzOriginalRtpc = nullptr;
void* MhgzOriginalReset = nullptr;
void* MhgzOriginalRegister = nullptr;
void* MhgzOriginalUnregister = nullptr;
void* MhgzOriginalTrigger = nullptr;
void* MhgzSpatialReturn = nullptr;
void* MhgzHitReturnA = nullptr;
void* MhgzHitReturnB = nullptr;
}
static NativeAudioQueue queue, context_queue;
static AudioObjectCapture objects;
static std::uintptr_t image_base = 0;
extern "C" void MhgzObserveTrigger(std::uint64_t container, std::uint64_t owner, std::uint32_t id, std::uint64_t emitter,
                                    std::uint64_t hit, std::uint64_t context, std::uint64_t origin) noexcept {
    objects.trigger(container,owner,id,emitter,hit,context,origin ? origin-image_base : 0);
}
static std::uint64_t object_key(void* object, std::uint32_t sub_id) noexcept { return objects.key(object, sub_id); }
extern "C" void MhgzObserveEvent(std::uint32_t event, std::uint64_t object, std::uint32_t flags) noexcept {
    queue.push(event, object, flags);
}
extern "C" void MhgzObserveState(std::uint32_t group, std::uint32_t value) noexcept { record_state(context_queue, group, value); }
extern "C" void MhgzObserveSwitch(std::uint32_t group, std::uint32_t value, std::uint64_t object) noexcept { record_switch(context_queue, group, value, object); }
extern "C" void MhgzObserveRtpc(std::uint32_t id, float value, std::uint64_t object, std::uint32_t duration,
                               std::uint32_t curve, bool bypass) noexcept { record_rtpc(context_queue, id, value, object, duration, curve, bypass); }
extern "C" void MhgzObserveReset(std::uint32_t id, std::uint64_t object) noexcept { context_queue.push(id, object, 0, static_cast<std::uint32_t>(ContextKind::ResetRtpc)); }
extern "C" void MhgzObserveRegister(std::uint64_t object) noexcept { context_queue.push(0, object, 0, static_cast<std::uint32_t>(ContextKind::Register)); objects.lifecycle(object, true); }
extern "C" void MhgzObserveUnregister(std::uint64_t object) noexcept { context_queue.push(0, object, 0, static_cast<std::uint32_t>(ContextKind::Unregister)); objects.lifecycle(object, false); }
namespace {
std::filesystem::path directory;
std::string error, nonce, basename, last_command;
std::uint64_t sequence = 0, rows = 0, session_generation = 0;
LARGE_INTEGER frequency{}, anchor{};
double capture_anchor = 0;
HANDLE csv = INVALID_HANDLE_VALUE;
HANDLE context_csv = INVALID_HANDLE_VALUE;
ContextHistory context_history;
std::uint64_t context_rows = 0, last_context_drops = 0;
std::uint64_t context_buffered_rows = 0;
std::string context_error, context_buffer;
constexpr char context_header[] = "row,capture_time_estimate_s,qpc,native_present_frame,thread_id,kind,scope,game_object_id,group_or_parameter_id,value_id,value_float,value_bits,duration_ms,curve,bypass,phase\n";
bool attempted = false, ready = false, failed = false;
ULONGLONG next_poll = 0, next_status = 0;

std::string read_small(const std::filesystem::path& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    char buffer[513]; DWORD size = 0;
    const bool ok = ReadFile(file, buffer, sizeof(buffer), &size, nullptr) != FALSE;
    CloseHandle(file);
    return ok && size <= 512 ? std::string(buffer, size) : std::string();
}
bool write_all(HANDLE file, const std::string& body) {
    DWORD done = 0;
    return WriteFile(file, body.data(), static_cast<DWORD>(body.size()), &done, nullptr) && done == body.size();
}
void write_status() {
    if (directory.empty()) return;
    const std::string body = "schema=mhgz_rise_native_audio_v1\nnonce=" + nonce + "\nseq=" + std::to_string(sequence) +
        "\nready=" + (ready ? "1" : "0") + "\nrecording=" + (csv != INVALID_HANDLE_VALUE ? "1" : "0") +
        "\nobserved=" + std::to_string(queue.observed.load()) + "\nrows=" + std::to_string(rows) +
        "\ndropped=" + std::to_string(queue.dropped.load()) + "\nanchor_qpc=" + std::to_string(anchor.QuadPart) +
        "\nqpc_frequency=" + std::to_string(frequency.QuadPart) + "\ncapture_anchor=" + std::to_string(capture_anchor) +
        "\ncontext_ready=" + (ready ? "1" : "0") + "\ncontext_observed=" + std::to_string(context_queue.observed.load()) +
        "\ncontext_rows=" + std::to_string(context_rows) + "\ncontext_dropped=" + std::to_string(context_queue.dropped.load()) +
        "\ncontext_gaps=" + std::to_string(context_history.gaps) + "\ncontext_error=" + context_error +
        "\nobjects_ready=" + (objects.ready ? "1" : "0") + "\nobjects_observed=" + std::to_string(objects.events.observed.load()) +
        "\nobjects_rows=" + std::to_string(objects.rows) + "\nobjects_dropped=" + std::to_string(objects.events.dropped.load()) +
        "\nobjects_full_dropped=" + std::to_string(objects.events.full_dropped.load()) +
        "\nobjects_reservation_dropped=" + std::to_string(objects.events.reservation_dropped.load()) +
        "\nobjects_queue_mode=bounded_mpsc_sequence_v1" +
        "\nobjects_error=" + objects.error +
        "\ntrigger_ready=" + (objects.trigger_ready ? "1" : "0") + "\ntrigger_observed=" + std::to_string(objects.triggers.load()) +
        "\ntrigger_hit_contexts=" + std::to_string(objects.hit_contexts.load()) + "\ntrigger_error=" + objects.trigger_error +
        "\nerror=" + error + "\nEND\n";
    const auto temp = directory / "audio_native_bridge_status.tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    const bool ok = write_all(file, body);
    CloseHandle(file);
    if (ok) MoveFileExW(temp.c_str(), (directory / "audio_native_bridge_status.txt").c_str(), MOVEFILE_REPLACE_EXISTING);
}
std::string context_line(const NativeAudioEvent& e, std::uint64_t row, double time, const char* phase) {
    if (!e.kind) {
        char line[384];
        std::snprintf(line, sizeof(line), "%llu,%.9f,%llu,,,unknown,unknown,,,,,,,,,%s\n",
            static_cast<unsigned long long>(row), time, static_cast<unsigned long long>(e.qpc), phase);
        return line;
    }
    const bool rtpc = e.kind == static_cast<std::uint32_t>(ContextKind::Rtpc);
    const bool discrete = e.kind == static_cast<std::uint32_t>(ContextKind::State) || e.kind == static_cast<std::uint32_t>(ContextKind::Switch);
    char number[48]{};
    if (rtpc && std::isfinite(context_float(e))) std::snprintf(number, sizeof(number), "%.9g", context_float(e));
    char line[768];
    std::snprintf(line, sizeof(line), "%llu,%.9f,%llu,%llu,%u,%s,%s,%llu,%s,%s,%s,%s,%u,%u,%u,%s\n",
        static_cast<unsigned long long>(row), time, static_cast<unsigned long long>(e.qpc),
        static_cast<unsigned long long>(e.native_frame), e.thread_id, context_name(e.kind),
        context_global(e) ? "global" : "object", static_cast<unsigned long long>(e.game_object_id),
        (discrete || rtpc || e.kind == static_cast<std::uint32_t>(ContextKind::ResetRtpc)) ? std::to_string(e.event_id).c_str() : "",
        discrete ? std::to_string(e.callback_flags).c_str() : "", number,
        rtpc ? std::to_string(e.callback_flags).c_str() : "", e.duration, e.curve, e.bypass, phase);
    return line;
}
void flush_context() {
    if (context_buffer.empty()) return;
    if (context_csv != INVALID_HANDLE_VALUE && write_all(context_csv, context_buffer)) context_rows += context_buffered_rows;
    else {
        context_error = "context_csv_write_failed";
        if (context_csv != INVALID_HANDLE_VALUE) { CloseHandle(context_csv); context_csv = INVALID_HANDLE_VALUE; }
    }
    context_buffer.clear(); context_buffered_rows = 0;
}
void emit_context(const NativeAudioEvent& e, const char* phase, bool baseline = false) {
    if (context_csv == INVALID_HANDLE_VALUE) return;
    const double delta = e.qpc >= static_cast<std::uint64_t>(anchor.QuadPart) ?
        static_cast<double>(e.qpc - anchor.QuadPart) / frequency.QuadPart :
        -static_cast<double>(anchor.QuadPart - e.qpc) / frequency.QuadPart;
    context_buffer += context_line(e, context_rows + ++context_buffered_rows, capture_anchor + (baseline ? 0 : delta), phase);
    if (context_buffer.size() >= 65536) flush_context();
}
void drain_context() {
    std::array<NativeAudioEvent, 256> batch;
    for (unsigned pass = 0; pass != NativeAudioQueue::capacity / batch.size(); ++pass) {
        const auto count = context_queue.take(batch.data(), batch.size());
        if (!count) break;
        for (std::size_t i = 0; i != count; ++i) {
            const auto before = context_history.gaps;
            context_history.apply(batch[i]);
            if (context_history.gaps != before) emit_context(batch[i], "cache_capacity_gap");
            emit_context(batch[i], context_history.qpc_barrier && batch[i].qpc <= context_history.qpc_barrier ?
                "pre_loss_barrier_not_cached" : "update_requested");
        }
    }
    const auto dropped = context_queue.dropped.load();
    if (dropped != last_context_drops) {
        last_context_drops = dropped;
        LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
        context_history.invalidate(now.QuadPart);
        NativeAudioEvent gap{}; gap.qpc = now.QuadPart;
        emit_context(gap, "queue_drop_history_unknown");
    }
    flush_context();
}
void write_context_live() {
    if (directory.empty() || !ready) return;
    std::string body = "schema=mhgz_rise_audio_context_live_v1\npid=" + std::to_string(GetCurrentProcessId()) +
        "\ncoverage=last_observed_numeric_requests_only;prehook_unknown;rtpc_current_value_unknown\n" +
        "emitter_owner=unknown;damage=not_captured;final_media=unknown\nobserved=" + std::to_string(context_queue.observed.load()) +
        "\ndropped=" + std::to_string(context_queue.dropped.load()) + "\ngaps=" + std::to_string(context_history.gaps) +
        "\ncache_size=" + std::to_string(context_history.values.size()) + "\nmax_preview_rows=64\n" + context_header;
    body += "objects_ready=" + std::string(objects.ready ? "1" : "0") +
        "\nobjects_observed=" + std::to_string(objects.events.observed.load()) +
        "\nobjects_dropped=" + std::to_string(objects.events.dropped.load()) +
        "\nobjects_full_dropped=" + std::to_string(objects.events.full_dropped.load()) +
        "\nobjects_reservation_dropped=" + std::to_string(objects.events.reservation_dropped.load()) +
        "\nobjects_queue_mode=bounded_mpsc_sequence_v1" +
        "\nlast_engine_gameobject_address=" + std::to_string(objects.last.object_address) +
        "\nlast_key_game_object_id=" + std::to_string(objects.last.game_object_id) +
        "\nlast_key_sub_id=" + std::to_string(objects.last.event_id) +
        "\ntrigger_ready=" + (objects.trigger_ready ? "1" : "0") + "\ntrigger_observed=" + std::to_string(objects.triggers.load()) +
        "\ntrigger_hit_contexts=" + std::to_string(objects.hit_contexts.load()) + "\ntrigger_error=" + objects.trigger_error +
        "\nobjects_error=" + objects.error + "\n";
    std::uint64_t i = 0;
    for (const auto& item : context_history.values) {
        if (i == 64) break;
        body += context_line(item.second, ++i, 0, "live_last_observed_request");
    }
    const auto temp = directory / "audio_native_context_live.tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    const bool ok = write_all(file, body); CloseHandle(file);
    if (ok) MoveFileExW(temp.c_str(), (directory / "audio_native_context_live.txt").c_str(), MOVEFILE_REPLACE_EXISTING);
}
void drain() {
    if (csv == INVALID_HANDLE_VALUE) return;
    std::array<NativeAudioEvent, 256> batch;
    std::string body;
    // Bound each render callback to the fixed queue capacity, including STOP.
    for (unsigned pass = 0; pass != NativeAudioQueue::capacity / batch.size(); ++pass) {
        const auto count = queue.take(batch.data(), batch.size());
        if (!count) break;
        for (std::size_t i = 0; i != count; ++i) {
            const auto& event = batch[i];
            const double elapsed = static_cast<double>(event.qpc - anchor.QuadPart) / frequency.QuadPart;
            char line[400];
            std::snprintf(line, sizeof(line), "%llu,%.9f,%.9f,%llu,%u,%llu,%u,%llu,%u,post_requested,all_world_unattributed\n",
                static_cast<unsigned long long>(rows + i + 1), capture_anchor + elapsed, elapsed,
                static_cast<unsigned long long>(event.qpc), event.event_id,
                static_cast<unsigned long long>(event.game_object_id), event.callback_flags,
                static_cast<unsigned long long>(event.native_frame), event.thread_id);
            body += line;
        }
        if (!write_all(csv, body)) { error = "csv_write_failed"; failed = true; queue.stop(); break; }
        rows += count;
        body.clear();
    }
}
void stop() {
    queue.stop();
    drain();
    drain_context();
    objects.drain(anchor, frequency, capture_anchor);
    objects.close();
    if (csv != INVALID_HANDLE_VALUE) { FlushFileBuffers(csv); CloseHandle(csv); csv = INVALID_HANDLE_VALUE; }
    if (context_csv != INVALID_HANDLE_VALUE) { FlushFileBuffers(context_csv); CloseHandle(context_csv); context_csv = INVALID_HANDLE_VALUE; }
}
std::string file_sha256(const std::filesystem::path& path) {
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    HANDLE file = INVALID_HANDLE_VALUE;
    std::string output;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    DWORD bytes = 0, object_size = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &bytes, 0) >= 0) {
        std::vector<unsigned char> object(object_size), buffer(65536);
        unsigned char digest[32];
        if (BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0) >= 0) {
            file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            bool ok = file != INVALID_HANDLE_VALUE;
            while (ok) {
                if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes, nullptr)) { ok = false; break; }
                if (!bytes) break;
                ok = BCryptHashData(hash, buffer.data(), bytes, 0) >= 0;
            }
            if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0) {
                constexpr char digits[] = "0123456789abcdef";
                for (auto byte : digest) { output += digits[byte >> 4]; output += digits[byte & 15]; }
            }
            BCryptDestroyHash(hash);
        }
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return output;
}
void install() {
    wchar_t path[32768];
    const auto length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (!length || length == std::size(path)) throw std::runtime_error("image_path_unavailable");
    const std::filesystem::path image(path);
    if (_wcsicmp(image.filename().c_str(), L"MonsterHunterRise.exe")) throw std::runtime_error("wrong_game");
    directory = image.parent_path() / "reframework/data/MHGZ_AerialTrajectoryRecorder";
    std::filesystem::create_directories(directory);
    // Never replay a command left on disk by an earlier game process.
    last_command = read_small(directory / "audio_native_bridge_command.txt");
    if (!QueryPerformanceFrequency(&frequency)) throw std::runtime_error("qpc_unavailable");
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    image_base = reinterpret_cast<std::uintptr_t>(base);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000)
        throw std::runtime_error("image_header_mismatch");
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE || pe->OptionalHeader.SizeOfImage != rise_audio_profile::image_size)
        throw std::runtime_error("image_build_mismatch");
    auto* target = base + rise_audio_profile::post_rva;
    if (!rise_audio_profile::compatible(pe->FileHeader.TimeDateStamp, pe->OptionalHeader.SizeOfImage,
                                       target, base + rise_audio_profile::numeric_entry_rva) ||
        file_sha256(image) != rise_audio_profile::sha256 || !rise_audio_profile::context_compatible(base))
        throw std::runtime_error("audio_profile_mismatch_no_hook");
    // Pin the DLL once accepted: a live trampoline must never point to an unloaded image.
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&MhgzPostEntry), &pinned)) throw std::runtime_error("pin_failed");
    if (MH_Initialize() != MH_OK) throw std::runtime_error("minhook_init_failed");
    struct Hook { void* target; void* entry; void** original; };
    std::array<Hook, 7> hooks{{
        {target, reinterpret_cast<void*>(&MhgzPostEntry), &MhgzOriginalPost},
        {base + rise_audio_profile::context_entries[0].rva, reinterpret_cast<void*>(&MhgzStateEntry), &MhgzOriginalState},
        {base + rise_audio_profile::context_entries[1].rva, reinterpret_cast<void*>(&MhgzSwitchEntry), &MhgzOriginalSwitch},
        {base + rise_audio_profile::context_entries[2].rva, reinterpret_cast<void*>(&MhgzRtpcEntry), &MhgzOriginalRtpc},
        {base + rise_audio_profile::context_entries[3].rva, reinterpret_cast<void*>(&MhgzResetEntry), &MhgzOriginalReset},
        {base + rise_audio_profile::context_entries[4].rva, reinterpret_cast<void*>(&MhgzRegisterEntry), &MhgzOriginalRegister},
        {base + rise_audio_profile::context_entries[5].rva, reinterpret_cast<void*>(&MhgzUnregisterEntry), &MhgzOriginalUnregister},
    }};
    void* object_target = base + rise_audio_profile::object_key_rva;
    bool object_created = false;
    void* trigger_target = base + rise_audio_profile::trigger_rva;
    bool trigger_created = false;
    std::size_t created = 0;
    try {
        for (auto& hook : hooks) {
            if (MH_CreateHook(hook.target, hook.entry, hook.original) != MH_OK) throw std::runtime_error("minhook_create_failed");
            ++created;
            if (MH_QueueEnableHook(hook.target) != MH_OK) throw std::runtime_error("minhook_queue_failed");
        }
        // Optional independently guarded observer: a failure cannot disable the
        // already-verified Post/condition capture. No speculative signature scan.
        if (std::memcmp(object_target, rise_audio_profile::object_key_prefix.data(), rise_audio_profile::object_key_prefix.size()))
            objects.error = "object_key_prefix_mismatch_no_hook";
        else if (MH_CreateHook(object_target, reinterpret_cast<void*>(&object_key), reinterpret_cast<void**>(&objects.original)) != MH_OK)
            objects.error = "object_hook_create_failed";
        else {
            object_created = true;
            if (MH_QueueEnableHook(object_target) != MH_OK) {
                MH_RemoveHook(object_target); object_created = false; objects.error = "object_hook_queue_failed";
            }
        }
        context_queue.start(1); // Keep observing conditions before/after F9, not just after START.
        if (!rise_audio_profile::trigger_compatible(base)) objects.trigger_error = "trigger_callchain_profile_mismatch";
        else if (MH_CreateHook(trigger_target,reinterpret_cast<void*>(&MhgzTriggerEntry),&MhgzOriginalTrigger)!=MH_OK)
            objects.trigger_error = "trigger_create_failed";
        else {
            trigger_created = true;
            MhgzSpatialReturn = base+rise_audio_profile::spatial_return_rva;
            MhgzHitReturnA = base+rise_audio_profile::hit_return_a;
            MhgzHitReturnB = base+rise_audio_profile::hit_return_b;
            if (MH_QueueEnableHook(trigger_target)!=MH_OK) {
                MH_RemoveHook(trigger_target); trigger_created=false; objects.trigger_error="trigger_queue_failed";
            }
        }
        objects.events.start(1);
        if (MH_ApplyQueued() != MH_OK) throw std::runtime_error("minhook_enable_failed");
    } catch (...) {
        context_queue.stop();
        objects.events.stop();
        if (object_created) { MH_DisableHook(object_target); MH_RemoveHook(object_target); }
        if (trigger_created) { MH_DisableHook(trigger_target); MH_RemoveHook(trigger_target); }
        for (std::size_t i = 0; i != created; ++i) { MH_DisableHook(hooks[i].target); MH_RemoveHook(hooks[i].target); }
        MH_Uninitialize(); throw;
    }
    ready = true;
    objects.ready = object_created;
    objects.trigger_ready = trigger_created;
}
void command(const AudioCommand& value) {
    if (value.nonce == nonce && value.sequence <= sequence) return;
    // A new Lua epoch must PING first; do not accept stale START/STOP from another epoch.
    if (value.nonce != nonce) {
        if (value.verb != "PING") return;
        stop(); nonce = value.nonce; sequence = 0;
    }
    sequence = value.sequence;
    if (value.verb == "START") {
        stop(); rows = context_rows = 0; basename = value.basename;
        if (!ready || failed) { write_status(); return; }
        csv = CreateFileW((directory / (basename + "_audio_native.csv")).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                           nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (csv == INVALID_HANDLE_VALUE) { error = "csv_create_failed_no_overwrite"; write_status(); return; }
        if (!write_all(csv, "row,capture_time_estimate_s,native_elapsed_s,qpc,event_id,game_object_id,callback_flags,native_present_frame,thread_id,kind,source_scope\n")) {
            error = "csv_header_failed"; failed = true; stop(); write_status(); return;
        }
        error.clear(); capture_anchor = value.capture_time; QueryPerformanceCounter(&anchor);
        context_error.clear();
        context_csv = CreateFileW((directory / (basename + "_audio_context.csv")).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                  nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (context_csv == INVALID_HANDLE_VALUE) context_error = "context_csv_create_failed_no_overwrite";
        else if (!write_all(context_csv, context_header)) {
            context_error = "context_csv_header_failed"; CloseHandle(context_csv); context_csv = INVALID_HANDLE_VALUE;
        }
        NativeAudioEvent unknown{}; unknown.qpc = anchor.QuadPart;
        emit_context(unknown, "initial_coverage_unknown", true);
        for (const auto& item : context_history.values) emit_context(item.second, "baseline_last_observed_request", true);
        flush_context();
        objects.start(directory / (basename + "_audio_objects.csv"));
        queue.start(++session_generation);
    } else if (value.verb == "STOP" && value.basename == basename) stop();
    write_status();
}
void present() noexcept {
    if (!attempted) {
        attempted = true;
        try { install(); } catch (const std::exception& exception) { error = exception.what(); failed = true; }
        write_status();
    }
    try {
        queue.frame.fetch_add(1, std::memory_order_relaxed);
        context_queue.frame.store(queue.frame.load(std::memory_order_relaxed), std::memory_order_relaxed);
        objects.events.frame.store(queue.frame.load(std::memory_order_relaxed), std::memory_order_relaxed);
        objects.drain(anchor, frequency, capture_anchor);
        drain_context();
        const auto now = GetTickCount64();
        if (now >= next_poll && !directory.empty()) {
            next_poll = now + 15;
            const auto body = read_small(directory / "audio_native_bridge_command.txt");
            AudioCommand value;
            if (body != last_command && parse_audio_command(body, value)) { last_command = body; command(value); }
        }
        drain();
        if (now >= next_status) { next_status = now + 1000; write_status(); write_context_live(); }
    } catch (...) {
        queue.stop(); failed = true; error = "present_io_exception";
        if (csv != INVALID_HANDLE_VALUE) { CloseHandle(csv); csv = INVALID_HANDLE_VALUE; }
        if (context_csv != INVALID_HANDLE_VALUE) { CloseHandle(context_csv); context_csv = INVALID_HANDLE_VALUE; }
        objects.close();
    }
}
}
extern "C" __declspec(dllexport) void reframework_plugin_required_version(RefVersion* value) {
    // Only write the original 3-int prefix; older hosts may allocate a smaller struct.
    value->major = 1; value->minor = 0; value->patch = 0;
}
extern "C" __declspec(dllexport) bool reframework_plugin_initialize(const RefInitializePrefix* value) {
    return value && value->version && value->version->major == 1 && value->functions &&
           value->functions->on_present && value->functions->on_present(present);
}
