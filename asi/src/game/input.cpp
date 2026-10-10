#include "input.hpp"
#include "sdk.hpp"
#include "../core/memory.hpp"
#include "../core/config.hpp"
#include <windows.h>
#include <cstring>
#include <mutex>

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
namespace {
using AsyncKeyFn = SHORT (WINAPI*)(int);
AsyncKeyFn originalAsyncKey = nullptr;
std::mutex sampKeyMutex;
bool sampKeyInstalled = false, sampKeyActive = false;
int sampKey = 0;
ULONGLONG sampKeyDeadline = 0;
unsigned sampKeyPolls = 0, sampPressedPolls = 0, sampIsolatedPolls = 0;
const char* sampKeyReason = "idle";
bool IsSampTestKey(int key) { return key == VK_LEFT || key == VK_RIGHT || key == VK_SHIFT || key == VK_F5; }
void ExpireSampKey() {
    if (sampKeyActive && GetTickCount64() >= sampKeyDeadline) {
        sampKeyActive = false; sampKeyReason = "expired";
    }
}
SHORT WINAPI ReadSampKey(int key) {
    AsyncKeyFn next;
    {
        std::lock_guard<std::mutex> lock(sampKeyMutex);
        ExpireSampKey();
        if (sampKeyActive && IsSampTestKey(key)) {
            ++sampKeyPolls;
            if (key == sampKey) { ++sampPressedPolls; return static_cast<SHORT>(0x8000); }
            return 0; // Other exposed class-selection keys are neutral during this lease.
        }
        if (GetConfig().isolatePhysicalInput && IsSampTestKey(key)) {
            ++sampIsolatedPolls;
            return 0;
        }
        next = originalAsyncKey;
    }
    return next ? next(key) : 0;
}
bool EnsureSampKeyHookLocked() {
    if (!sampKeyInstalled) {
        HMODULE module = GetModuleHandleW(L"samp.dll");
        if (!module) return false;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const BYTE*>(module) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return false;
        void* previous = mem::HookModuleImport(module, "user32.dll", "GetAsyncKeyState", reinterpret_cast<void*>(&ReadSampKey));
        if (!previous || previous == reinterpret_cast<void*>(&ReadSampKey)) return false;
        originalAsyncKey = reinterpret_cast<AsyncKeyFn>(previous);
        sampKeyInstalled = true;
    }
    return true;
}
}
void RefreshSampKeyHook() {
    if (!GetConfig().isolatePhysicalInput) return;
    std::lock_guard<std::mutex> lock(sampKeyMutex);
    EnsureSampKeyHookLocked();
}
bool StartSampKey(int key, unsigned durationMs) {
    if (!IsSampTestKey(key) || !durationMs || durationMs > 5000) return false;
    std::lock_guard<std::mutex> lock(sampKeyMutex);
    if (!EnsureSampKeyHookLocked()) return false;
    sampKey = key; sampKeyDeadline = GetTickCount64() + durationMs;
    sampKeyPolls = sampPressedPolls = 0; sampKeyActive = true; sampKeyReason = "active";
    return true;
}
void ReleaseSampKey() {
    std::lock_guard<std::mutex> lock(sampKeyMutex);
    sampKeyActive = false; sampKeyReason = "cancelled";
}
SampKeyStatus GetSampKeyStatus() {
    std::lock_guard<std::mutex> lock(sampKeyMutex);
    ExpireSampKey();
    const auto now = GetTickCount64();
    const unsigned remainingMs = sampKeyActive && sampKeyDeadline > now ? static_cast<unsigned>(sampKeyDeadline-now) : 0;
    return {sampKeyInstalled,sampKeyActive,GetConfig().isolatePhysicalInput,sampKey,remainingMs,sampKeyPolls,sampPressedPolls,sampIsolatedPolls,sampKeyReason};
}
bool Install() {
    RefreshSampKeyHook(); // If SA-MP is already loaded, isolate before the first game frame.
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
    RefreshSampKeyHook(); // Retry on simulation frames before any pad/menu early return.
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
        // No old pad snapshot is restored. SA-MP class keys have a separate IAT gate.
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
