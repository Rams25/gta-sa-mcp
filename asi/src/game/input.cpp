#include "input.hpp"
#include "sdk.hpp"
#include "../core/memory.hpp"
#include "../core/config.hpp"
#include <windows.h>
#include <cstring>

namespace game::input {
namespace {
constexpr std::uintptr_t kPad = 0xB73458;
std::uintptr_t nativeUpdate = 0;
State requested{};
unsigned remaining = 0, applied = 0;
ULONGLONG deadline = 0;
bool releasePending = false;
const char* reason = "idle";
void __cdecl Update() {
    reinterpret_cast<void (__cdecl*)()>(nativeUpdate)();
    AfterUpdatePads();
}
}
bool Install() {
    if (nativeUpdate) return true;
    // CGame::Process's call, NOT the outer call replaced by SA-MP and NOT
    // a global pad/ped hook that could execute with remote control context.
    const unsigned char expected[]{0xE8,0xE5,0x5E,0x00,0x00};
    if (std::memcmp(reinterpret_cast<void*>(0x53BEE6), expected, sizeof(expected))) return false;
    nativeUpdate = mem::HookCall(0x53BEE6, reinterpret_cast<void*>(Update));
    return nativeUpdate != 0;
}
void Start(const State& state, unsigned frames, unsigned timeoutMs) {
    requested = state;
    remaining = frames;
    applied = 0;
    deadline = GetTickCount64() + timeoutMs;
    releasePending = false;
    reason = "scheduled";
}
void Release() {
    releasePending = remaining != 0 || releasePending;
    remaining = 0;
    reason = "released";
}
void AfterUpdatePads() {
    const bool isolate = GetConfig().isolatePhysicalInput;
    if (!remaining && !releasePending && !isolate) return;
    // Never touch a pad while SA-MP has switched CWorld::PlayerInFocus.
    // Menu/deadline cancellation cannot bypass native controls-disabled gates.
    if (At<std::uint8_t>(0xB7CD74) != 0) {
        remaining = 0; releasePending = false; reason = "nonlocal_context"; return;
    }
    if (!PlayerPed() || At<bool>(0xBA67A4)) {
        // The menu owns its input. Do not neutralize or replay gameplay there.
        remaining = 0; releasePending = false; reason = "player_unavailable_or_menu";
        return;
    }
    if (remaining && GetTickCount64() >= deadline) {
        remaining = 0; releasePending = true; reason = "deadline";
    }
    if (releasePending || !remaining) {
        // One neutral sample gives native edge consumers a release. Thereafter
        // the next UpdatePads owns NewState again unless idle isolation is enabled.
        // No old pad snapshot is restored. SA-MP/UI direct OS polling is unaffected.
        std::memset(reinterpret_cast<void*>(kPad), 0, sizeof(State));
        releasePending = false;
        return;
    }
    // Native CPad::Update has already copied prior NewState to OldState and
    // reconciled physical input. Only replace local NewState, never OldState,
    // PCTemp*, disable flags, remote backups, or the OS keyboard/mouse.
    std::memcpy(reinterpret_cast<void*>(kPad), requested.data(), sizeof(State));
    ++applied;
    if (--remaining == 0) { releasePending = true; reason = "completed"; }
    else reason = "active";
}
Status GetStatus() { return {nativeUpdate != 0, GetConfig().isolatePhysicalInput, remaining, applied, reason}; }
static_assert(sizeof(State) == 0x30, "CControllerState layout");
}
