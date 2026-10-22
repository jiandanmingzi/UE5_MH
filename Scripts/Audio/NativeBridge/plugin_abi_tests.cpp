#include "reframework_abi.hpp"
#include <Windows.h>
#include <iostream>
#include <stdexcept>
static RefPresent saved = nullptr;
static bool register_present(RefPresent value) { saved = value; return value != nullptr; }
static void check(bool value, const char* label) { if (!value) throw std::runtime_error(label); }
int wmain(int argc, wchar_t** argv) try {
    check(argc == 2, "DLL path required");
    const auto dll = LoadLibraryW(argv[1]);
    check(dll != nullptr, "load actual built DLL in test process");
    const auto version = reinterpret_cast<void (*)(RefVersion*)>(GetProcAddress(dll, "reframework_plugin_required_version"));
    const auto initialize = reinterpret_cast<bool (*)(const RefInitializePrefix*)>(GetProcAddress(dll, "reframework_plugin_initialize"));
    check(version && initialize, "two plugin exports");
    struct OldVersion { int major = -1, minor = -1, patch = -1; unsigned int sentinel = 0xAABBCCDD; } old;
    version(reinterpret_cast<RefVersion*>(&old));
    check(old.major == 1 && old.minor == 0 && old.patch == 0 && old.sentinel == 0xAABBCCDD,
          "required_version writes only original three-int prefix");
    check(!initialize(nullptr), "null host rejected");
    RefFunctionsPrefix functions{nullptr, nullptr, register_present};
    RefVersion host_version{2, 0, 0, nullptr};
    RefInitializePrefix host{nullptr, &host_version, &functions};
    check(!initialize(&host) && !saved, "incompatible API major rejected");
    host_version.major = 1;
    check(initialize(&host) && saved, "stable API prefix registers callback");
    // Do NOT execute the callback: this test must never inspect a game or install a hook.
    FreeLibrary(dll);
    std::cout << "PASS: actual DLL load, exports, old version size, major guard and stable callback prefix\n";
    return 0;
} catch (const std::exception& exception) { std::cerr << "FAIL: " << exception.what() << '\n'; return 1; }
