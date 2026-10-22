// Minimal STABLE PREFIX of the public REFramework plugin C ABI (API major 1).
// Reference: praydog/REFramework include/reframework/API.h. No SDK offsets,
// Lua ABI, newer function slots or third-party plugin implementation are used.
#pragma once
#include <cstddef>

struct RefVersion { int major, minor, patch; const char* game_name; };
using RefPresent = void (*)();
struct RefFunctionsPrefix {
    void* on_lua_state_created;
    void* on_lua_state_destroyed;
    bool (*on_present)(RefPresent);
};
struct RefInitializePrefix {
    void* reframework_module;
    const RefVersion* version;
    const RefFunctionsPrefix* functions;
};
static_assert(sizeof(void*) == 8, "Rise plugin is x64 only");
static_assert(offsetof(RefFunctionsPrefix, on_present) == 16);
static_assert(offsetof(RefInitializePrefix, functions) == 16);
