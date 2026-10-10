#pragma once
#include "samp_headmove.hpp"
#include "samp_ui_pins.hpp"
#include "../core/config.hpp"
#include "../core/memory.hpp"
#include "../render/window.hpp"
namespace samp_ui {
// Temporary, same-thread cursor input to the native hit-test. No desktop cursor move.
inline thread_local bool fixedCursorActive=false;
inline thread_local POINT fixedCursor{};
inline BOOL WINAPI FixedCursor(LPPOINT p) {
    if(fixedCursorActive && p){*p=fixedCursor;return TRUE;}
    return GetCursorPos(p); // This ASI import is never replaced.
}
struct CursorScope {
    HMODULE module; void* previous;
    CursorScope(HMODULE m, POINT p):module(m),previous(nullptr) {
        fixedCursor=p;fixedCursorActive=true;
        previous=mem::HookModuleImport(module,"user32.dll","GetCursorPos",reinterpret_cast<void*>(&FixedCursor));
        if(!previous)fixedCursorActive=false;
    }
    ~CursorScope(){fixedCursorActive=false;if(previous)mem::HookModuleImport(module,"user32.dll","GetCursorPos",previous);}
};
inline json Set(const json& p) {
    using samp_headmove::Require; using samp_headmove::Read;
    Require(p.contains("element") && p["element"].is_string() && (p.contains("event") || (p.contains("open") && p["open"].is_boolean())),"element and boolean open required");
    const std::string element=p["element"]; const bool open=p.value("open",false);
    Require(element=="scoreboard" || element=="chat" || element=="help" || element=="netstats" || element=="hud_hidden" || element=="textdraw" || element=="dialog" || element=="editor" || element=="object_selection","Unsupported fixed UI element");
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
    const bool nativeLayout=pin.value("native_layout",original);
    json sortResult, editorResult;
    if(p.contains("sort_mode")) {
        Require(element=="scoreboard" && !p.contains("event"),"sort_mode requires scoreboard open/close request");
        Require(p["sort_mode"].is_number_integer(),"sort_mode must be integer 0..2");
        const auto mode=p["sort_mode"].get<long long>();Require(mode>=0 && mode<=2,"sort_mode outside 0..2");
        BYTE* score=object("score");
        if(original || !pin["functions"].contains("score_sort")) {
            // Fixed reviewed field fallback only; constructor layout checked offline for this SHA.
            Require(pin.contains("score_sort_offset") && pin["score_sort_offset"].is_number_integer(),"Sort field not reviewed for this build");
            const auto offset=pin["score_sort_offset"].get<unsigned>();
            Require(original ? offset==0x40u : (nativeLayout ? offset==0x40u : offset==0x1cu),"Unexpected sort field layout");
            if(!original)function("score_ctor");
            auto refresh=function("score_update");
            const int value=static_cast<int>(mode);int previous=0;Read(score+offset,&previous,4);
            SIZE_T written=0;Require(WriteProcessMemory(GetCurrentProcess(),score+offset,&value,4,&written)&&written==4,"Score sort write failed");
            reinterpret_cast<void(__thiscall*)(void*)>(refresh)(score);
            sortResult={{"previous",previous},{"requested",value}};
        } else {
            auto setter=function("score_sort");
            reinterpret_cast<void(__thiscall*)(void*,int)>(setter)(score,static_cast<int>(mode));
            sortResult={{"requested",static_cast<int>(mode)}};
        }
        if(pin.contains("score_sort_offset") && pin["score_sort_offset"].is_number_integer()) {
            int applied=0;Read(score+pin["score_sort_offset"].get<unsigned>(),&applied,4);
            sortResult["applied"]=applied;
            Require(applied==mode,"Score sort selector did not retain requested mode");
        }
    }
    if(p.contains("event")) {
        const std::string event=p["event"];
        if(element=="editor") {
            Require(GetConfig().pipe=="gta-sa-mcp-original-trial" || GetConfig().pipe=="gta-sa-mcp-rebuilt-trial","Editor probe requires isolated trial pipe");
            Require(nativeLayout,"This build retains the rewritten editor; native editor probe unavailable");
            Require(event=="mode" || event=="adjust" || event=="finish" || event=="icon","Fixed editor mode/adjust/finish event required");
            BYTE* editor=original?object("editor"):reinterpret_cast<BYTE*(__cdecl*)()>(function("editor_get"))();
            Require(editor!=nullptr,"Native editor unavailable");
            DWORD active=0,target=0;int mode=0;Read(editor+0x80,&active,4);Read(editor+0x78,&target,4);Read(editor+0x7c,&mode,4);
            Require(active!=0 && (target==1 || target==2),"Start editing through the isolated server fixture first");
            editorResult={{"before_mode",mode},{"target_type",target}};
            if(event=="icon") {
                const std::string icon=p.value("icon","");
                const int hovered=icon=="move"?3:icon=="rotate"?4:icon=="scale"?5:icon=="save"?10:-1;
                Require(hovered>=0,"Fixed move/rotate/scale/save icon required");
                BYTE dragging=0,allowScale=0;Read(editor+0xA3,&dragging,1);Read(editor+0xA2,&allowScale,1);
                Require(!dragging,"Release editor drag before icon probe");
                Require(hovered!=5 || allowScale,"Scale icon unavailable for this target");
                auto handler=function("editor_message");int previous=0;Read(editor+0x113,&previous,4);
                SIZE_T written=0;Require(WriteProcessMemory(GetCurrentProcess(),editor+0x113,&hovered,4,&written)&&written==4,"Icon hover seed failed");
                const bool handled=original?reinterpret_cast<int(__thiscall*)(void*,UINT,WPARAM,LPARAM)>(handler)(editor,WM_LBUTTONUP,0,0)!=0:
                    reinterpret_cast<bool(__thiscall*)(void*,UINT)>(handler)(editor,WM_LBUTTONUP);
                WriteProcessMemory(GetCurrentProcess(),editor+0x113,&previous,4,&written);
                Require(handled,"Native editor did not consume icon release");
                Read(editor+0x7c,&mode,4);Read(editor+0x80,&active,4);
                editorResult["icon"]=icon;editorResult["applied_mode"]=mode;editorResult["active"]=active;
                editorResult["hover_seeded"]=true;
            } else if(event=="mode") {
                Require(p.contains("mode") && p["mode"].is_number_integer(),"Integer editor mode required");
                const int requested=p["mode"].get<int>();Require(requested>=0 && requested<=2,"Editor mode outside0..2");
                reinterpret_cast<void(__thiscall*)(void*,int)>(function("editor_mode"))(editor,requested);
                Read(editor+0x7c,&mode,4);editorResult["applied_mode"]=mode;
            } else if(event=="adjust") {
                const std::string axis=p.value("axis","");const int index=axis=="x"?0:axis=="y"?1:axis=="z"?2:-1;
                Require(index>=0,"Fixed x/y/z editor axis required");
                Require(p.contains("delta") && p["delta"].is_number(),"Fixed editor delta required");
                const double delta=p["delta"].get<double>();Require(delta==0.25 || delta==-0.25,"Editor delta must be+0.25 or-0.25");
                if(target==1) {
                    // 072FF0 mouse-down seeds +C3 before 072CB0 translation. Each bounded
                    // adjustment represents a fresh drag; invoke only its matrix-read precondition.
                    WORD id=0;Read(editor+0x88,&id,2);Require(id<2100,"Editor object ID outside valid pool");
                    BYTE* pools=nullptr;Read(object("netgame")+0x3DE,&pools,4);Require(pools!=nullptr,"NetGame pools unavailable");
                    BYTE* pool=nullptr;Read(pools+0x14,&pool,4);Require(pool!=nullptr,"Object pool unavailable");
                    BYTE* selected=reinterpret_cast<BYTE*(__thiscall*)(void*,WORD)>(function("editor_get_object"))(pool,id);
                    Require(selected!=nullptr,"Selected native object unavailable");
                    reinterpret_cast<void(__thiscall*)(void*,void*)>(function("entity_matrix"))(selected,editor+0xC3);
                    editorResult["drag_matrix_seeded"]=true;
                }
                reinterpret_cast<void(__thiscall*)(void*,int,float)>(function("editor_adjust"))(editor,index,static_cast<float>(delta));
                editorResult["axis"]=axis;editorResult["delta"]=delta;
            } else {
                const std::string result=p.value("result","");Require(result=="save" || result=="cancel","Editor finish requires save/cancel");
                reinterpret_cast<void(__thiscall*)(void*,int)>(function("editor_finish"))(editor,result=="save"?1:0);
                Read(editor+0x80,&active,4);Require(active==0,"Editor did not finish");editorResult["result"]=result;
            }
        } else if((element=="textdraw" || element=="object_selection") && event=="click") {
            Require(nativeLayout,"Textdraw hit-test requires native layout");
            Require(GetConfig().pipe=="gta-sa-mcp-original-trial" || GetConfig().pipe=="gta-sa-mcp-rebuilt-trial","Selection probe requires isolated pipe");
            const int x=p.value("x",-1),y=p.value("y",-1);
            int width=0,height=0;Read(reinterpret_cast<void*>(0xC17044),&width,4);Read(reinterpret_cast<void*>(0xC17048),&height,4);
            Require(x>=0 && y>=0 && x<width && y<height && width<=3840 && height<=2160,"Textdraw point outside current raster");
            HWND hwnd=nullptr;Read(reinterpret_cast<void*>(0xC97C1C),&hwnd,4);Require(IsWindow(hwnd),"Game window unavailable");
            POINT point{x,y};Require(ClientToScreen(hwnd,&point),"Textdraw coordinate conversion failed");
            BYTE* selector=nullptr;
            if(element=="object_selection") {
                selector=original?object("object_selection"):base+pin["globals"]["object_selection_data"].get<DWORD>();
                DWORD active=0;Read(selector,&active,4);Require(active!=0,"Start object selection with server fixture first");
            } else if(original)selector=object("selection");
            else {BYTE* pools=nullptr;Read(object("netgame")+0x3DE,&pools,4);Require(pools!=nullptr,"NetGame pools unavailable");Read(pools+0x20,&selector,4);}
            Require(selector!=nullptr,"Textdraw selector unavailable");
            const bool objectSelection=element=="object_selection";
            auto process=function(objectSelection?"object_selection_process":"selection_process");auto click=function(objectSelection?"object_selection_click":"selection_click");
            CursorScope cursor(module,point);Require(cursor.previous!=nullptr,"Native cursor import unavailable");
            reinterpret_cast<void(__thiscall*)(void*)>(process)(selector);
            if(original || objectSelection)reinterpret_cast<int(__thiscall*)(void*,UINT,WPARAM,LPARAM)>(click)(selector,WM_LBUTTONUP,0,MAKELPARAM(x,y));
            else reinterpret_cast<bool(__thiscall*)(void*,int,int)>(click)(selector,x,y);
            if(objectSelection) {WORD hovered=0xFFFF;Read(selector+4,&hovered,2);editorResult["selected_object"]=hovered;}
            editorResult.update({{"native_hit_test",true},{"x",x},{"y",y},{"scope","temporary per-process cursor import; desktop cursor unchanged"}});
        } else if(element=="chat" && event=="key") {
            const std::string key=p.value("key","");
            const DWORD vk=key=="UP"?VK_UP:key=="DOWN"?VK_DOWN:key=="PAGEUP"?VK_PRIOR:key=="PAGEDOWN"?VK_NEXT:0;
            Require(vk!=0,"Chat key must be UP/DOWN/PAGEUP/PAGEDOWN");
            reinterpret_cast<int(__cdecl*)(DWORD)>(function("chat_key"))(vk);
        } else if(event=="type_fixture") {
            Require(element=="dialog","Typing fixture is limited to visible server dialog");
            BYTE* owner=object("dialog");BYTE visible=0;Read(owner+0x28,&visible,1);Require(visible!=0,"Dialog not visible");
            const std::string sample=p.value("sample","");Require(sample=="ascii" || sample=="accent" || sample=="wide" || sample=="overwrite","Fixed typing sample required");
            auto proc=reinterpret_cast<LRESULT(__stdcall*)(HWND,UINT,WPARAM,LPARAM)>(function("window_message"));
            if(sample=="overwrite") {
                Require(!original && nativeLayout,"Original overwrite is proven memory corruption; machine witness substitutes for destructive live test");
                const unsigned offset=0x24;
                BYTE* edit=nullptr;Read(owner+offset,&edit,4);Require(edit!=nullptr,"Dialog edit missing");
                reinterpret_cast<void(__thiscall*)(void*,const char*,bool)>(function("edit_text"))(edit,"ABC",false);
                proc(nullptr,WM_KEYDOWN,VK_HOME,0);proc(nullptr,WM_KEYUP,VK_HOME,0);
                BYTE insert=0;Read(edit+0x11D,&insert,1);Require(insert==1,"Fresh edit is not in insert mode");
                proc(nullptr,WM_KEYDOWN,VK_INSERT,0);proc(nullptr,WM_KEYUP,VK_INSERT,0);
                proc(nullptr,WM_CHAR,'Z',0);
                const wchar_t* text=nullptr;Read(edit+0x4D,&text,4);Require(text!=nullptr,"Edit text missing");
                wchar_t result[4];Read(text,result,sizeof(result));int caret=0;Read(edit+0x119,&caret,4);
                proc(nullptr,WM_KEYDOWN,VK_INSERT,0);proc(nullptr,WM_KEYUP,VK_INSERT,0);
                Require(result[0]==L'Z'&&result[1]==L'B'&&result[2]==L'C'&&result[3]==0&&caret==1,"Overwrite fixture result differs from ZBC/caret1");
            }  else if(sample=="ascii")for(unsigned ch: {'U','_','1'})proc(nullptr,WM_CHAR,ch,0);
            else proc(nullptr,WM_CHAR,sample=="accent"?0xE9:0x4E2D,0);
        } else if(event=="command") {
            Require(element=="chat","Command requires chat");
            const std::string command=p.value("text","");
            Require(command=="/help" || command=="/shop" || command=="/kill" || command=="/timestamp" ||
                command=="/fontsize -3" || command=="/fontsize 0" || command=="/fontsize 5" ||
                command=="/pagesize 10" || command=="/pagesize 20" || command=="W chat fixture","Only fixed test commands allowed");
            Require(GetConfig().pipe=="gta-sa-mcp-original-trial" || GetConfig().pipe=="gta-sa-mcp-rebuilt-trial","Command probe requires isolated pipe");
            BYTE* dialog=object("dialog");BYTE visible=0;Read(dialog+0x28,&visible,1);Require(!visible,"Close dialog before submitting chat command");
            BYTE* chat=object("chat");BYTE* edit=nullptr;Read(chat+8,&edit,4);Require(edit!=nullptr,"Chat edit control unavailable");
            reinterpret_cast<void(__thiscall*)(void*)>(function("chat_open"))(chat);
            reinterpret_cast<void(__thiscall*)(void*,const char*,bool)>(function("edit_text"))(edit,command.c_str(),false);
            reinterpret_cast<void(__thiscall*)(void*)>(function("chat_submit"))(chat);
        } else {
            Require(element=="scoreboard" || element=="dialog","List or dialog required");
            const bool score=element=="scoreboard";BYTE* owner=object(score?"score":"dialog");
            BYTE visible=0;Read(owner+(score?(nativeLayout?0:12):0x28),&visible,1);Require(visible!=0,"Requested UI is not visible");
            if(event=="accept" || event=="cancel") {
                Require(!score,"Accept/cancel is limited to server dialogs");
                const WPARAM key=event=="accept"?VK_RETURN:VK_ESCAPE;
                if(original) {
                    if(key==VK_RETURN)reinterpret_cast<int(__cdecl*)(int)>(function("dialog_key"))(static_cast<int>(key));
                    else reinterpret_cast<LRESULT(__stdcall*)(HWND,UINT,WPARAM,LPARAM)>(function("window_message"))(nullptr,WM_CHAR,key,0);
                } else if(nativeLayout) {
                    // Native dialog routing handles Enter on key-up and Escape on WM_CHAR.
                    reinterpret_cast<LRESULT(__stdcall*)(HWND,UINT,WPARAM,LPARAM)>(function("window_message"))(nullptr,key==VK_RETURN?WM_KEYUP:WM_CHAR,key,0);
                } else reinterpret_cast<bool(__thiscall*)(void*,HWND,UINT,WPARAM,LPARAM)>(function("dialog_message"))(owner,nullptr,WM_KEYDOWN,key,0);
            } else {
                BYTE* list=nullptr;Read(owner+(nativeLayout?(score?0x38:0x20):8),&list,4);Require(list!=nullptr,"List unavailable");
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
    } else if(element=="editor") {
        Require(false,"Native editor helper requires a fixed event");
    } else if(element=="textdraw") {
        Require(!open,"Begin selection through the isolated server fixture");
        BYTE* selection=nullptr;
        if(original) selection=object("selection");
        else if(pin["functions"].contains("textdraw_pool"))
            selection=reinterpret_cast<BYTE*(__thiscall*)(void*)>(function("textdraw_pool"))(object("netgame"));
        else {
            Require(nativeLayout,"Missing pool accessor for legacy layout");
            function("netgame_ctor"); // Exact-build constructor bytes checked before fixed ABI dereferences.
            BYTE* pools=nullptr;Read(object("netgame")+0x3DE,&pools,4);Require(pools!=nullptr,"NetGame pools unavailable");
            Read(pools+0x20,&selection,4); // CNetGame/CNetGamePools original layout assertions.
        }
        Require(selection!=nullptr,"Textdraw selector not initialized");
        if(original)reinterpret_cast<void(__thiscall*)(void*)>(function("selection_cancel"))(selection);
        else reinterpret_cast<void(__thiscall*)(void*,bool)>(function("selection_cancel"))(selection,true);
    } else if(element=="netstats" || element=="hud_hidden") {
        if(open){
            unsigned duration=1000;
            if(p.contains("duration_ms")){Require(p["duration_ms"].is_number_unsigned() || p["duration_ms"].is_number_integer(),"duration_ms must be integer");const auto value=p["duration_ms"].get<long long>();Require(value>=1 && value<=5000,"duration_ms outside1..5000");duration=static_cast<unsigned>(value);}
            Require(game::input::StartSampKey(element=="netstats"?VK_F5:VK_F10,duration),"SA-MP key poll hook unavailable");
        } else game::input::ReleaseSampKey();
    } else if(element=="help") {
        BYTE* dialog=object("dialog");
        if(open) reinterpret_cast<void(__cdecl*)()>(function("help_open"))();
        else {
            int id=0;Read(dialog+(nativeLayout?0x30:0x2c),&id,4);
            BYTE visible=0;Read(dialog+0x28,&visible,1);
            if(visible){
                // Only dismiss local help: never close a server dialog with the same ID.
                DWORD send=0;if(nativeLayout)Read(dialog+0x81,&send,4);else Read(dialog+0x29,&send,1);
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
    return {{"dispatched",true},{"element",element},{"requested_open",open},{"sort_mode",sortResult},{"editor",editorResult},{"samp_sha256",sha},{"scope","fixed native UI method or bounded F5 polling; inspect screenshot for visible result; no OS input"}};
}
}
