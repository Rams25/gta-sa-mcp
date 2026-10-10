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
inline json Get(const json& params) {
    using samp_headmove::Require;using samp_headmove::Read;
    Require(params.empty(),"get_samp_ui takes no parameters");
    HMODULE module=GetModuleHandleW(L"samp.dll");Require(module!=nullptr,"SA-MP not loaded");
    const auto sha=samp_headmove::Hash(module);json pin;
    for(const auto& candidate:json::parse(SampUiPins))if(candidate["sha256"]==sha){pin=candidate;break;}
    Require(!pin.is_null() && pin.contains("telemetry") && pin["telemetry"].is_object(),"Native telemetry ABI not reviewed for this SHA; regenerate pins");
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
    if(pool)for(int id=0;id<2000;++id)if(Value<DWORD>(pool+0x3074+id*4)){
        BYTE* wrapper=Value<BYTE*>(pool+0x1134+id*4);if(!wrapper)continue;
        BYTE* native=Value<BYTE*>(wrapper+0x4c);
        json row={{"id",id},{"wrapper",reinterpret_cast<DWORD>(wrapper)},{"native",reinterpret_cast<DWORD>(native)},{"plate_text",Text(wrapper+0x93,33)},{"plate_texture",Value<DWORD>(wrapper+0x8f)}};
        if(native){
            // Raw automobile damage layout; model is explicit so callers can exclude bikes/trains.
            row.update({{"model",Value<WORD>(native+0x22)},{"health",Value<float>(native+0x4c0)},{"panels",Value<DWORD>(native+0x5b4)},{"doors",Value<DWORD>(native+0x5a9)},{"lights",Value<BYTE>(native+0x5b0)},{"wheel_bytes",{Value<BYTE>(native+0x5a5),Value<BYTE>(native+0x5a6),Value<BYTE>(native+0x5a7),Value<BYTE>(native+0x5a8)}}});
        }
        vehicles.push_back(row);
    }
    // Original 06D1F0 releases +0C/+08/+04/+20; 06D240 writes
    // display mode at +10, texture at +08, surface at +0C and renderer at +04.
    // Rebuilt CLicensePlate stores its display mode directly after the device.
    const bool original=pin["original"].get<bool>();
    json resources={{"score",scoreboard.is_null()?json():pointers(score,{{"device",0x30},{"dialog",0x34},{"list",0x38}})},
        {"chat",pointers(object("chat_window"),{{"text_sprite",0x63a6},{"texture_sprite",0x63aa},{"device",0x63ae},{"render_to_surface",0x63b6},{"texture",0x63ba},{"surface",0x63be}})},
        {"death",pointers(object("death"),{{"font",0x137},{"weapon_font",0x13b},{"box_font",0x13f},{"sprite",0x143},{"device",0x147},{"aux_font",0x14f},{"aux_box_font",0x153}})},
        {"plate_renderer",pointers(object("plate_renderer"),{{"device",0},{"texture",original?0x08u:0x14u},{"surface",original?0x0cu:0x18u},{"render_to_surface",original?0x04u:0x1cu},{"default_texture",0x20}})}};
    return {{"samp_sha256",sha},{"tick",GetTickCount()},{"scoreboard",scoreboard},{"vehicle_pool",reinterpret_cast<DWORD>(pool)},{"vehicles",vehicles},{"resources",resources},{"scope","Read-only game-thread snapshot of actual native list order and opaque resource ownership. Automobile damage bytes only; no COM calls, writes or native refresh. Snapshots cannot certify transient lost/reset ordering or pixels."}};
}
}
