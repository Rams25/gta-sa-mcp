#include "window.hpp"

#include "../core/config.hpp"
#include "../core/log.hpp"
#include "../core/memory.hpp"

#include <windows.h>
#include <d3d9.h>

namespace window
{

namespace
{

using Direct3DCreate9Fn = IDirect3D9* (WINAPI*)(UINT);
using CreateDeviceFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using ResetFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

// Vtable slots.
constexpr std::size_t kCreateDevice = 16;
constexpr std::size_t kReset = 16;

Direct3DCreate9Fn g_create9 = nullptr;
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

// A titled window whose client area is exactly the back buffer.
void FitWindow(const D3DPRESENT_PARAMETERS* params)
{
	if (!g_window || !params || !params->BackBufferWidth || !params->BackBufferHeight)
		return;
	const Config& config = GetConfig();

	const LONG style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
	SetWindowLongW(g_window, GWL_STYLE, style);
	RECT rect { 0, 0, static_cast<LONG>(params->BackBufferWidth), static_cast<LONG>(params->BackBufferHeight) };
	AdjustWindowRect(&rect, static_cast<DWORD>(style), FALSE);
	SetWindowPos(g_window, HWND_NOTOPMOST, config.x, config.y, rect.right - rect.left, rect.bottom - rect.top,
		SWP_FRAMECHANGED | (config.noActivate ? SWP_NOACTIVATE : 0u));
}

HRESULT STDMETHODCALLTYPE OnReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params)
{
	ForceWindowed(params);
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

IDirect3D9* WINAPI OnDirect3DCreate9(UINT sdkVersion)
{
	IDirect3D9* d3d = g_create9(sdkVersion);
	if (d3d)
		if (void* previous = mem::HookVtable(d3d, kCreateDevice, reinterpret_cast<void*>(&OnCreateDevice)))
			g_createDevice = reinterpret_cast<CreateDeviceFn>(previous);
	return d3d;
}

}

void Install()
{
	if (!GetConfig().windowed)
		return;
	void* previous = mem::HookImport("d3d9.dll", "Direct3DCreate9", reinterpret_cast<void*>(&OnDirect3DCreate9));
	if (!previous)
	{
		Log("Windowed mode unavailable: the game does not import Direct3DCreate9.");
		return;
	}
	g_create9 = reinterpret_cast<Direct3DCreate9Fn>(previous);
}

}
