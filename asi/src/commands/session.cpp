// Commands about the plugin itself; answered without waiting for the game thread.
#include "common.hpp"
#include "../render/window.hpp"
#include "../core/config.hpp"
#include "samp_pickup_fixture.hpp"


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

json SetWindowVisibility(const json& p) {
    if(!GetConfig().windowed || !GetConfig().noActivate)throw CommandError("protected_window_required","Requires windowed/no_activate");
    const std::string state=p.value("state","");
    if(state!="minimize" && state!="restore")throw CommandError("invalid_params","Fixed minimize or restore required");
    HWND hwnd=game::At<HWND>(0xC97C1C);DWORD owner=0;
    if(!IsWindow(hwnd) || !GetWindowThreadProcessId(hwnd,&owner) || owner!=GetCurrentProcessId())throw CommandError("own_window_required","No owned GTA window");
    const bool before=IsIconic(hwnd)!=FALSE;
    if(!ShowWindowAsync(hwnd,state=="minimize"?SW_SHOWMINNOACTIVE:SW_SHOWNOACTIVATE))throw CommandError("window_request_failed","Window state request failed");
    return {{"requested",state},{"was_minimized",before},{"scope","asynchronous owned-window state, no activation; confirm get_status"}};
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
        { "window_minimized", IsIconic(game::At<HWND>(0xC97C1C))!=FALSE },
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
json ResizeWindowed(const json& p) {
    if(!p.contains("width") || !p.contains("height") || !p["width"].is_number_integer() || !p["height"].is_number_integer())throw CommandError("invalid_params","Integer width and height required");
    const auto w=p["width"].get<long long>(),h=p["height"].get<long long>();
    if(w<640 || w>1920 || h<480 || h>1080)throw CommandError("invalid_params","Dimensions outside640..1920x480..1080");
    int oldW=0,oldH=0;
    if(!window::ResizeWindowed(static_cast<int>(w),static_cast<int>(h),oldW,oldH))throw CommandError("resize_unavailable","Requires own game window and windowed/no_activate");
    return {{"previous_width",oldW},{"previous_height",oldH},{"requested_width",w},{"requested_height",h},{"scope","Owned window resized without activation; native WM_SIZE processing may be deferred. Verify later status/capture dimensions and restore previous dimensions."}};
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
	dispatcher::Register("samp_dropped_pickup_fixture", Phase::Tick, samp_pickup_fixture::Invoke);
	dispatcher::Register("resize_windowed_client", Phase::Tick, ResizeWindowed);
	dispatcher::Register("get_status", Phase::Direct, GetStatus);
    dispatcher::Register("set_window_visibility",Phase::Direct,SetWindowVisibility);
	dispatcher::Register("get_windowed_modes", Phase::Tick, WindowedModes);
	dispatcher::Register("change_windowed_mode", Phase::Tick, ChangeWindowedMode);
	dispatcher::Register("reset_windowed_device", Phase::Tick, ResetWindowed);
}
