#pragma once
#include "samp_ui.hpp"
namespace samp_ui {
template<class T> inline T Value(const BYTE* at) {T v{};samp_headmove::Read(at,&v,sizeof(v));return v;}
inline std::string Text(const BYTE* at,size_t count) {
    // Preserve arbitrary native ACP bytes without manufacturing UTF-8 text.
    char raw[258]{};samp_headmove::Require(count<=257,"Text bound exceeded");
    samp_headmove::Read(at,raw,count);size_t length=0;while(length<count && raw[length])++length;
    wchar_t wide[258]{};int n=MultiByteToWideChar(CP_ACP,0,raw,static_cast<int>(length),wide,258);
    char utf8[1032]{};int bytes=n?WideCharToMultiByte(CP_UTF8,0,wide,n,utf8,1032,nullptr,nullptr):0;
    return std::string(utf8,bytes);
}
inline json Vector3(const BYTE* at) {return {Value<float>(at),Value<float>(at+4),Value<float>(at+8)};}
inline json PedSnapshot(BYTE* ped) {
    if(!ped)return nullptr;
    // DL-R1 AC9E0/ABA60: native ped, vehicle bit and jump task. No task methods.
    BYTE* native=Value<BYTE*>(ped+0x2a4);
    json row={{"wrapper",reinterpret_cast<DWORD>(ped)},{"native",reinterpret_cast<DWORD>(native)},{"attachments",json::array()}};
    if(native){
        const bool vehicle=(Value<DWORD>(native+0x46c)&0x100)!=0;
        BYTE* tasks=Value<BYTE*>(native+0x47c);BYTE* jump=tasks?Value<BYTE*>(tasks+0x10):nullptr;
        row.update({{"action",Value<BYTE>(native+0x530)},{"in_vehicle",vehicle},{"jetpack_task",reinterpret_cast<DWORD>(jump)},
            {"jetpack",!vehicle && jump && Value<DWORD>(jump)==0x8705c4}});
    }
    // B0710 copies 13 DWORDs to +74+slot*34; AEA50 owns +4C/+27C.
    for(unsigned slot=0;slot<10;++slot)if(Value<DWORD>(ped+0x4c+slot*4)==1){
        BYTE* data=ped+0x74+slot*0x34;
        row["attachments"].push_back({{"slot",slot},{"model",Value<int>(data)},{"bone",Value<int>(data+4)},
            {"offset",Vector3(data+8)},{"rotation",Vector3(data+0x14)},{"scale",Vector3(data+0x20)},
            {"color1",Value<DWORD>(data+0x2c)},{"color2",Value<DWORD>(data+0x30)},
            {"object",Value<DWORD>(ped+0x27c+slot*4)}});
    }
    return row;
}
inline json Get(const json& params) {
    using samp_headmove::Require;using samp_headmove::Read;
    Require(params.empty(),"get_samp_ui takes no parameters");
    HMODULE module=GetModuleHandleW(L"samp.dll");Require(module!=nullptr,"SA-MP not loaded");
    // Commands execute on the game thread; the reviewed DLL is immutable for
    // this process lifetime. Hash and parse the allowlist once per module instead
    // of doing file I/O and parsing every sample. PE identity is still checked below.
    static HMODULE cachedModule=nullptr;
    static std::string cachedSha;
    static json cachedPin;
    if(cachedModule!=module || cachedPin.is_null()){
        const auto sha=samp_headmove::Hash(module);json selected;
        for(const auto& candidate:json::parse(SampUiPins))if(candidate["sha256"]==sha){selected=candidate;break;}
        Require(!selected.is_null() && selected.contains("telemetry") && selected["telemetry"].is_object(),"Native telemetry ABI not reviewed for this SHA; regenerate pins");
        cachedSha=sha;cachedPin=std::move(selected);cachedModule=module;
    }
    const auto& sha=cachedSha;const auto& pin=cachedPin;
    auto base=reinterpret_cast<BYTE*>(module);auto dos=Value<IMAGE_DOS_HEADER>(base);
    Require(dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096,"Invalid loaded DOS header");
    auto pe=Value<IMAGE_NT_HEADERS32>(base+dos.e_lfanew);
    Require(pe.Signature==IMAGE_NT_SIGNATURE && pe.FileHeader.Machine==IMAGE_FILE_MACHINE_I386 && pe.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC && pe.FileHeader.TimeDateStamp==pin["timestamp"].get<DWORD>() && pe.OptionalHeader.SizeOfImage==pin["size"].get<DWORD>(),"Loaded PE differs from pin");
    auto object=[&](const char* name){return Value<BYTE*>(base+pin["globals"].at(name).get<DWORD>());};
    auto pointers=[&](BYTE* owner,std::initializer_list<std::pair<const char*,unsigned>> fields){
        json row={{"owner",reinterpret_cast<DWORD>(owner)}};
        if(owner)for(auto f:fields)row[f.first]=Value<DWORD>(owner+f.second);
        return row;
    };
    BYTE* score=object("score");json scoreboard=pointers(score,{{"device",0x30},{"dialog",0x34},{"list",0x38}});
    scoreboard["rows"]=json::array();
    if(score){
        scoreboard["visible"]=Value<DWORD>(score)!=0;scoreboard["sort_mode"]=Value<int>(score+0x40);
        BYTE* list=Value<BYTE*>(score+0x38);
        if(list){
            auto t=pin["telemetry"];int count=Value<int>(list+t["count"].get<unsigned>());
            Require(count>=0 && count<=1004,"Native scoreboard row count outside bounds");
            BYTE* array=Value<BYTE*>(list+t["array"].get<unsigned>());
            Require(!count || array,"Native scoreboard rows absent");
            BYTE* scroll=list+t["scroll"].get<unsigned>()+t["position"].get<unsigned>();
            scoreboard.update({{"row_count",count},{"selected_index",Value<int>(list+t["selected"].get<unsigned>())},{"scroll_position",Value<int>(scroll)},{"page_size",Value<int>(scroll+4)},{"scroll_start",Value<int>(scroll+8)},{"scroll_end",Value<int>(scroll+12)}});
            for(int i=0;i<count;++i){
                BYTE* item=Value<BYTE*>(array+i*4);Require(item!=nullptr,"Native scoreboard item absent");
                RECT rect=Value<RECT>(item+0x288);
                scoreboard["rows"].push_back({{"row",i},{"id_text",Text(item,257)},{"name",Text(item+0x101,129)},{"score_text",Text(item+0x182,129)},{"ping_text",Text(item+0x203,129)},{"data_id",Value<int>(item+0x284)},{"color",Value<DWORD>(item+0x299)},{"selected",Value<BYTE>(item+0x298)!=0},{"rect",{rect.left,rect.top,rect.right,rect.bottom}}});
            }
        }
    }
    json vehicles=json::array();BYTE* net=object("netgame");BYTE* pools=net?Value<BYTE*>(net+0x3de):nullptr;BYTE* pool=pools?Value<BYTE*>(pools+0xc):nullptr;
    if(pool){
      std::array<DWORD,2000> active{};Read(pool+0x3074,active.data(),sizeof(active));
      for(int id=0;id<2000;++id)if(active[id]){
        BYTE* wrapper=Value<BYTE*>(pool+0x1134+id*4);if(!wrapper)continue;
        BYTE* native=Value<BYTE*>(wrapper+0x4c);
        json row={{"id",id},{"wrapper",reinterpret_cast<DWORD>(wrapper)},{"native",reinterpret_cast<DWORD>(native)},{"plate_text",Text(wrapper+0x93,33)},{"plate_texture",Value<DWORD>(wrapper+0x8f)}};
        if(native){
            // Raw automobile damage layout; model is explicit so callers can exclude bikes/trains.
            row.update({{"model",Value<WORD>(native+0x22)},{"health",Value<float>(native+0x4c0)},{"panels",Value<DWORD>(native+0x5b4)},{"doors",Value<DWORD>(native+0x5a9)},{"lights",Value<BYTE>(native+0x5b0)},{"wheel_bytes",{Value<BYTE>(native+0x5a5),Value<BYTE>(native+0x5a6),Value<BYTE>(native+0x5a7),Value<BYTE>(native+0x5a8)}}});
        }
        vehicles.push_back(row);
      }
    }
    // Original 06D1F0 releases +0C/+08/+04/+20; 06D240 writes
    // display mode at +10, texture at +08, surface at +0C and renderer at +04.
    // Rebuilt CLicensePlate stores its display mode directly after the device.
    const bool original=pin["original"].get<bool>();
    json resources={{"score",scoreboard.is_null()?json():pointers(score,{{"device",0x30},{"dialog",0x34},{"list",0x38}})},
        {"chat",pointers(object("chat_window"),{{"text_sprite",0x63a6},{"texture_sprite",0x63aa},{"device",0x63ae},{"render_to_surface",0x63b6},{"texture",0x63ba},{"surface",0x63be}})},
        {"death",pointers(object("death"),{{"font",0x137},{"weapon_font",0x13b},{"box_font",0x13f},{"sprite",0x143},{"device",0x147},{"aux_font",0x14f},{"aux_box_font",0x153}})},
        {"plate_renderer",pointers(object("plate_renderer"),{{"device",0},{"texture",original?0x08u:0x14u},{"surface",original?0x0cu:0x18u},{"render_to_surface",original?0x04u:0x1cu},{"default_texture",0x20}})}};
    json progress,artwork,players;
    if(pin["telemetry"].value("snapshot_abi",0)==1){
        BYTE* window=object("model_progress");
        if(window){
            progress={{"owner",reinterpret_cast<DWORD>(window)},{"visible",Value<DWORD>(window+0x30)!=0},
                {"completed",Value<int>(window+0x24c)},{"total",Value<int>(window+0x250)},
                {"rate_kib_per_second",Value<float>(window+0x25c)},{"entries",json::array()},{"list_rows",json::array()}};
            const unsigned count=Value<unsigned>(window+4);Require(count<=40000,"Native download extent outside bounds");
            BYTE* entries=Value<BYTE*>(window);Require(!count || entries,"Native download entries absent");
            BYTE* list=Value<BYTE*>(window+0x2c);int rows=0;BYTE* array=nullptr;
            if(list){
                rows=Value<int>(list+pin["telemetry"]["count"].get<unsigned>());
                Require(rows>=0 && rows<=40000,"Native download list outside bounds");
                array=Value<BYTE*>(list+pin["telemetry"]["array"].get<unsigned>());
                Require(!rows || array,"Native download list rows absent");
                for(int i=0;i<rows;++i){
                    BYTE* item=Value<BYTE*>(array+i*4);Require(item!=nullptr,"Native download list item absent");
                    progress["list_rows"].push_back({{"row",i},{"type_text",Text(item,257)},
                        {"id_text",Text(item+0x101,129)},{"status_text",Text(item+0x182,129)}});
                }
            }
            unsigned completed=0,errors=0;
            for(unsigned i=0;i<count;++i){
                BYTE* e=Value<BYTE*>(entries+i*4);if(!e)continue;
                int index=Value<int>(e);BYTE state=Value<BYTE>(e+9);
                json entry={{"slot",i},{"index",index},{"type",Value<BYTE>(e+4)},{"crc",Value<DWORD>(e+5)},
                    {"state",state},{"received",Value<int>(e+0xa)},{"total",Value<int>(e+0xe)},
                    {"status_text",index>=0 && index<rows?progress["list_rows"][index]["status_text"]:json()}};
                progress["entries"].push_back(entry);if(state==4)++completed;if(state==3)++errors;
            }
            progress["completed_entries"]=completed;progress["error_entries"]=errors;
        }
        BYTE* manager=pin["telemetry"]["manager_indirect"].get<bool>()?object("artwork_manager"):
            base+pin["globals"].at("artwork_manager").get<DWORD>();
        if(manager)artwork={{"owner",reinterpret_cast<DWORD>(manager)},{"download_state",Value<int>(manager+0x213)},
            {"expected",Value<DWORD>(manager+0x21b)},{"list_ready",Value<BYTE>(manager+0x21f)!=0},
            {"finished",Value<BYTE>(manager+0x220)!=0},{"acknowledged",Value<BYTE>(manager+0x221)!=0}};
        BYTE* playerPool=pools?Value<BYTE*>(pools+8):nullptr;
        if(playerPool){
            players={{"local",nullptr},{"remote",json::array()}};
            BYTE* local=Value<BYTE*>(playerPool+0x1e);
            if(local)players["local"]={{"id",Value<WORD>(playerPool)},{"animation",Value<DWORD>(local+0x100)},
                {"animation_sent",Value<DWORD>(local+0x104)!=0},{"ped",PedSnapshot(Value<BYTE*>(local))}};
            std::array<DWORD,1004> active{};Read(playerPool+0xfd6,active.data(),sizeof(active));
            for(unsigned id=0;id<1004;++id)if(active[id]){
                BYTE* info=Value<BYTE*>(playerPool+0x26+id*4);BYTE* remote=info?Value<BYTE*>(info+8):nullptr;if(!remote)continue;
                float health=Value<float>(remote+0x1b0),armour=Value<float>(remote+0x1ac);
                // These are mathematical inputs, not claims that a tag was rendered.
                const float cappedHealth=health>100.0f?100.0f:health,cappedArmour=armour>100.0f?100.0f:armour;
                players["remote"].push_back({{"id",id},{"reported_health",health},{"reported_armour",armour},
                    {"show_nametag",Value<DWORD>(remote+0x10)!=0},{"special_action",Value<BYTE>(remote+0x18)},
                    {"jetpack_latch",Value<DWORD>(remote+0x14)!=0},{"state",Value<BYTE>(remote+0x1a)},
                    {"animation",Value<DWORD>(remote+0x1c0)},{"animation_from_rpc",Value<DWORD>(remote+0x1cd)!=0},
                    {"ped",PedSnapshot(Value<BYTE*>(remote+4))},
                    {"bar_geometry_input",{{"health_width",double(cappedHealth)*double(0.38461539149284363f)},
                        {"armour_width",double(cappedArmour)*double(0.38461539149284363f)},
                        {"health_nonnegative",health>=0.0f},{"armour_positive",armour>0.0f},{"derived",true}}}});
            }
        }
    }
    return {{"samp_sha256",sha},{"tick",GetTickCount()},{"scoreboard",scoreboard},{"vehicle_pool",reinterpret_cast<DWORD>(pool)},{"vehicles",vehicles},{"resources",resources},{"download_progress",progress},{"artwork",artwork},{"players",players},{"scope","Read-only game-thread snapshot of actual native list order and opaque resource ownership. Automobile damage bytes only; no COM calls, writes or native refresh. Download states, player flags and attachment local transforms are raw native storage; bar widths are derived inputs and do not prove visibility or pixels. Snapshots cannot certify transient lost/reset ordering."}};
}
}
