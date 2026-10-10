#pragma once
#include "samp_headmove.hpp"
#include "samp_ui_pins.hpp"
#include "../core/config.hpp"
#include "../render/window.hpp"
namespace samp_ui {
inline json Set(const json& p) {
    using samp_headmove::Require; using samp_headmove::Read;
    Require(p.contains("element") && p["element"].is_string() && p.contains("open") && p["open"].is_boolean(),"element and boolean open required");
    const std::string element=p["element"]; const bool open=p["open"];
    Require(element=="scoreboard" || element=="chat" || element=="help" || element=="netstats","Unsupported fixed UI element");
    Require(GetConfig().windowed && GetConfig().noActivate,"Requires windowed/no_activate desktop protection");
    window::ProtectDesktop();
    HMODULE module=GetModuleHandleW(L"samp.dll");Require(module!=nullptr,"SA-MP not loaded");
    const auto sha=samp_headmove::Hash(module); json pin;
    for(const auto& candidate:json::parse(SampUiPins)) if(candidate["sha256"]==sha){pin=candidate;break;}
    Require(!pin.is_null(),"Unsupported SA-MP SHA256; regenerate fixed-symbol allowlist offline");
    auto base=reinterpret_cast<BYTE*>(module); IMAGE_DOS_HEADER dos;Read(base,&dos,sizeof(dos));
    Require(dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096,"Invalid loaded DOS header");
    IMAGE_NT_HEADERS32 pe;Read(base+dos.e_lfanew,&pe,sizeof(pe));
    Require(pe.Signature==IMAGE_NT_SIGNATURE && pe.FileHeader.Machine==IMAGE_FILE_MACHINE_I386 && pe.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC && pe.FileHeader.TimeDateStamp==pin["timestamp"].get<DWORD>() && pe.OptionalHeader.SizeOfImage==pin["size"].get<DWORD>(),"Loaded PE differs from pin");
    BYTE menu;Read(reinterpret_cast<void*>(0xBA67A4),&menu,1);Require(!menu,"Close native menu before UI probe");
    auto function=[&](const std::string& name)->BYTE* {
        Require(pin["functions"].contains(name),"This build has no implementation for this UI element");
        const auto& f=pin["functions"][name]; BYTE bytes[24];auto address=base+f["rva"].get<DWORD>();Read(address,bytes,24);
        for(unsigned i=0;i<24;++i)Require(!f["mask"][i].get<int>() || bytes[i]==f["bytes"][i].get<BYTE>(),"Loaded UI function differs from pin");
        return address;
    };
    auto object=[&](const char* name)->BYTE* {BYTE* obj=nullptr;Read(base+pin["globals"][name].get<DWORD>(),&obj,4);Require(obj!=nullptr,"UI object not initialized");return obj;};
    const bool original=pin["original"];
    if(element=="netstats") {
        if(open){
            unsigned duration=1000;
            if(p.contains("duration_ms")){Require(p["duration_ms"].is_number_unsigned() || p["duration_ms"].is_number_integer(),"duration_ms must be integer");const auto value=p["duration_ms"].get<long long>();Require(value>=1 && value<=5000,"duration_ms outside1..5000");duration=static_cast<unsigned>(value);}
            Require(game::input::StartSampKey(VK_F5,duration),"SA-MP key poll hook unavailable");
        } else game::input::ReleaseSampKey();
    } else if(element=="help") {
        BYTE* dialog=object("dialog");
        if(open) reinterpret_cast<void(__cdecl*)()>(function("help_open"))();
        else {
            int id=0;Read(dialog+(original?0x30:0x2c),&id,4);
            BYTE visible=0;Read(dialog+0x28,&visible,1);
            if(visible){
                // Only dismiss local help: never close a server dialog with the same ID.
                DWORD send=0;if(original)Read(dialog+0x81,&send,4);else Read(dialog+0x29,&send,1);
                Require(id==1 && !send,"Visible dialog is not local help");
                reinterpret_cast<void(__thiscall*)(void*)>(function("help_close"))(dialog);
            }
        }
    } else {
        const bool score=element=="scoreboard";BYTE* obj=object(score?"score":"chat");
        auto target=function(std::string(score?"score":"chat")+(open?"_open":"_close"));
        if(score && !open)reinterpret_cast<void(__thiscall*)(void*,bool)>(target)(obj,true);
        else reinterpret_cast<void(__thiscall*)(void*)>(target)(obj);
    }
    window::ProtectDesktop();
    return {{"dispatched",true},{"element",element},{"requested_open",open},{"samp_sha256",sha},{"scope","fixed native UI method or bounded F5 polling; inspect screenshot for visible result; no OS input"}};
}
}
