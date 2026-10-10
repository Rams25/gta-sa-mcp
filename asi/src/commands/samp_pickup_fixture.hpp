#pragma once
// Exact-build dormant creator fixture; deliberately not a normally reachable RPC path.
#include "samp_headmove.hpp"
#include "samp_pickup_fixture_pins.hpp"
#include "../core/config.hpp"
#include "../render/window.hpp"
namespace samp_pickup_fixture {
template<class T> inline T ReadAt(const BYTE* p){T v{};samp_headmove::Read(p,&v,sizeof(v));return v;}
inline json Invoke(const json& args){
 using samp_headmove::Require;
 const std::string action=args.value("action","");Require(action=="read"||action=="seed","Only read/seed allowed");
 Require(GetConfig().windowed&&GetConfig().noActivate&&game::InGame(),"Requires protected in-game fixture process");
 Require(GetConfig().pipe=="gta-sa-mcp-original-trial"||GetConfig().pipe=="gta-sa-mcp-rebuilt-trial","Requires isolated trial pipe");
 const auto module=GetModuleHandleW(L"samp.dll");Require(module!=nullptr,"SA-MP missing");
 const auto sha=samp_headmove::Hash(module);json pin;
 for(const auto& candidate:json::parse(SampPickupFixturePins))if(candidate["sha256"]==sha){pin=candidate;break;}
 Require(!pin.is_null(),"Only pinned original/N/O supported");
 BYTE* base=reinterpret_cast<BYTE*>(module);const auto dos=ReadAt<IMAGE_DOS_HEADER>(base);
 Require(dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>0&&dos.e_lfanew<4096,"Invalid DOS header");
 const auto pe=ReadAt<IMAGE_NT_HEADERS32>(base+dos.e_lfanew);
 Require(pe.Signature==IMAGE_NT_SIGNATURE&&pe.FileHeader.Machine==IMAGE_FILE_MACHINE_I386&&pe.FileHeader.TimeDateStamp==pin["timestamp"].get<DWORD>()&&pe.OptionalHeader.SizeOfImage==pin["size"].get<DWORD>(),"Loaded PE differs");
 const DWORD creator=pin["rva"];for(unsigned i=0;i<pin["bytes"].size();++i)if(pin["mask"][i].get<int>())Require(ReadAt<BYTE>(base+creator+i)==pin["bytes"][i].get<BYTE>(),"Loaded creator differs from original bytes");
 const bool original=pin["original"];
 BYTE* net=ReadAt<BYTE*>(base+pin["net_rva"].get<DWORD>());Require(net!=nullptr,"NetGame missing");
 BYTE* pools=original?ReadAt<BYTE*>(net+0x3de):nullptr;
 BYTE* pool=original?(Require(pools!=nullptr,"Pools missing"),ReadAt<BYTE*>(pools+0x10)):ReadAt<BYTE*>(net+0x14);
 Require(pool!=nullptr,"Pickup pool missing");
 const unsigned countOffset=original?0:0x14000,handleOffset=original?4:0x14004,timerOffset=original?0x8004:0x18004,dropOffset=original?0xc004:0x1c004,dropStride=original?3:4,ownerOffset=original?1:2,pickupOffset=original?0xf004:0;
 auto count=[&](){const int n=ReadAt<int>(pool+countOffset);Require(n>=0&&n<=4096,"Invalid pickup count");return n;};
 auto inspect=[&](){json rows=json::array();for(unsigned i=0;i<4096;++i){const DWORD h=ReadAt<DWORD>(pool+handleOffset+i*4);if(!h)continue;const auto dropped=ReadAt<BYTE>(pool+dropOffset+i*dropStride);if(!dropped)continue;const WORD owner=ReadAt<WORD>(pool+dropOffset+i*dropStride+ownerOffset);if(owner>1)continue;BYTE* info=pool+pickupOffset+i*20;rows.push_back({{"slot",i},{"handle",h},{"timer",ReadAt<int>(pool+timerOffset+i*4)},{"dropped",dropped},{"owner",owner},{"model",ReadAt<int>(info)},{"type",ReadAt<int>(info+4)},{"position",{ReadAt<float>(info+8),ReadAt<float>(info+12),ReadAt<float>(info+16)}}});}return rows;};
 const int beforeCount=count();json before=inspect();
 if(action=="seed"){
 Require(args.contains("owner")&&args["owner"].is_number_integer(),"owner0/1 required");const int owner=args["owner"];Require(owner==0||owner==1,"owner must0/1");
 Require(beforeCount<4096,"Pool full");for(const auto& row:before)Require(row["owner"].get<int>()!=owner,"Existing dropped pickup for owner; remove via RPC151 first");
 window::ProtectDesktop();
 using Create=void(__thiscall*)(void*,int,float,float,float,DWORD,WORD);
 reinterpret_cast<Create>(base+creator)(pool,346,1640.f,-2500.f+owner*4.f,13.6f,7,static_cast<WORD>(owner));
 }
 const int afterCount=count();json after=inspect();
 return {{"samp_sha256",sha},{"action",action},{"creator_rva",creator},{"before_count",beforeCount},{"after_count",afterCount},{"before",before},{"after",after},{"scope","tool-seeded dormant dropped-pickup creator; fixed model346 ammo7 coordinates and owner0/1; no ordinary network-creation claim; remove with actual server RPC151"}};
}
}
