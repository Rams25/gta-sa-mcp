#pragma once
#include "samp_headmove.hpp"
#include "samp_ui_pins.hpp"
#include "../core/config.hpp"
#include "../render/window.hpp"
namespace samp_ui {
inline json Set(const json& p) {
    using samp_headmove::Require; using samp_headmove::Read;
    Require(p.contains("element") && p["element"].is_string() && (p.contains("event") || (p.contains("open") && p["open"].is_boolean())),"element and boolean open required");
    const std::string element=p["element"]; const bool open=p.value("open",false);
    Require(element=="scoreboard" || element=="chat" || element=="help" || element=="netstats" || element=="dialog","Unsupported fixed UI element");
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
    if(p.contains("event")) {
        const std::string event=p["event"];
        if(event=="command") {
            Require(element=="chat","Command requires chat");
            const std::string command=p.value("text","");
            Require(command=="/help" || command=="/shop" || command=="/kill","Only fixed test commands allowed");
            BYTE* dialog=object("dialog");BYTE visible=0;Read(dialog+0x28,&visible,1);Require(!visible,"Close dialog before submitting chat command");
            BYTE* chat=object("chat");BYTE* edit=nullptr;Read(chat+8,&edit,4);Require(edit!=nullptr,"Chat edit control unavailable");
            reinterpret_cast<void(__thiscall*)(void*)>(function("chat_open"))(chat);
            reinterpret_cast<void(__thiscall*)(void*,const char*,bool)>(function("edit_text"))(edit,command.c_str(),false);
            reinterpret_cast<void(__thiscall*)(void*)>(function("chat_submit"))(chat);
        } else {
            Require(element=="scoreboard" || element=="dialog","List or dialog required");
            const bool score=element=="scoreboard";BYTE* owner=object(score?"score":"dialog");
            BYTE visible=0;Read(owner+(score?(original?0:12):0x28),&visible,1);Require(visible!=0,"Requested UI is not visible");
            if(event=="accept" || event=="cancel") {
                Require(!score,"Accept/cancel is limited to server dialogs");
                const WPARAM key=event=="accept"?VK_RETURN:VK_ESCAPE;
                if(original) {
                    if(key==VK_RETURN)reinterpret_cast<int(__cdecl*)(int)>(function("dialog_key"))(static_cast<int>(key));
                    else reinterpret_cast<LRESULT(__stdcall*)(HWND,UINT,WPARAM,LPARAM)>(function("window_message"))(nullptr,WM_CHAR,key,0);
                } else reinterpret_cast<bool(__thiscall*)(void*,HWND,UINT,WPARAM,LPARAM)>(function("dialog_message"))(owner,nullptr,WM_KEYDOWN,key,0);
            } else {
                BYTE* list=nullptr;Read(owner+(original?(score?0x38:0x20):8),&list,4);Require(list!=nullptr,"List unavailable");
                if(event=="key") {
                    const std::string key=p.value("key","");
                    const UINT vk=key=="HOME"?VK_HOME:key=="END"?VK_END:key=="UP"?VK_UP:key=="DOWN"?VK_DOWN:key=="PAGEUP"?VK_PRIOR:key=="PAGEDOWN"?VK_NEXT:0;
                    Require(vk!=0,"Unsupported list key");
                    reinterpret_cast<bool(__thiscall*)(void*,UINT,WPARAM,LPARAM)>(function("list_key"))(list,WM_KEYDOWN,vk,0);
                } else {
                    Require(event=="wheel" || event=="double_click","Unsupported UI event");
                    const int x=p.value("x",0),y=p.value("y",0),steps=p.value("steps",0);
                    Require(x>=0 && x<=1920 && y>=0 && y<=1080 && steps>=-10 && steps<=10,"Coordinates or wheel steps outside bounds");
                    auto mouse=reinterpret_cast<bool(__thiscall*)(void*,UINT,POINT,WPARAM,LPARAM)>(function("list_mouse"));
                    POINT pt{x,y};
                    if(event=="wheel")mouse(list,WM_MOUSEWHEEL,pt,MAKEWPARAM(0,static_cast<WORD>(steps*WHEEL_DELTA)),0);
                    else {mouse(list,WM_LBUTTONDOWN,pt,MK_LBUTTON,0);mouse(list,WM_LBUTTONUP,pt,0,0);mouse(list,WM_LBUTTONDBLCLK,pt,MK_LBUTTON,0);mouse(list,WM_LBUTTONUP,pt,0,0);}
                }
            }
        }
    } else if(element=="netstats") {
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
