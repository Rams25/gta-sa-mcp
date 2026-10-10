// Windowed mode: the game renders into a normal desktop window instead of taking the screen, so it
// can run beside (or behind) the tools driving it.
#pragma once

#include <vector>

namespace window
{

// Called once when the plugin is loaded, before the game creates its Direct3D device.
void Install();
void ProtectDesktop();
void RefreshCaptureHook();
bool CapturesAtPresent();
unsigned BlockedMouseCalls();
struct ResetRecord
{
	unsigned ordinal = 0;
	unsigned long tick = 0;
	bool hasParameters = false;
	unsigned requestedWidth = 0, requestedHeight = 0;
	bool requestedWindowed = false;
	unsigned forwardedWidth = 0, forwardedHeight = 0;
	bool forwardedWindowed = false;
	bool completed = false;
	long result = 0; // meaningful only when completed
};
// Independent copy under a lock; safe for the Direct command thread.
std::vector<ResetRecord> ResetHistory();
unsigned ResetCount();
long LastResetResult();
bool ResetCurrentWindowedMode(int requestedMode = -1);
struct WindowedMode { int index, width, height; };
std::vector<WindowedMode> WindowedModes();

}
