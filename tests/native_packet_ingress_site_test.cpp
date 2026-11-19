#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <xmmintrin.h>
#include "MinHook.h"
#include "../mods/game_test_client/native_packet_ingress_probe.h"

namespace {
unsigned failures=0,owners=0,originals=0,logs=0;
void Check(bool ok,const char* why){if(!ok){++failures;std::printf("FAIL %s\n",why);}}
unsigned short Control(){
    unsigned short value;
    __asm { fnstcw value }
    return value;
}
void Control(unsigned short value){
    __asm { fldcw value }
}
using Fn=int(__cdecl*)(unsigned,unsigned,unsigned,void*,unsigned);
Fn entryOriginal=nullptr;
unsigned char packet[8]={0x80,0x08,0x11,0x22,0x33,0x44,0x9a,0x27};
int __cdecl Body(unsigned a,unsigned b,unsigned source,void* bytes,unsigned size){
    ++originals;
    Check(a==11&&b==22&&source==33&&bytes==packet&&size==8,"five native arguments survive both hooks");
    Check(GetLastError()==42,"site observer restores entry LastError");
    Check(Control()==0x077f&&_mm_getcsr()==0x3f80,"site observer restores native floating point state");
    SetLastError(97);return 0x12345678;
}
__declspec(naked) int __cdecl Native(unsigned,unsigned,unsigned,void*,unsigned){
    __asm {push ebp}
    __asm {mov ebp,esp}
    __asm {sub esp,10h}
    __asm {nop} __asm {nop} __asm {nop} __asm {nop} __asm {nop}
    __asm {nop} __asm {nop} __asm {nop} __asm {nop} __asm {nop}
    // Offset 16 is a separate observer site beyond the entry detour span.
    __asm {nop} __asm {nop} __asm {nop} __asm {nop} __asm {nop}
    __asm {push dword ptr [ebp+18h]}
    __asm {push dword ptr [ebp+14h]}
    __asm {push dword ptr [ebp+10h]}
    __asm {push dword ptr [ebp+0ch]}
    __asm {push dword ptr [ebp+8]}
    __asm {call Body}
    __asm {add esp,14h}
    __asm {mov esp,ebp}
    __asm {pop ebp}
    __asm {ret}
}
int __cdecl Owner(unsigned a,unsigned b,unsigned source,void* bytes,unsigned size){
    ++owners;return entryOriginal(a,b,source,bytes,size);
}
void Log(SdgoMod*,int,const char*,...){++logs;SetLastError(555);Control(0x0b7f);_mm_setcsr(0x5f80);}
int Site(SdgoMod*,const char* name,void* hook,void** original){
    Check(!std::strcmp(name,"test.native_packet_ingress_site"),"post-prologue symbol selected");
    auto* at=reinterpret_cast<unsigned char*>(&Native)+16;
    Check(at[0]==0x90&&at[4]==0x90,"fixture observer span is independent from the entry patch");
    return MH_CreateHook(at,hook,original)==MH_OK;
}
}
int main(){
    Check(MH_Initialize()==MH_OK,"real MinHook starts");
    Check(MH_CreateHook(reinterpret_cast<void*>(&Native),reinterpret_cast<void*>(&Owner),reinterpret_cast<void**>(&entryOriginal))==MH_OK,"another Mod owns the entry");
    SdgoApi api{};api.size=sizeof(api);api.HookSite=&Site;api.Log=&Log;
    NativePacketIngressProbe observer;
    Check(observer.InstallAtSite(&api,reinterpret_cast<SdgoMod*>(1)),"passive observer coexists with existing entry owner");
    Check(MH_EnableHook(MH_ALL_HOOKS)==MH_OK,"both real detours activate");
    SetLastError(42);Control(0x077f);_mm_setcsr(0x3f80);
    const int result=Native(11,22,33,packet,8);
    Check(result==0x12345678&&GetLastError()==97,"original return and exit LastError survive");
    Check(owners==1&&originals==1&&logs==1,"entry owner, observer and original each execute once");
    Check(packet[0]==0x80&&packet[6]==0x9a,"observation never rewrites the packet");
    MH_DisableHook(MH_ALL_HOOKS);MH_Uninitialize();
    std::printf("NATIVE INGRESS ENTRY/SITE COEXISTENCE: %u failures\n",failures);return failures?1:0;
}
