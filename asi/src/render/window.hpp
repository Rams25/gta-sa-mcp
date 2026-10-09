// Windowed mode: the game renders into a normal desktop window instead of taking the screen, so it
// can run beside (or behind) the tools driving it.
#pragma once

namespace window
{

// Called once when the plugin is loaded, before the game creates its Direct3D device.
void Install();
void ProtectDesktop();
void RefreshCaptureHook();
bool CapturesAtPresent();
unsigned BlockedMouseCalls();

}
