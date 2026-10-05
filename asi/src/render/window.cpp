#include "window.hpp"

#include "../core/config.hpp"
#include "../core/log.hpp"
#include "../core/memory.hpp"
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
HWND g_window = nullptr;

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
	ForceWindowed(params);
	// The resolution the game renders at, whatever size its window happens to have.
	if (params && game::ScreenWidth() > 0 && game::ScreenHeight() > 0)
	{
		params->BackBufferWidth = static_cast<UINT>(game::ScreenWidth());
		params->BackBufferHeight = static_cast<UINT>(game::ScreenHeight());
	}
	const HRESULT result = g_reset(device, params);
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

void Install()
{
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
