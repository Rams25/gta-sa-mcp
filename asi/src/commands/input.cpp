#include "common.hpp"
#include "../game/input.hpp"
#include "samp_headmove.hpp"

namespace {
json Status() {
    const auto s = game::input::GetStatus();
    return {{"available",s.available},{"isolate_physical",s.isolated},{"remaining_frames",s.remaining},
        {"applied_frames",s.applied},{"state",s.reason},
        {"scope","local GTA pad 0; no desktop input; release occurs at the next simulation pad update"}};
}
int Integer(const json& p, const char* key, int fallback, int low, int high) {
    if (!p.contains(key)) return fallback;
    if (!p[key].is_number_integer()) throw CommandError("invalid_params",std::string(key)+" must be an integer");
    const auto value=p[key].get<std::int64_t>();
    if(value<low || value>high) throw CommandError("invalid_params",std::string(key)+" is outside the allowed range");
    return static_cast<int>(value);
}
json Set(const json& p) {
    if (!game::input::GetStatus().available) throw CommandError("input_unavailable","Verified local pad hook was not installed");
    game::input::State state{};
    const char* axes[]{"left_x","left_y","right_x","right_y"};
    for(unsigned i=0;i<4;++i) state[i]=static_cast<std::int16_t>(Integer(p,axes[i],0,-128,128));
    const char* buttons[]{"left_shoulder1","left_shoulder2","right_shoulder1","right_shoulder2",
        "dpad_up","dpad_down","dpad_left","dpad_right","start","select","square","triangle","cross","circle","shock_left","shock_right","chat_indicated","walk","vehicle_mouse_look","radio_track_skip"};
    if(p.contains("buttons")) {
        if(!p["buttons"].is_array()) throw CommandError("invalid_params","buttons must be an array");
        for(const auto& button:p["buttons"]) {
            if(!button.is_string()) throw CommandError("invalid_params","button names must be strings");
            bool found=false;
            // Pause/start is intentionally not exposed: this lease drives game controls.
            for(unsigned i=0;i<20;++i) if(i!=8 && i!=16 && button==buttons[i]) {state[i+4]=128;found=true;break;}
            if(!found) throw CommandError("invalid_params","unknown or unsupported pad button: "+button.get<std::string>());
        }
    }
    const auto frames=Integer(p,"frames",1,1,300);
    const auto timeout=Integer(p,"timeout_ms",5000,1,10000);
    game::input::Start(state,frames,timeout);
    return Status();
}
json SampStatus(const json&) {
    const auto s=game::input::GetSampKeyStatus();
    return {{"installed",s.installed},{"active",s.active},{"isolate_physical",s.isolated},{"isolated_polls",s.isolatedPolls},{"virtual_key",s.key},
        {"remaining_ms",s.remainingMs},{"overridden_polls",s.polls},{"pressed_polls",s.pressedPolls},
        {"state",s.reason},{"scope","samp.dll GetAsyncKeyState import only; no OS input or focus changes"}};
}
json SampSet(const json& p) {
    if(!p.contains("key") || !p["key"].is_string()) throw CommandError("invalid_params","key must be LEFT, RIGHT or SHIFT");
    const std::string name=p["key"].get<std::string>();
    const int key=name=="LEFT"?0x25:name=="RIGHT"?0x27:name=="SHIFT"?0x10:0;
    if(!key) throw CommandError("invalid_params","key must be LEFT, RIGHT or SHIFT");
    const int duration=Integer(p,"duration_ms",150,1,5000);
    if(!game::input::StartSampKey(key,duration)) throw CommandError("input_unavailable","x86 samp.dll GetAsyncKeyState import unavailable");
    return SampStatus(p);
}
json SampRelease(const json& p) {game::input::ReleaseSampKey();return SampStatus(p);}
json Release(const json&) {game::input::Release();return Status();}
json Get(const json&) {return Status();}
}
void RegisterInputCommands() {
    dispatcher::Register("invoke_samp_headmove",Phase::Tick,samp_headmove::Invoke);
    dispatcher::Register("set_samp_key",Phase::Tick,SampSet);
    dispatcher::Register("release_samp_key",Phase::Direct,SampRelease);
    dispatcher::Register("get_samp_key",Phase::Direct,SampStatus);
    dispatcher::Register("set_game_input",Phase::Tick,Set);
    dispatcher::Register("release_game_input",Phase::Tick,Release);
    dispatcher::Register("get_game_input",Phase::Tick,Get);
}
