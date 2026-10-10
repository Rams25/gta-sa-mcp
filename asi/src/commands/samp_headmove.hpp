#pragma once
// Test-only fixed registered-command dispatch. No supplied code address or OS input.
#include <Windows.h>
#include <wincrypt.h>
#include <array>
#include <cstring>
#pragma comment(lib,"advapi32.lib")
namespace samp_headmove {
struct Handles {
    HANDLE file=INVALID_HANDLE_VALUE; HCRYPTPROV provider=0; HCRYPTHASH hash=0;
    ~Handles(){if(hash)CryptDestroyHash(hash);if(provider)CryptReleaseContext(provider,0);if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
};
inline void Require(bool yes,const char* message){if(!yes)throw CommandError("samp_build_mismatch",message);}
inline void Read(const void* source,void* dest,size_t size){SIZE_T got=0;Require(ReadProcessMemory(GetCurrentProcess(),source,dest,size,&got)&&got==size,"Unreadable SA-MP address");}
inline std::string Hash(HMODULE module){
    wchar_t path[32768];DWORD n=GetModuleFileNameW(module,path,32768);
    Require(n&&n<32768,"Cannot resolve loaded SA-MP file");Handles h;
    h.file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    Require(h.file!=INVALID_HANDLE_VALUE,"Cannot open loaded SA-MP file");
    Require(CryptAcquireContextW(&h.provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)!=0,"SHA256 provider unavailable");
    Require(CryptCreateHash(h.provider,CALG_SHA_256,0,0,&h.hash)!=0,"SHA256 unavailable");
    std::array<BYTE,65536> buffer;DWORD count;
    for(;;){Require(ReadFile(h.file,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr)!=0,"Cannot read SA-MP file");if(!count)break;Require(CryptHashData(h.hash,buffer.data(),count,0)!=0,"Hash failed");}
    BYTE digest[32];DWORD bytes=32;Require(CryptGetHashParam(h.hash,HP_HASHVAL,digest,&bytes,0)&&bytes==32,"Hash failed");
    static const char hex[]="0123456789abcdef";std::string result;for(BYTE b:digest){result+=hex[b>>4];result+=hex[b&15];}return result;
}
inline json Invoke(const json& params){
    Require(params.empty(),"This fixed command takes no parameters");
    HMODULE module=GetModuleHandleW(L"samp.dll");Require(module!=nullptr,"SA-MP not loaded");
    const auto sha=Hash(module);const bool original=sha=="bccdb297464bd382625635be25585df07a8fa6668bc0015650708e3eb4ffcd4b";
    Require(original||sha=="bc6a8af90d8a59856f7a56e1724969e4863e99389cb5dcc39c4d37431fe21c1e","Unsupported SA-MP SHA256");
    auto base=reinterpret_cast<BYTE*>(module);IMAGE_DOS_HEADER dos;Read(base,&dos,sizeof(dos));
    Require(dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>0&&dos.e_lfanew<4096,"Invalid DOS header");
    IMAGE_NT_HEADERS32 pe;Read(base+dos.e_lfanew,&pe,sizeof(pe));
    Require(pe.Signature==IMAGE_NT_SIGNATURE&&pe.FileHeader.Machine==IMAGE_FILE_MACHINE_I386&&pe.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC&&
        pe.FileHeader.TimeDateStamp==(original?0x5a6a3130u:0x6ac97eeau)&&pe.OptionalHeader.SizeOfImage==(original?0x2be000u:0x412000u),"Loaded PE differs from pin");
    const DWORD lookup=original?0x69150:0xc01d0,handler=original?0x683a0:0x6e510;
    const BYTE originalLookup[]={0x53,0x55,0x56,0x57,0x8b,0xf9,0x8b,0x87};
    const BYTE rebuiltLookup[]={0x55,0x8b,0xec,0x53,0x8b,0xd9,0x56,0x33};
    BYTE actual[8];Read(base+lookup,actual,8);Require(!memcmp(actual,original?originalLookup:rebuiltLookup,8),"Lookup prologue differs");
    BYTE expected[8]={};const DWORD game=reinterpret_cast<DWORD>(base)+(original?0x2aca3c:0x18a300);
    if(original){expected[0]=0xa1;memcpy(expected+1,&game,4);expected[5]=0x85;expected[6]=0xc0;expected[7]=0x74;}
    else{expected[0]=0x83;expected[1]=0x3d;memcpy(expected+2,&game,4);expected[6]=0;expected[7]=0x74;}
    Read(base+handler,actual,8);Require(!memcmp(actual,expected,8),"Handler prologue differs");
    void* window=nullptr;Read(base+(original?0x2aca14:0x18a310),&window,4);Require(window!=nullptr,"Command window not initialized");
    using Command=void(__cdecl*)(char*);using Lookup=Command(__thiscall*)(void*,char*);
    char name[]="headmove";Command command=reinterpret_cast<Lookup>(base+lookup)(window,name);
    Require(command!=nullptr,"Headmove is not registered");auto target=reinterpret_cast<BYTE*>(command);
    // MSVC incremental-link registrations may point to one E9 thunk.
    BYTE opcode;Read(target,&opcode,1);if(opcode==0xe9){LONG displacement;Read(target+1,&displacement,4);target+=5+displacement;}
    Require(target==base+handler,"Registered headmove target differs");char empty[]="";command(empty);
    return {{"invoked",true},{"command","/headmove"},{"samp_sha256",sha},{"scope","registered handler on game thread; no text-entry/recall simulation or desktop input"}};
}
}
