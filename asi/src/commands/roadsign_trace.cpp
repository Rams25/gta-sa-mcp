// Opt-in diagnostics only: observe native call returns without substituting them.
#include "common.hpp"
#include "../core/memory.hpp"
#include "../core/log.hpp"
#include <windows.h>
#include <array>
#include <mutex>
#include <cstdio>
#include <cstring>

namespace {
struct Event {
    unsigned sequence, stage, value, tick, charset, raster, pixels;
    unsigned sourceStride, destinationStride, readable, thread, source, destination;
};
std::mutex traceMutex;
struct TraceFile {
    unsigned magic, version, eventSize, capacity, committed;
    Event events[512];
};
TraceFile* persisted = nullptr;
HANDLE traceFile = INVALID_HANDLE_VALUE, mapping = nullptr;
std::wstring tracePath;
std::array<Event, 512> history{};
void OpenTraceFile() {
    if (persisted) { FlushViewOfFile(persisted,0); UnmapViewOfFile(persisted); persisted=nullptr; }
    if (mapping) { CloseHandle(mapping); mapping=nullptr; }
    if (traceFile != INVALID_HANDLE_VALUE) { CloseHandle(traceFile); traceFile=INVALID_HANDLE_VALUE; }
    tracePath = PluginDirectory() + L"roadsign-trace-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".bin";
    traceFile=CreateFileW(tracePath.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(traceFile==INVALID_HANDLE_VALUE) throw CommandError("trace_file_failed","Cannot create a fresh trace file");
    mapping=CreateFileMappingW(traceFile,nullptr,PAGE_READWRITE,0,sizeof(TraceFile),nullptr);
    if(!mapping) throw CommandError("trace_mapping_failed","Cannot map trace file");
    persisted=static_cast<TraceFile*>(MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,sizeof(TraceFile)));
    if(!persisted) throw CommandError("trace_view_failed","Cannot map trace view");
    *persisted={}; persisted->magic=0x39313352; persisted->version=1;
    persisted->eventSize=sizeof(Event); persisted->capacity=512;
}
unsigned sequence = 0;
bool enabled = false;
unsigned installed = 0;

bool ReadWord(unsigned address, unsigned* result) {
    __try { *result = *reinterpret_cast<const unsigned*>(address); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *result = 0; return false; }
}
void __cdecl Record(unsigned stage, unsigned value, unsigned* args) noexcept {
    // Every wrapper saves registers, flags and x87/SSE state around this call.
    const DWORD lastError = GetLastError();
    try {
        std::lock_guard<std::mutex> lock(traceMutex);
        if (enabled) {
            Event e{}; e.sequence = ++sequence; e.stage = stage;
            e.value = value; e.tick = GetTickCount(); e.thread=GetCurrentThreadId();
            if (ReadWord(0xC3EF84, &e.charset)) e.readable |= 1;
            if (e.charset && ReadWord(e.charset, &e.raster)) e.readable |= 2;
            if (ReadWord(0xC3EF88, &e.pixels)) e.readable |= 4;
            // Copy-call args: text,count,sourceRaster,palette,destinationRaster.
            // Read bounded fields only; no traversal of entity/model pointers.
            if (stage == 3) {
                unsigned src = 0, dst = 0;
                if (ReadWord(reinterpret_cast<unsigned>(args + 2), &src) && src &&
                    ReadWord(src + 0x18, &e.sourceStride)) e.readable |= 8;
                e.source=src;
                if (ReadWord(reinterpret_cast<unsigned>(args + 4), &dst) && dst &&
                    ReadWord(dst + 0x18, &e.destinationStride)) e.readable |= 16;
                e.destination=dst;
            }
            history[(e.sequence - 1) % history.size()] = e;
            // Fixed mapped ring: no allocation, file I/O API or formatted log here.
            // The OS keeps dirty mapped pages after ordinary process termination.
            if (persisted) {
                persisted->events[(e.sequence-1)%512]=e;
                MemoryBarrier();
                InterlockedExchange(reinterpret_cast<volatile LONG*>(&persisted->committed),e.sequence);
            }
        }
    } catch (...) { /* Diagnostic recording failure must not replace a native return. */ }
    SetLastError(lastError);
}

// Rewrite only the return address of a verified cdecl call. Argument stack
// and entry registers stay unchanged. After native RET, restore all machine
// state around the observer and jump to the original fixed continuation.
#define OBSERVER(Name, Id, Target, Continue) \
    unsigned Name##Target = Target; \
    unsigned Name##Continue = Continue; \
    __declspec(naked) void Name() { \
        __asm { mov dword ptr [esp], offset returned } \
        __asm { jmp dword ptr [Name##Target] } \
        __asm { returned: pushfd } \
        __asm { pushad } \
        __asm { mov ebp, esp } \
        __asm { sub esp, 528 } \
        __asm { and esp, 0FFFFFFF0h } \
        __asm { fxsave [esp] } \
        __asm { cld } \
        __asm { lea eax, [ebp+36] } \
        __asm { push eax } \
        __asm { push dword ptr [ebp+28] } \
        __asm { push Id } \
        __asm { call Record } \
        __asm { add esp, 12 } \
        __asm { fxrstor [esp] } \
        __asm { mov esp, ebp } \
        __asm { popad } \
        __asm { popfd } \
        __asm { jmp dword ptr [Name##Continue] } \
    }
OBSERVER(Material, 1, 0x74D990, 0x6FEDF5)
OBSERVER(Raster, 2, 0x7FB230, 0x6FECBF)
OBSERVER(CopyPixels, 3, 0x6FEB70, 0x6FECF5)
OBSERVER(LockRaster, 4, 0x7FB2D0, 0x6FEB82)
OBSERVER(Texture, 5, 0x7F37C0, 0x6FED02)
OBSERVER(Geometry, 6, 0x74CA90, 0x6FEEC7)
OBSERVER(Atomic, 7, 0x749C50, 0x6FF1FF)
OBSERVER(SetGeometry, 8, 0x749D40, 0x6FF23B)
OBSERVER(Frame, 9, 0x7F0410, 0x6FF285)
OBSERVER(Constructor, 10, 0x6FEDA0, 0x6FF343)
OBSERVER(LineTexture, 11, 0x6FECA0, 0x6FEE59)
#undef OBSERVER
struct Site { unsigned address, target; void* hook; const char* name; };
const Site sites[] = {
    {0x6FEDF0,0x74D990,Material,"material"},
    {0x6FECBA,0x7FB230,Raster,"raster"},
    {0x6FECF0,0x6FEB70,CopyPixels,"copy_pixels_AL"},
    {0x6FEB7D,0x7FB2D0,LockRaster,"raster_lock"},
    {0x6FECFD,0x7F37C0,Texture,"texture"},
    {0x6FEEC2,0x74CA90,Geometry,"geometry"},
    {0x6FF1FA,0x749C50,Atomic,"atomic"},
    {0x6FF236,0x749D40,SetGeometry,"set_geometry"},
    {0x6FF280,0x7F0410,Frame,"frame"},
    {0x6FF33E,0x6FEDA0,Constructor,"constructor"},
    {0x6FEE54,0x6FECA0,LineTexture,"line_texture"}
};
unsigned CallTarget(unsigned site) {
    if (*reinterpret_cast<const unsigned char*>(site) != 0xE8) return 0;
    int relative; std::memcpy(&relative, reinterpret_cast<const void*>(site+1),4);
    return site + 5 + relative;
}
json Snapshot(const json&) {
    std::lock_guard<std::mutex> lock(traceMutex);
    json events = json::array(), names = json::array();
    for (const auto& site : sites) names.push_back(site.name);
    const unsigned count = sequence < history.size() ? sequence : unsigned(history.size());
    for (unsigned n = sequence-count; n < sequence; ++n) {
        const auto& e = history[n % history.size()];
        events.push_back({{"sequence",e.sequence},{"stage",e.stage},{"value",e.value},
            {"tick",e.tick},{"charset",e.charset},{"raster",e.raster},{"pixels",e.pixels},
            {"source_stride",e.sourceStride},{"destination_stride",e.destinationStride},
            {"readable",e.readable},{"thread",e.thread},{"source",e.source},{"destination",e.destination}});
    }
    return {{"enabled",enabled},{"installed_sites",installed},{"total_events",sequence},
        {"events",events},{"stage_names_one_based",names},{"capacity",history.size()},
        {"overwritten_events",sequence-count},{"trace_file",ToUtf8(tracePath)},
        {"scope","Native call-return observations only; callback changes timing. Does not recover failed resources or suppress faults."}};
}
json Begin(const json&) {
    {
        std::lock_guard<std::mutex> lock(traceMutex);
        enabled=false;
        // Verify the whole site set before writing; retain ownership on retries.
        for (unsigned i=0; i<std::size(sites); ++i) {
            const unsigned expected = i < installed ? reinterpret_cast<unsigned>(sites[i].hook) : sites[i].target;
            if (CallTarget(sites[i].address) != expected)
                throw CommandError("signature_mismatch", "Roadsign call site differs; no additional hooks installed");
        }
        OpenTraceFile();
        for (; installed<std::size(sites); ++installed) {
            const auto& s = sites[installed];
            if (!mem::HookCall(s.address,s.hook))
                throw CommandError("hook_failed", "Roadsign trace partially installed; recording disabled; inspect installed_sites");
        }
        sequence=0; history={}; enabled=true;
    }
    Log("roadsign_trace enabled; 11 verified call sites; return values unchanged");
    return Snapshot({});
}
json End(const json&) {
    { std::lock_guard<std::mutex> lock(traceMutex); enabled=false; if(persisted) FlushViewOfFile(persisted,0); }
    // Hooks stay installed until process exit; safe during nested native calls.
    return Snapshot({});
}
}
void RegisterRoadsignTraceCommands() {
    dispatcher::Register("begin_roadsign_trace",Phase::Tick,Begin);
    dispatcher::Register("get_roadsign_trace",Phase::Direct,Snapshot);
    dispatcher::Register("end_roadsign_trace",Phase::Tick,End);
}
