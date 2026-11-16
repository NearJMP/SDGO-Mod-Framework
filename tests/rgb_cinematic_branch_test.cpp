#include "../mods/unit_appearance_client/rgb/sync.cpp"
#include <MinHook.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace sync=sdgo::rgb_paint::sync;
namespace rgb=sdgo::rgb_paint;
namespace {
unsigned checks=0,styleCalls=0;
std::array<std::uint8_t,0xab8> actor{};
std::array<std::uint8_t,24> colors{};
unsigned model=0;
void Check(bool value,const char* message){
    ++checks;if(!value){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
void __cdecl NativeStyle(void* object,const void* palette,unsigned,const void* decals){
    Check(object==&model&&palette==colors.data()&&decals==colors.data()+20,
          "native style receives its unchanged cdecl arguments");
    ++styleCalls;
    sync::models.erase(object); // The existing style-entry observer retires prior identity.
    SetLastError(0x1234);
}
unsigned SymbolRva(const char* path){
    std::ifstream input(path);std::string line;bool selected=false;
    while(std::getline(input,line)){
        if(!line.empty()&&line.front()=='[')selected=line.find("[rgb_paint.style_return_25]")==0;
        if(selected&&line.rfind("rva=",0)==0)return static_cast<unsigned>(std::stoul(line.substr(4),nullptr,0));
    }
    return 0;
}
struct Fixture {
    std::uint8_t* code=nullptr;
    unsigned branch=0,cleanup=0,join=0;
    using Run=void*(__cdecl*)(void*,const void*);
    Fixture(){
        code=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
        Check(code!=nullptr,"owned x86 fixture allocated");
        std::vector<std::uint8_t> bytes={
            0x55,0x8b,0xec,0x83,0xec,0x10,0x53,0x56,0x57,
            0x8b,0x75,0x08, // actor in ESI
            0x8b,0x4d,0x0c,0x89,0x4d,0xf0, // palette in frame -0x10
            0x8d,0x79,0xac, // native source record in EDI (palette -0x54)
            0x8d,0x59,0x14, // decals in EBX
            0x8b,0x86,0xb4,0x0a,0,0,0x85,0xc0};
        branch=static_cast<unsigned>(bytes.size());
        bytes.insert(bytes.end(),{0x74,0,0x53,0x6a,0,0x51,0x50,0xe8});
        const auto callEnd=reinterpret_cast<std::uintptr_t>(code)+bytes.size()+4;
        const auto displacement=static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&NativeStyle)-callEnd);
        const auto* raw=reinterpret_cast<const std::uint8_t*>(&displacement);
        bytes.insert(bytes.end(),raw,raw+4);
        cleanup=static_cast<unsigned>(bytes.size());
        bytes.insert(bytes.end(),{0x83,0xc4,0x10});
        join=static_cast<unsigned>(bytes.size());
        bytes.insert(bytes.end(),{0x8b,0xc6,0x8b,0x4d,0xf4,0x5f,0x5e,0x5b,0x8b,0xe5,0x5d,0xc3});
        bytes[branch+1]=static_cast<std::uint8_t>(join-branch-2);
        std::memcpy(code,bytes.data(),bytes.size());
        FlushInstructionCache(GetCurrentProcess(),code,bytes.size());
    }
    ~Fixture(){VirtualFree(code,0,MEM_RELEASE);}
    Run run()const{return reinterpret_cast<Run>(code);}
};
void CheckLegacyHazard(){
    Fixture old;void* trampoline=nullptr;
    Check(MH_CreateHook(old.code+old.cleanup,reinterpret_cast<void*>(&sync::StyleReturn25),&trampoline)==MH_OK,
          "real MinHook accepts the legacy cleanup site");
    Check(MH_EnableHook(old.code+old.cleanup)==MH_OK,"legacy site patched");
    const auto* incoming=old.code+old.branch+2+old.code[old.branch+1];
    Check(old.code[old.cleanup]==0xe9&&incoming==old.code+old.cleanup+3,
          "null-model branch enters the middle of the legacy five-byte JMP");
    Check(MH_RemoveHook(old.code+old.cleanup)==MH_OK,"legacy fixture removed before execution");
}
}
int main(int argc,char** argv){
    Check(argc==2,"production symbol fragment supplied");
    Check(SymbolRva(argv[1])==0x103b16,"production hook is the branch join, not its preceding cleanup");
    Check(MH_Initialize()==MH_OK,"real MinHook initialized");
    CheckLegacyHazard();
    Fixture fixed;
    Check(MH_CreateHook(fixed.code+fixed.join,reinterpret_cast<void*>(&sync::StyleReturn25),&sync::returnSites[25])==MH_OK,
          "real production observer hooked at the complete branch join");
    Check(MH_EnableHook(fixed.code+fixed.join)==MH_OK,"candidate site patched");
    Check(fixed.code+fixed.branch+2+fixed.code[fixed.branch+1]==fixed.code+fixed.join,
          "native null-model branch now enters the JMP at its first byte");
    rgb::Record record;record.type=27115;record.state.identity={7,700};
    sync::battleStyles[reinterpret_cast<std::uintptr_t>(colors.data())]=record;
    SetLastError(0x5678);
    Check(fixed.run()(actor.data(),colors.data())==actor.data(),"null alternate model returns its native actor");
    Check(styleCalls==0&&sync::models.empty(),"null branch neither styles nor binds an absent model");
    Check(GetLastError()==0x5678,"null observer preserves native last error");
    void* pointer=&model;std::memcpy(actor.data()+0xab4,&pointer,4);
    Check(fixed.run()(actor.data(),colors.data())==actor.data(),"styled alternate model returns its native actor");
    Check(styleCalls==1,"original style call executes exactly once");
    const auto found=sync::models.find(&model);
    Check(found!=sync::models.end()&&found->second.record.state.identity==record.state.identity&&found->second.battle,
          "post-style observer recovers palette identity from verified callee-saved state");
    Check(GetLastError()==0x1234,"post-style observer preserves native last error");
    sync::models.clear();sync::battleStyles.clear();
    fixed.run()(actor.data(),colors.data());
    Check(styleCalls==2&&sync::models.empty(),"unknown identity retains original styling without inventing an owner");
    Check(MH_RemoveHook(fixed.code+fixed.join)==MH_OK&&MH_Uninitialize()==MH_OK,"owned hooks released");
    std::cout<<checks<<" cinematic branch and real-observer checks passed\n";
}
