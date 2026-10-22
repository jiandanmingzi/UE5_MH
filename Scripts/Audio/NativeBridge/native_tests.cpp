#include "capture_core.hpp"
#include "condition_core.hpp"
#include "object_capture.hpp"
#include "reframework_abi.hpp"
#include "game_profile.hpp"
#include "protocol.hpp"
#include <MinHook.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

extern "C" void MhgzPostEntry();
extern "C" void MhgzStateEntry();
extern "C" void MhgzSwitchEntry();
extern "C" void MhgzRtpcEntry();
extern "C" void MhgzResetEntry();
extern "C" void MhgzRegisterEntry();
extern "C" void MhgzUnregisterEntry();
extern "C" void MhgzTriggerEntry();
extern "C" std::uint64_t MhgzTriggerFixture(std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t);
extern "C" void MhgzTestSpatialReturn();
extern "C" void MhgzTestHitReturn();
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
static AudioObjectCapture object_fixture;
extern "C" void MhgzObserveTrigger(std::uint64_t a,std::uint64_t b,std::uint32_t c,std::uint64_t d,
                                    std::uint64_t e,std::uint64_t f,std::uint64_t g) noexcept { object_fixture.trigger(a,b,c,d,e,f,g); }
__declspec(noinline) static std::uint64_t trigger_original(std::uint64_t a,std::uint64_t b,std::uint64_t c,std::uint64_t d) {return a^b^c^d;}
__declspec(noinline) static std::uint64_t key_fixture(void* ptr, std::uint32_t slot) {
    return reinterpret_cast<std::uintptr_t>(ptr) ^ slot ^ 0xFE1234567890ABCDULL;
}
static std::uint64_t key_observer(void* ptr, std::uint32_t slot) noexcept { return object_fixture.key(ptr, slot); }
extern "C" void MhgzObserveEvent(std::uint32_t id, std::uint64_t obj, std::uint32_t flags) noexcept {
    queue.push(id, obj, flags);
}
extern "C" void MhgzObserveState(std::uint32_t a, std::uint32_t b) noexcept { record_state(context_queue,a,b); }
extern "C" void MhgzObserveSwitch(std::uint32_t a, std::uint32_t b, std::uint64_t c) noexcept { record_switch(context_queue,a,b,c); }
extern "C" void MhgzObserveRtpc(std::uint32_t a, float b, std::uint64_t c, std::uint32_t d, std::uint32_t e, bool f) noexcept { record_rtpc(context_queue,a,b,c,d,e,f); }
extern "C" void MhgzObserveReset(std::uint32_t a, std::uint64_t b) noexcept { context_queue.push(a,b,0,4); }
extern "C" void MhgzObserveRegister(std::uint64_t a) noexcept { context_queue.push(0,a,0,5); }
extern "C" void MhgzObserveUnregister(std::uint64_t a) noexcept { context_queue.push(0,a,0,6); }
static void check(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(what);
}

// Deliberately uses upper pointer bits, a float register, a float stack value
// and six additional stack arguments. The observer must not truncate/shift any.
using Synthetic = std::uint64_t (*)(std::uint64_t, std::uint64_t, std::uint32_t, double,
                                   float, std::uint64_t, std::uint64_t, std::uint64_t,
                                   std::uint64_t, std::uint64_t, std::uint64_t);
__declspec(noinline) static std::uint64_t synthetic(std::uint64_t id, std::uint64_t obj,
    std::uint32_t flags, double floating, float stack_float, std::uint64_t a,
    std::uint64_t b, std::uint64_t c, std::uint64_t d, std::uint64_t e, std::uint64_t f) {
    std::uint64_t bits{};
    std::uint32_t fbits{};
    std::memcpy(&bits, &floating, sizeof(bits));
    std::memcpy(&fbits, &stack_float, sizeof(fbits));
    return id ^ obj ^ flags ^ bits ^ fbits ^ a ^ b ^ c ^ d ^ e ^ f;
}
static std::uint64_t invoke(Synthetic fn) {
    return fn(0xABCDEF0012345678ULL, 0xFEDCBA9876543210ULL, 0xCDEF0123,
              -12345.678, 87.25f, 0x11, 0x22, 0x4444, 0x888888, 0x123456789, 0xFEDCBA987);
}
using RtpcSynthetic = std::uint64_t (*)(std::uint32_t,float,std::uint64_t,std::uint32_t,std::uint32_t,bool,std::uint64_t);
__declspec(noinline) static std::uint64_t rtpc_synthetic(std::uint32_t a, float b, std::uint64_t c,
    std::uint32_t d, std::uint32_t e, bool f, std::uint64_t g) {
    std::uint32_t bits; std::memcpy(&bits,&b,sizeof(bits)); return a ^ bits ^ c ^ d ^ e ^ static_cast<std::uint64_t>(f) ^ g;
}
int main() try {
    AudioCommand command;
    const std::string valid = "MHGZ_AUDIO_NATIVE_V1\nepoch_001\n2\nSTART\nmhrise_20261018_01\n1.250000000\nEND\n";
    check(parse_audio_command(valid, command) && command.sequence == 2 && command.capture_time == 1.25,
          "complete protocol command");
    for (const auto& bad : std::vector<std::string>{valid.substr(0, valid.size() - 5), valid + "extra\n", std::string(513, 'x'),
             "MHGZ_AUDIO_NATIVE_V1\nepoch\n1\nSTART\n../escape\n0\nEND\n",
             "MHGZ_AUDIO_NATIVE_V1\nepoch\n1\nEXEC\nnone\n0\nEND\n",
             "MHGZ_AUDIO_NATIVE_V1\nepoch\n1\nSTART\nnone\nnan\nEND\n",
             "MHGZ_AUDIO_NATIVE_V1\nepoch\n1\nSTART\nnone\n-1\nEND\n",
             "MHGZ_AUDIO_NATIVE_V1\nepoch\n18446744073709551616\nSTART\nnone\n0\nEND\n"})
        check(!parse_audio_command(bad, command), "reject partial/oversize/path/injection/numeric commands");
    using namespace rise_audio_profile;
    auto altered = post_prefix;
    check(compatible(timestamp, image_size, post_prefix.data(), entry_prefix.data()), "exact supported profile");
    altered[0] = 0xE9;
    check(!compatible(timestamp, image_size, altered.data(), entry_prefix.data()) &&
          !compatible(timestamp + 1, image_size, post_prefix.data(), entry_prefix.data()) &&
          !compatible(timestamp, image_size - 1, post_prefix.data(), entry_prefix.data()), "fail closed on existing hook/build drift");
    std::vector<NativeAudioEvent> events(NativeAudioQueue::capacity);
    queue.push(1, 2, 3);
    check(queue.observed == 1 && queue.take(events.data(), events.size()) == 0, "idle records no events");
    queue.start(1);
    queue.push(123, 0xFFFFFFFF12345678ULL, 19);
    check(queue.take(events.data(), 1) == 1, "one event");
    check(events[0].event_id == 123 && events[0].game_object_id == 0xFFFFFFFF12345678ULL &&
          events[0].callback_flags == 19 && events[0].qpc > 0, "scalar identity and timestamp");
    queue.stop();
    queue.push(2, 3, 4);
    check(queue.take(events.data(), 1) == 0, "stop disables enqueuing");
    queue.start(2);
    for (std::size_t i = 0; i != NativeAudioQueue::capacity + 2; ++i) queue.push(static_cast<std::uint32_t>(i), i, 7);
    check(queue.dropped == 2, "bounded queue counts overflow");
    check(queue.take(events.data(), events.size()) == NativeAudioQueue::capacity, "capacity exact");
    for (std::size_t i = 0; i != NativeAudioQueue::capacity; ++i)
        check(events[i].event_id == i && events[i].game_object_id == i, "queue order");
    queue.start(3);
    check(queue.dropped == 0 && queue.take(events.data(), 1) == 0, "session reset");
    std::vector<std::thread> producers;
    for (std::uint32_t t = 0; t != 4; ++t) producers.emplace_back([t]() {
        for (std::uint32_t i = 0; i != 5000; ++i) queue.push(t * 5000 + i, t * 5000 + i, 7);
    });
    for (auto& producer : producers) producer.join();
    const auto accepted = queue.take(events.data(), events.size());
    check(accepted + queue.dropped == 20000, "MPSC accepted plus drops equals attempts");
    for (std::size_t i = 0; i != accepted; ++i)
        check(events[i].event_id == events[i].game_object_id && events[i].callback_flags == 7, "MPSC no partial records");
    // Keep total in-flight work below capacity: ANY loss in this producer /
    // consumer overlap is a bug, not an intentionally full queue.
    auto object_queue = std::make_unique<NativeObjectQueue>();
    object_queue->start(1);
    std::atomic<bool> object_go{false};
    std::atomic<unsigned> object_done{0};
    std::vector<std::thread> object_producers;
    for (unsigned t = 0; t < 4; ++t) object_producers.emplace_back([&,t]() {
        while (!object_go.load(std::memory_order_acquire)) std::this_thread::yield();
        for (unsigned i = 0; i < 1000; ++i) object_queue->push(t*1000+i,0xFEDCBA9800000000ULL+t*1000+i,7,4);
        object_done.fetch_add(1,std::memory_order_release);
    });
    std::vector<bool> seen_objects(4000,false); std::size_t object_count = 0;
    bool object_identity_ok = true;
    object_go.store(true,std::memory_order_release);
    do {
        NativeAudioEvent one{};
        if (object_queue->take(&one,1)) {
            if (one.event_id>=4000 || seen_objects[one.event_id] || one.kind!=4 || !one.thread_id || !one.qpc ||
                one.game_object_id!=0xFEDCBA9800000000ULL+one.event_id) object_identity_ok = false;
            else seen_objects[one.event_id]=true;
            ++object_count;
        } else std::this_thread::yield();
    } while (object_done.load(std::memory_order_acquire)!=4 || object_count+object_queue->dropped.load()<4000);
    for (auto& producer : object_producers) producer.join();
    check(object_identity_ok,"object queue preserves scalar identity without duplicates");
    check(object_count==4000 && object_queue->dropped==0 && object_queue->reservation_dropped==0,
        "concurrent object producers/consumer lose no records below capacity");
    for (unsigned pass=0;pass<12;++pass) {
        for (unsigned i=0;i<2000;++i) object_queue->push(i,i,0);
        check(object_queue->take(events.data(),events.size())==2000,"object sequence cells recycle over multiple wraps");
        for (unsigned i=0;i<2000;++i) check(events[i].event_id==i,"object wrap FIFO order");
    }
    for (std::size_t i=0;i<NativeObjectQueue::capacity+2;++i) object_queue->push(1,2,3);
    check(object_queue->dropped==2 && object_queue->full_dropped==2 && object_queue->reservation_dropped==0,
        "object full loss is distinguished from reservation exhaustion");
    check(object_queue->take(events.data(),events.size())==NativeObjectQueue::capacity,"object exact bounded capacity");
    object_queue->stop(); object_queue->push(7,8,9);
    check(object_queue->take(events.data(),1)==0,"object stop does not enqueue new observations");
    object_queue->start(2);
    check(object_queue->dropped==0 && object_queue->full_dropped==0,"object reset only with quiescent producers");
    queue.start(4);
    MhgzOriginalPost = reinterpret_cast<void*>(synthetic);
    const auto expected = invoke(synthetic);
    check(invoke(reinterpret_cast<Synthetic>(MhgzPostEntry)) == expected, "assembly passthrough result and all arguments");
    check(queue.take(events.data(), 1) == 1 && events[0].event_id == 0x12345678 &&
          events[0].game_object_id == 0xFEDCBA9876543210ULL && events[0].callback_flags == 0xCDEF0123,
          "assembly observer reads correct first three scalars");
    check(MH_Initialize() == MH_OK, "MinHook init");
    check(MH_CreateHook(reinterpret_cast<void*>(synthetic), reinterpret_cast<void*>(MhgzPostEntry), &MhgzOriginalPost) == MH_OK,
          "MinHook create synthetic target");
    check(MH_EnableHook(reinterpret_cast<void*>(synthetic)) == MH_OK, "MinHook enable synthetic target");
    Synthetic volatile dynamic_target = synthetic;
    check(invoke(dynamic_target) == expected, "real detour preserves GPR, SSE and stack parameters");
    check(queue.take(events.data(), 1) == 1, "real detour emits exactly one entry");
    check(MH_DisableHook(reinterpret_cast<void*>(synthetic)) == MH_OK, "disable only own hook");
    check(MH_RemoveHook(reinterpret_cast<void*>(synthetic)) == MH_OK, "remove only own hook");
    check(MH_Uninitialize() == MH_OK, "MinHook shutdown");
    check(invoke(dynamic_target) == expected && queue.take(events.data(), 1) == 0, "unhook restores behavior");
    context_queue.start(1);
    const std::pair<void(*)(),void**> entries[] = {
        {MhgzStateEntry,&MhgzOriginalState},{MhgzSwitchEntry,&MhgzOriginalSwitch},
        {MhgzResetEntry,&MhgzOriginalReset},{MhgzRegisterEntry,&MhgzOriginalRegister},
        {MhgzUnregisterEntry,&MhgzOriginalUnregister}};
    for (const auto& entry : entries) {
        *entry.second = reinterpret_cast<void*>(synthetic);
        check(invoke(reinterpret_cast<Synthetic>(entry.first)) == expected, "condition assembly preserves all original parameters");
        check(context_queue.take(events.data(),1)==1, "each observer emits one request");
    }
    MhgzOriginalRtpc = reinterpret_cast<void*>(rtpc_synthetic);
    const auto call_rtpc=[](RtpcSynthetic fn) { return fn(0xfedc1234U,-87.25f,0xfedcba9876543210ULL,123,7,true,0x1122334455667788ULL); };
    check(call_rtpc(reinterpret_cast<RtpcSynthetic>(MhgzRtpcEntry))==call_rtpc(rtpc_synthetic), "RTPC register and stack passthrough");
    check(context_queue.take(events.data(),1)==1 && events[0].kind==3 && context_float(events[0])==-87.25f &&
        events[0].duration==123 && events[0].curve==7 && events[0].bypass==1 &&
        events[0].game_object_id==0xfedcba9876543210ULL, "RTPC copies float, UInt64 and fifth/sixth stack parameters exactly");
    check(MH_Initialize()==MH_OK, "condition MinHook initialize");
    for (const auto& entry : entries) {
        check(MH_CreateHook(reinterpret_cast<void*>(synthetic),reinterpret_cast<void*>(entry.first),entry.second)==MH_OK &&
            MH_EnableHook(reinterpret_cast<void*>(synthetic))==MH_OK,"install condition synthetic detour");
        check(invoke(dynamic_target)==expected,"real condition detour preserves arguments and result");
        check(MH_DisableHook(reinterpret_cast<void*>(synthetic))==MH_OK &&
            MH_RemoveHook(reinterpret_cast<void*>(synthetic))==MH_OK,"remove only synthetic condition detour");
    }
    check(MH_CreateHook(reinterpret_cast<void*>(rtpc_synthetic),reinterpret_cast<void*>(MhgzRtpcEntry),&MhgzOriginalRtpc)==MH_OK &&
        MH_EnableHook(reinterpret_cast<void*>(rtpc_synthetic))==MH_OK,"install RTPC synthetic detour");
    RtpcSynthetic volatile dynamic_rtpc=rtpc_synthetic;
    check(call_rtpc(dynamic_rtpc)==call_rtpc(reinterpret_cast<RtpcSynthetic>(MhgzOriginalRtpc)),"real RTPC detour preserves float and stack arguments");
    check(MH_DisableHook(reinterpret_cast<void*>(rtpc_synthetic))==MH_OK &&
        MH_RemoveHook(reinterpret_cast<void*>(rtpc_synthetic))==MH_OK && MH_Uninitialize()==MH_OK,"remove RTPC synthetic detour");
    ContextHistory history;
    history.apply(events[0]); check(history.values.size()==1, "remember observed request");
    auto e=events[0]; e.kind=4; history.apply(e); check(history.values.empty(), "reset makes old RTPC unknown");
    e.kind=2; e.event_id=0; e.callback_flags=0; history.apply(e); check(history.values.size()==1, "zero IDs/values are valid observed scalars");
    e.kind=6; history.apply(e); check(history.values.empty(), "unregister invalidates reused object");
    e.kind=1; e.game_object_id=UINT64_MAX; history.apply(e);
    e.kind=5; e.game_object_id=99; history.apply(e); check(history.values.size()==1, "object registration does not erase global state");
    history.invalidate(); check(history.values.empty() && history.gaps==1, "loss does not silently retain old history");
    for (std::uint32_t i=0;i<=ContextHistory::capacity;++i) { e.kind=2;e.game_object_id=1;e.event_id=i;history.apply(e); }
    check(history.values.size()==1 && history.gaps==2, "history memory bounded with explicit capacity gap");
    const auto barrier=e.qpc+100;
    history.invalidate(barrier); history.apply(e);
    check(history.values.empty(),"late pre-drop queued record cannot restore stale history");
    e.qpc=barrier+1; history.apply(e); check(history.values.size()==1,"post-gap request can establish a new known target");
    object_fixture.events.start(1);
    object_fixture.original = key_fixture;
    auto* const fake_object = reinterpret_cast<void*>(0xFEDCBA9876543210ULL);
    const auto key_expected = key_fixture(fake_object, 0xABCDEF01);
    check(key_observer(fake_object,0xABCDEF01)==key_expected,"key observer preserves full UInt64 return and original arguments");
    check(object_fixture.events.take(events.data(),1)==1 && events[0].object_address==0xFEDCBA9876543210ULL &&
        events[0].game_object_id==key_expected && events[0].event_id==0xABCDEF01,"key observer copies scalar pointer without dereferencing");
    check(MH_Initialize()==MH_OK && MH_CreateHook(reinterpret_cast<void*>(key_fixture),reinterpret_cast<void*>(key_observer),
        reinterpret_cast<void**>(&object_fixture.original))==MH_OK && MH_EnableHook(reinterpret_cast<void*>(key_fixture))==MH_OK,
        "real two-argument key detour installs in test process");
    AudioObjectCapture::KeyFunction volatile dynamic_key=key_fixture;
    check(dynamic_key(fake_object,0xABCDEF01)==key_expected && object_fixture.events.take(events.data(),1)==1,
        "real key detour returns exactly original ID and emits one binding");
    check(MH_DisableHook(reinterpret_cast<void*>(key_fixture))==MH_OK && MH_RemoveHook(reinterpret_cast<void*>(key_fixture))==MH_OK &&
        MH_Uninitialize()==MH_OK,"remove only own key fixture hook");
    MhgzOriginalTrigger = reinterpret_cast<void*>(trigger_original);
    MhgzSpatialReturn = reinterpret_cast<void*>(MhgzTestSpatialReturn);
    MhgzHitReturnA = reinterpret_cast<void*>(MhgzTestHitReturn);
    check(MhgzTriggerFixture(11,22,33,44)==trigger_original(11,22,33,44),"trigger frame fixture preserves original result");
    check(object_fixture.events.take(events.data(),1)==1 && events[0].kind==4 && events[0].container_address==11 &&
        events[0].owner_address==22 && events[0].event_id==33 && events[0].object_address==44 &&
        events[0].hit_info_address==0xFEDCBA9812345678ULL && events[0].thread_context_address==0x123456789ABCDEF0ULL &&
        events[0].caller_rva==reinterpret_cast<std::uintptr_t>(MhgzTestHitReturn),"verified nested frame extracts exact HitInfo and thread context");
    MhgzHitReturnA = nullptr;
    check(MhgzTriggerFixture(11,22,33,44)==trigger_original(11,22,33,44) &&
        object_fixture.events.take(events.data(),1)==1 && !events[0].hit_info_address && !events[0].thread_context_address,
        "unknown parent caller never fabricates a hit context");
    MhgzOriginalTrigger = reinterpret_cast<void*>(synthetic);
    check(invoke(reinterpret_cast<Synthetic>(MhgzTriggerEntry))==expected,"trigger entry preserves unknown stack arguments, SSE, and return exactly");
    std::cout << "PASS: idle/start/stop/reset, bounded MPSC/overflow, x64 passthrough and real synthetic detour\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
