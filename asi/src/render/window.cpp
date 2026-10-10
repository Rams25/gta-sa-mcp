#include "window.hpp"

#include "../core/config.hpp"
#include "../core/log.hpp"
#include "../core/memory.hpp"
#include "../core/dispatcher.hpp"
#include <atomic>
#include <cstring>
#include <mutex>
#include "../game/sdk.hpp"

#include <windows.h>
#include <d3d9.h>

namespace window
{

namespace
{

using Direct3DCreate9Fn = IDirect3D9* (WINAPI*)(UINT);
using CreateWindowExAFn = HWND (WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
using CreateDeviceFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using ResetFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

// Vtable slots.
constexpr std::size_t kCreateDevice = 16;
constexpr std::size_t kReset = 16;

CreateWindowExAFn g_createWindow = nullptr;
CreateDeviceFn g_createDevice = nullptr;
ResetFn g_reset = nullptr;
using PresentFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
PresentFn g_present = nullptr;
std::atomic<unsigned> g_blockedMouse{0};
std::atomic<unsigned> g_resetCount{0};
std::atomic<long> g_lastResetResult{0};
std::atomic<unsigned> g_resetOrdinal{0};
std::mutex g_resetHistoryMutex;
std::vector<ResetRecord> g_resetHistory;
constexpr std::size_t kResetHistoryLimit = 64;
HWND g_window = nullptr;
BOOL WINAPI BackgroundWindowPos(HWND hwnd, HWND after, int x, int y, int width, int height, UINT flags)
{
	flags |= SWP_NOACTIVATE;
	if (hwnd == g_window) {
		x = GetConfig().x; y = GetConfig().y;
		flags &= ~SWP_NOMOVE;
	}
	// This plugin's own import is not patched; preserve the real API result.
	return SetWindowPos(hwnd, after, x, y, width, height, flags);
}
BOOL WINAPI IgnoreClip(const RECT*) { ++g_blockedMouse; return TRUE; }
BOOL WINAPI IgnoreCursorPos(int, int) { ++g_blockedMouse; return TRUE; }
HWND WINAPI IgnoreCapture(HWND) { ++g_blockedMouse; return nullptr; }
BOOL WINAPI IgnoreReleaseCapture() { return TRUE; }
BOOL WINAPI IgnoreForeground(HWND) { return TRUE; }
HWND WINAPI IgnoreFocus(HWND) { return nullptr; }
HRESULT STDMETHODCALLTYPE OnPresent(IDirect3DDevice9* device, const RECT* source, const RECT* dest, HWND overrideWindow, const RGNDATA* dirty)
{
	// This is the real device beneath SA-MP's proxy: its overlays are drawn now.
	dispatcher::PumpFrame();
	return g_present(device, source, dest, overrideWindow, dirty);
}

void ForceWindowed(D3DPRESENT_PARAMETERS* params)
{
	if (!params)
		return;
	params->Windowed = TRUE;
	params->FullScreen_RefreshRateInHz = 0; // must be 0 for a windowed device
}

// A borderless window of exactly the back buffer's size. With a title bar the game, which sizes
// its windowed back buffer from the whole window, would render a few pixels off.
void FitWindow(const D3DPRESENT_PARAMETERS* params)
{
	if (!g_window || !params || !params->BackBufferWidth || !params->BackBufferHeight)
		return;
	const Config& config = GetConfig();

	SetWindowLongW(g_window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
	SetWindowPos(g_window, HWND_NOTOPMOST, config.x, config.y,
		static_cast<int>(params->BackBufferWidth), static_cast<int>(params->BackBufferHeight),
		SWP_FRAMECHANGED | (config.noActivate ? SWP_NOACTIVATE : 0u));
}

HRESULT STDMETHODCALLTYPE OnReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params)
{
	ResetRecord record;
	record.ordinal = ++g_resetOrdinal;
	record.tick = GetTickCount();
	record.hasParameters = params != nullptr;
	if (params) {
		record.requestedWidth = params->BackBufferWidth;
		record.requestedHeight = params->BackBufferHeight;
		record.requestedWindowed = params->Windowed != FALSE;
	}
	ForceWindowed(params);
	// Keep the native requested dimensions: ScreenWidth/Height can still describe
	// the old device while GTA is processing a window resize.
	if (params) {
		record.forwardedWidth = params->BackBufferWidth;
		record.forwardedHeight = params->BackBufferHeight;
		record.forwardedWindowed = params->Windowed != FALSE;
	}
	{
		std::lock_guard<std::mutex> lock(g_resetHistoryMutex);
		if (g_resetHistory.size() == kResetHistoryLimit)
			g_resetHistory.erase(g_resetHistory.begin());
		g_resetHistory.push_back(record);
	}
	// Publish and log the attempt before entering the driver; a crash or blocked
	// call then remains distinguishable from a completed successful Reset.
	Log("Reset begin #" + std::to_string(record.ordinal) + " tick=" + std::to_string(record.tick)
		+ " requested=" + std::to_string(record.requestedWidth) + "x" + std::to_string(record.requestedHeight)
		+ " windowed=" + std::to_string(record.requestedWindowed)
		+ " forwarded=" + std::to_string(record.forwardedWidth) + "x" + std::to_string(record.forwardedHeight)
		+ " windowed=" + std::to_string(record.forwardedWindowed));
	const HRESULT result = g_reset(device, params);
	{
		std::lock_guard<std::mutex> lock(g_resetHistoryMutex);
		for (auto& entry : g_resetHistory) {
			if (entry.ordinal == record.ordinal) {
				entry.result = result;
				entry.completed = true;
				break;
			}
		}
	}
	Log("Reset end #" + std::to_string(record.ordinal) + " HRESULT=" + std::to_string(result));
	g_lastResetResult = result;
	++g_resetCount;
	if (SUCCEEDED(result))
		FitWindow(params);
	return result;
}

HRESULT STDMETHODCALLTYPE OnCreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
	D3DPRESENT_PARAMETERS* params, IDirect3DDevice9** device)
{
	ForceWindowed(params);
	const HRESULT result = g_createDevice(d3d, adapter, type, focus, flags, params, device);
	if (FAILED(result) || !device || !*device)
	{
		Log("CreateDevice failed in windowed mode, HRESULT " + std::to_string(static_cast<unsigned long>(result)));
		return result;
	}

	if (void* previous = mem::HookVtable(*device, 17, reinterpret_cast<void*>(&OnPresent)))
		g_present = reinterpret_cast<PresentFn>(previous);
	if (void* previous = mem::HookVtable(*device, kReset, reinterpret_cast<void*>(&OnReset)))
		g_reset = reinterpret_cast<ResetFn>(previous);
	g_window = params && params->hDeviceWindow ? params->hDeviceWindow : focus;
	FitWindow(params);
	if (params)
		Log("Windowed device created: " + std::to_string(params->BackBufferWidth) + "x" + std::to_string(params->BackBufferHeight));
	return result;
}

// Every IDirect3D9 of the process shares one vtable: patching it through an object of our own also
// catches the one the game creates later. (The game resolves Direct3DCreate9 by itself, without an
// import we could replace.)
void HookDirect3D()
{
	HMODULE library = LoadLibraryW(L"d3d9.dll");
	const auto create = library ? reinterpret_cast<Direct3DCreate9Fn>(GetProcAddress(library, "Direct3DCreate9")) : nullptr;
	IDirect3D9* d3d = create ? create(D3D_SDK_VERSION) : nullptr;
	if (!d3d)
	{
		Log("Windowed mode unavailable: Direct3DCreate9 failed.");
		return;
	}
	if (void* previous = mem::HookVtable(d3d, kCreateDevice, reinterpret_cast<void*>(&OnCreateDevice)))
		g_createDevice = reinterpret_cast<CreateDeviceFn>(previous);
	d3d->Release();
}

// The game creates its window before it starts Direct3D: the right moment, on the game thread and
// outside of any DLL initialisation.
HWND WINAPI OnCreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR title, DWORD style, int x, int y, int width, int height,
	HWND parent, HMENU menu, HINSTANCE instance, LPVOID param)
{
	if (GetConfig().noActivate && !parent)
	{
		exStyle |= WS_EX_NOACTIVATE;
		x = GetConfig().x; y = GetConfig().y;
	}
	const HWND created = g_createWindow(exStyle, className, title, style, x, y, width, height, parent, menu, instance, param);
	static bool done = false;
	if (!done)
	{
		done = true;
		HookDirect3D();
	}
	return created;
}

}

void ProtectDesktop()
{
	if (!GetConfig().noActivate) return;
	// DirectInput has its own USER32 cursor imports, independent of GTA's IAT.
	// Patch only an already-loaded module in this process; do not load/acquire
	// a device or issue even a NULL ClipCursor request to the shared desktop.
	if (HMODULE input = GetModuleHandleW(L"dinput8.dll"))
	{
		mem::HookModuleImport(input, "user32.dll", "ClipCursor", reinterpret_cast<void*>(&IgnoreClip));
		mem::HookModuleImport(input, "user32.dll", "SetCursorPos", reinterpret_cast<void*>(&IgnoreCursorPos));
	}
	// Restrict interception to these processes' modules, never user32 globally.
	// Even a NULL ClipCursor request is ignored: another app may own confinement.
	for (HMODULE module : {GetModuleHandleW(nullptr), GetModuleHandleW(L"samp.dll")})
	{
		if (!module) continue;
		mem::HookModuleImport(module, "user32.dll", "ClipCursor", reinterpret_cast<void*>(&IgnoreClip));
		mem::HookModuleImport(module, "user32.dll", "SetCursorPos", reinterpret_cast<void*>(&IgnoreCursorPos));
		mem::HookModuleImport(module, "user32.dll", "SetCapture", reinterpret_cast<void*>(&IgnoreCapture));
		mem::HookModuleImport(module, "user32.dll", "ReleaseCapture", reinterpret_cast<void*>(&IgnoreReleaseCapture));
		mem::HookModuleImport(module, "user32.dll", "SetForegroundWindow", reinterpret_cast<void*>(&IgnoreForeground));
		mem::HookModuleImport(module, "user32.dll", "SetFocus", reinterpret_cast<void*>(&IgnoreFocus));
		mem::HookModuleImport(module, "user32.dll", "SetWindowPos", reinterpret_cast<void*>(&BackgroundWindowPos));
	}
}
void RefreshCaptureHook()
{
	if (!GetConfig().windowed) return;
	auto* exposed = game::At<IDirect3DDevice9*>(0xC97C28);
	if (!exposed) return;
	IDirect3DDevice9* actual = nullptr;
	// Both verified SA-MP proxies forward QueryInterface to the underlying device.
	// SA-MP can replace the initially returned device/vtable after startup.
	if (FAILED(exposed->QueryInterface(__uuidof(IDirect3DDevice9), reinterpret_cast<void**>(&actual))) || !actual) return;
	if (void* previous = mem::HookVtable(actual, 17, reinterpret_cast<void*>(&OnPresent)))
		g_present = reinterpret_cast<PresentFn>(previous);
	if (void* previous = mem::HookVtable(actual, kReset, reinterpret_cast<void*>(&OnReset)))
		g_reset = reinterpret_cast<ResetFn>(previous);
	actual->Release();
}
bool CapturesAtPresent() { return g_present != nullptr; }
unsigned BlockedMouseCalls() { return g_blockedMouse.load(); }
std::vector<ResetRecord> ResetHistory()
{
	std::lock_guard<std::mutex> lock(g_resetHistoryMutex);
	return g_resetHistory;
}
unsigned ResetCount() { return g_resetCount.load(); }
long LastResetResult() { return g_lastResetResult.load(); }
std::vector<WindowedMode> WindowedModes()
{
    std::vector<WindowedMode> out;
    if (!GetConfig().windowed || !GetConfig().noActivate || !game::InGame()) return out;
    auto* modes=game::At<int*>(0xC97C48);const int count=game::At<int>(0xC97C40);
    if(!modes || count<1 || count>512)return out;
    for(int i=0;i<count;++i)if(modes[i*5]>=640 && modes[i*5]<=1920 && modes[i*5+1]>=480 && modes[i*5+1]<=1080)
        out.push_back({i,modes[i*5],modes[i*5+1]});
    return out;
}
bool ResetCurrentWindowedMode(int requestedMode)
{
	// Test command, game thread only. Use GTA's full video-mode lifecycle, not a
	// direct D3D Reset that would skip RenderWare's resource callbacks.
	if (!GetConfig().windowed || !GetConfig().noActivate || !game::InGame()) return false;
	const unsigned char expected[]{0x8B,0x44,0x24,0x04,0x50,0xE8,0xC6,0x29,0x0B,0x00};
	if (std::memcmp(reinterpret_cast<void*>(0x745C70), expected, sizeof(expected))) return false;
	RefreshCaptureHook();
	if (!g_reset) return false;
	auto& current = game::At<int>(0xC97C18);
	const int mode = requestedMode < 0 ? current : requestedMode;
	auto* modes = game::At<int*>(0xC97C48);
	const int count = game::At<int>(0xC97C40);
	if (!modes || count<1 || count>512 || mode < 0 || mode >= count) return false;
    if(requestedMode>=0 && (modes[mode*5]<640 || modes[mode*5]>1920 || modes[mode*5+1]<480 || modes[mode*5+1]>1080))return false;
	int& flags = modes[mode * 5 + 4];
	const int savedFlags = flags;
	flags &= ~1; // Never request exclusive fullscreen on the shared desktop.
	current = -1; // Force same-mode recreation, as the client's mode switch does.
	reinterpret_cast<void (__cdecl*)(int)>(0x745C70)(mode);
	current = mode;
	flags = savedFlags;
	ProtectDesktop();
	return true;
}
void Install()
{
	ProtectDesktop();
	if (!GetConfig().windowed)
		return;
	void* previous = mem::HookImport("user32.dll", "CreateWindowExA", reinterpret_cast<void*>(&OnCreateWindowExA));
	if (!previous)
	{
		Log("Windowed mode unavailable: the game does not import CreateWindowExA.");
		return;
	}
	g_createWindow = reinterpret_cast<CreateWindowExAFn>(previous);
}

}
