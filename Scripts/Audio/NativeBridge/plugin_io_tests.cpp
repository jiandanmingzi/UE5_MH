// Same production IPC/session code, run only in this process against a unique
// build-directory fixture. install() is deliberately never called.
#include "plugin.cpp"
#include <fstream>
#include <iostream>
static void check(bool value, const char* label) { if (!value) throw std::runtime_error(label); }
static std::string all(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
static void send(const std::string& body) {
    std::ofstream file(directory / "audio_native_bridge_command.txt", std::ios::binary);
    file << body; file.close(); next_poll = 0; present();
}
int main() try {
    directory = std::filesystem::current_path() / ("NativeIO-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    check(std::filesystem::create_directory(directory), "unique fixture");
    attempted = ready = true; QueryPerformanceFrequency(&frequency);
    context_queue.start(1);
    objects.ready = true; objects.events.start(1);
    record_state(context_queue,33,44);
    record_switch(context_queue,77,88,0xFFFFFFFF12345678ULL);
    send("MHGZ_AUDIO_NATIVE_V1\nnew_epoch\n1\nSTART\nfixture\n0\nEND\n");
    check(csv == INVALID_HANDLE_VALUE && sequence == 0, "new epoch cannot START without PING");
    send("MHGZ_AUDIO_NATIVE_V1\nnew_epoch\n1\nPING\nnone\n0\nEND\n");
    // read_small is deliberately the <=512-byte COMMAND reader. Status has
    // extra diagnostic keys and must not be truncated by that command limit.
    const auto ping_status = all(directory / "audio_native_bridge_status.txt");
    check(nonce == "new_epoch" && sequence == 1 && ping_status.find("ready=1\n") != std::string::npos,
          "PING live acknowledgement");
    check(ping_status.find("objects_queue_mode=bounded_mpsc_sequence_v1\n")!=std::string::npos &&
        ping_status.find("objects_full_dropped=0\n")!=std::string::npos &&
        ping_status.find("objects_reservation_dropped=0\n")!=std::string::npos,"queue loss diagnostics in status");
    const std::string start = "MHGZ_AUDIO_NATIVE_V1\nnew_epoch\n2\nSTART\nfixture\n1.25\nEND\n";
    send(start);
    check(csv != INVALID_HANDLE_VALUE, "START opens CSV");
    check(context_csv != INVALID_HANDLE_VALUE && context_rows==3,"context CSV has unknown marker and pre-F9 observed baseline");
    queue.push(18325887, 0xFFFFFFFF12345678ULL, 7);
    objects.events.push(0x12345678,0xFFFFFFFF12345678ULL,0,1,0,0,0,0xFEDCBA9876543210ULL);
    objects.lifecycle(0xFFFFFFFF12345678ULL, false);
    objects.trigger(0x1111222233334444ULL,0xFEDCBA9876543210ULL,123,0xFFFFFFFF12345678ULL,0x5566778899aabbccULL,99,88);
    record_rtpc(context_queue,99,-87.25f,UINT64_MAX,123,7,true);
    send(start);
    check(rows == 1, "duplicate command does not restart or erase CSV");
    send("MHGZ_AUDIO_NATIVE_V1\nnew_epoch\n3\nSTOP\nfixture\n2\nEND");
    check(csv != INVALID_HANDLE_VALUE, "partial command ignored");
    send("MHGZ_AUDIO_NATIVE_V1\nnew_epoch\n3\nPING\nnone\n2\nEND\n");
    check(csv != INVALID_HANDLE_VALUE, "same-epoch heartbeat preserves recording");
    send("MHGZ_AUDIO_NATIVE_V1\nold_epoch\n99\nSTOP\nfixture\n2\nEND\n");
    check(csv != INVALID_HANDLE_VALUE, "foreign epoch STOP ignored");
    send("MHGZ_AUDIO_NATIVE_V1\nnew_epoch\n4\nSTOP\nfixture\n2\nEND\n");
    check(csv == INVALID_HANDLE_VALUE, "STOP closes CSV");
    const auto first = all(directory / "fixture_audio_native.csv");
    const auto object_first = all(directory / "fixture_audio_objects.csv");
    check(object_first.find("initial_coverage_unknown;no_pref9_baseline")!=std::string::npos &&
        object_first.find("object_key_return,18446744069720004216,18364758544493064720,305419896,")!=std::string::npos &&
        object_first.find("unregister_requested,18446744069720004216,,,")!=std::string::npos,
        "object CSV keeps full pointer and ID namespaces plus raw lifecycle observations");
    check(object_first.find("container_trigger_context,,18446744069720004216,123,")!=std::string::npos &&
        object_first.find("native_call_arguments;trigger_id_NOT_event_id;pool_identity_not_unique_transaction,1229801703532086340,18364758544493064720,6153737369425722316,99,88")!=std::string::npos,
        "trigger relationship preserves pointer namespaces and does not invent an event ID or unique transaction");
    std::istringstream object_stream(object_first); std::string object_row;
    while (std::getline(object_stream,object_row)) check(std::count(object_row.begin(),object_row.end(),',')==14,"object CSV consistent fifteen columns");
    const auto context_first = all(directory / "fixture_audio_context.csv");
    check(context_first.find("unknown,unknown,,,,,,,,,initial_coverage_unknown\n")!=std::string::npos,
        "missing initial state is blank/unknown, not fabricated zero values");
    check(context_first.find("77,88,,0,0,0,baseline_last_observed_request")!=std::string::npos ||
          context_first.find("77,88,,,0,0,0,baseline_last_observed_request")!=std::string::npos,"baseline observed Switch retained");
    check(context_first.find("18446744069720004216")!=std::string::npos &&
          context_first.find("rtpc_requested,global,18446744073709551615,99,,-87.25,")!=std::string::npos &&
          context_first.find(",123,7,1,update_requested")!=std::string::npos,"context CSV retains full UInt64, exact RTPC target and interpolation parameters");
    std::istringstream context_stream(context_first); std::string context_row;
    while (std::getline(context_stream,context_row))
        check(std::count(context_row.begin(),context_row.end(),',')==15,"context CSV consistent 16 columns including unknown markers");
    check(first.find(",18325887,18446744069720004216,7,") != std::string::npos &&
          first.find("post_requested,all_world_unattributed\n") != std::string::npos,
          "actual CSV retains UInt32/UInt64 identity and request-only semantics");
    send("MHGZ_AUDIO_NATIVE_V1\nnew_epoch\n5\nSTART\nfixture\n0\nEND\n");
    check(csv == INVALID_HANDLE_VALUE && all(directory / "fixture_audio_native.csv") == first,
          "existing recording never overwritten");
    send("MHGZ_AUDIO_NATIVE_V1\nnew_epoch\n6\nSTART\nfixture2\n0\nEND\n");
    check(csv != INVALID_HANDLE_VALUE, "new session recovers after filename collision");
    queue.push(25934417, 44, 0);
    send("MHGZ_AUDIO_NATIVE_V1\nreset_epoch\n1\nPING\nnone\n0\nEND\n");
    check(csv == INVALID_HANDLE_VALUE && all(directory / "fixture2_audio_native.csv").find("25934417,44,0,") != std::string::npos,
          "script reset handshake drains and closes previous session");
    {
        std::ofstream existing(directory / "fixture3_audio_context.csv",std::ios::binary); existing<<"preserve me\n";
    }
    send("MHGZ_AUDIO_NATIVE_V1\nreset_epoch\n2\nSTART\nfixture3\n0\nEND\n");
    check(csv!=INVALID_HANDLE_VALUE && context_csv==INVALID_HANDLE_VALUE &&
        context_error=="context_csv_create_failed_no_overwrite" && all(directory/"fixture3_audio_context.csv")=="preserve me\n",
        "context sidecar failure never overwrites it or blocks original audio capture");
    queue.push(12345,42,0);
    send("MHGZ_AUDIO_NATIVE_V1\nreset_epoch\n3\nSTOP\nfixture3\n1\nEND\n");
    check(all(directory/"fixture3_audio_native.csv").find("12345,42,0,")!=std::string::npos,"original CSV survives context-only failure");
    {
        std::ofstream existing(directory / "fixture4_audio_objects.csv",std::ios::binary); existing<<"preserve objects\n";
    }
    send("MHGZ_AUDIO_NATIVE_V1\nreset_epoch\n4\nSTART\nfixture4\n0\nEND\n");
    check(csv!=INVALID_HANDLE_VALUE && !objects.recording() && objects.error=="object_csv_create_failed_no_overwrite" &&
        all(directory/"fixture4_audio_objects.csv")=="preserve objects\n","object-only collision preserves data and original capture");
    send("MHGZ_AUDIO_NATIVE_V1\nreset_epoch\n5\nSTOP\nfixture4\n0\nEND\n");
    send("MHGZ_AUDIO_NATIVE_V1\nreset_epoch\n6\nSTART\nfixture5\n0\nEND\n");
    for (std::size_t i=0;i<NativeAudioQueue::capacity+2;++i) objects.events.push(0,i,0,1,0,0,0,55);
    send("MHGZ_AUDIO_NATIVE_V1\nreset_epoch\n7\nSTOP\nfixture5\n0\nEND\n");
    check(all(directory/"fixture5_audio_objects.csv").find("queue_gap;no_safe_complete_join")!=std::string::npos,"object losses explicitly prevent complete ownership claims");
    check(objects.events.full_dropped>0 && objects.events.reservation_dropped==0,
        "object full/reservation diagnostics distinguish loss causes");
    for (std::size_t i=0;i<NativeAudioQueue::capacity+2;++i) record_state(context_queue,33,static_cast<std::uint32_t>(i));
    present();
    check(context_queue.dropped==2 && context_history.values.empty() && context_history.gaps>0,
        "lost condition updates invalidate rather than silently reuse cache");
    context_queue.stop();
    objects.events.stop();
    for (const auto& file : std::filesystem::directory_iterator(directory)) std::filesystem::remove(file.path());
    std::filesystem::remove(directory);
    std::cout << "PASS: production IPC PING/START/STOP, partial/replay/foreign commands, reset drain, CSV identity and no-overwrite\n";
    return 0;
} catch (const std::exception& exception) {
    stop(); std::cerr << "FAIL: " << exception.what() << " fixture=" << directory << '\n'; return 1;
}
