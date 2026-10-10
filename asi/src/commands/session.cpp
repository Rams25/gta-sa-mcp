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
	json resetHistory = json::array();
	for (const auto& entry : window::ResetHistory()) {
		resetHistory.push_back({
			{"ordinal", entry.ordinal}, {"tick_ms", entry.tick},
			{"has_parameters", entry.hasParameters}, {"completed", entry.completed},
			{"requested", {{"width", entry.requestedWidth}, {"height", entry.requestedHeight}, {"windowed", entry.requestedWindowed}}},
			{"forwarded", {{"width", entry.forwardedWidth}, {"height", entry.forwardedHeight}, {"windowed", entry.forwardedWindowed}}},
			{"hresult", entry.completed ? json(entry.result) : json(nullptr)}
		});
	}
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
		{ "reset_history", resetHistory },
		{ "reset_history_limit", 64 },
		{ "resolution", json::array({ game::ScreenWidth(), game::ScreenHeight() }) },
		{ "methods", dispatcher::Methods() },
	};
}

json WindowedModes(const json&) {
    json modes=json::array();for(const auto& m:window::WindowedModes())modes.push_back({{"index",m.index},{"width",m.width},{"height",m.height}});
    return {{"current_index",game::At<int>(0xC97C18)},{"modes",modes}};
}
json ChangeWindowedMode(const json& p) {
    if(!p.contains("mode_index") || !p["mode_index"].is_number_integer())throw CommandError("invalid_params","mode_index must be integer from get_windowed_modes");
    const auto mode=p["mode_index"].get<long long>();if(mode<0 || mode>511)throw CommandError("invalid_params","mode_index outside0..511");
    const int previous=game::At<int>(0xC97C18);const unsigned before=window::ResetCount();
    if(!window::ResetCurrentWindowedMode(static_cast<int>(mode)))throw CommandError("reset_unavailable","Requires verified native mode,640..1920x480..1080,in-game windowed/no_activate");
    return {{"previous_mode_index",previous},{"requested_mode_index",mode},{"reset_calls",window::ResetCount()-before},{"hresult",window::LastResetResult()},{"scope","native windowed mode request; check reset history and restore previous index; no exclusive fullscreen"}};
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
	dispatcher::Register("get_windowed_modes", Phase::Tick, WindowedModes);
	dispatcher::Register("change_windowed_mode", Phase::Tick, ChangeWindowedMode);
	dispatcher::Register("reset_windowed_device", Phase::Tick, ResetWindowed);
}
