// Commands about the plugin itself; answered without waiting for the game thread.
#include "common.hpp"
#include "../render/window.hpp"


#include <windows.h>

namespace
{

const char* StateName(int state)
{
	switch (state)
	{
	case 0: case 1: case 2: case 3: case 4: return "intro";
	case 5: case 6: return "starting";
	case 7: return "main_menu";
	case 8: return "loading";
	case 9: return "in_game";
	default: return "unknown";
	}
}

json GetStatus(const json&)
{
	const int state = game::GameState();
	return {
		{ "plugin", "gta-sa-mcp" },
		{ "version", GTA_SA_MCP_VERSION },
		{ "pid", static_cast<unsigned>(GetCurrentProcessId()) },
		{ "game_state", StateName(state) },
		{ "ready", game::InGame() },
		{ "frame", dispatcher::FrameCount() },
		{ "capture_at_present", window::CapturesAtPresent() },
		{ "blocked_mouse_calls", window::BlockedMouseCalls() },
		{ "reset_count", window::ResetCount() },
		{ "last_reset_hresult", window::LastResetResult() },
		{ "resolution", json::array({ game::ScreenWidth(), game::ScreenHeight() }) },
		{ "methods", dispatcher::Methods() },
	};
}

json ResetWindowed(const json&)
{
	const unsigned before = window::ResetCount();
	if (!window::ResetCurrentWindowedMode())
		throw CommandError("reset_unavailable", "Requires verified GTA in-game code and windowed/no_activate configuration");
	return {{"reset_calls", window::ResetCount() - before},
		{"hresult", window::LastResetResult()}, {"scope", "same-mode windowed native lifecycle; not fullscreen acceptance"}};
}

}

void RegisterSessionCommands()
{
	dispatcher::Register("get_status", Phase::Direct, GetStatus);
	dispatcher::Register("reset_windowed_device", Phase::Tick, ResetWindowed);
}
