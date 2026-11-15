// Opt-in read-only scene sampling alongside an unchanged, sealed gameplay suite.
#include "native_appearance_pointer_driver.h"
std::vector<std::uint64_t> appearanceSceneUnits;
std::wstring appearanceScenePath;
HHOOK appearanceSceneHook=nullptr;
UINT_PTR appearanceSceneTimer=0;
void CALLBACK AppearanceSceneTick(HWND,UINT,UINT_PTR,DWORD){
    AppearancePointerTick();
    static std::uint64_t previous=0;const auto now=GetTickCount64();if(now-previous<1000)return;previous=now;
    const auto module=GetModuleHandleW(L"unit_appearance_client.dll");
    using namespace sdgo::surface_decal;
    const auto observe=reinterpret_cast<test::LayerObserve>(GetProcAddress(module,test::layerExport));
    const auto app=reinterpret_cast<void*(__cdecl*)()>(ownerApi->Symbol(ownerMod,"game.app"));
    if(!observe||!app||!app())return;const auto state=At<unsigned>(app(),0x4a0);
    if(state!=18&&state!=22&&state!=15&&state!=6)return;
    FILE* file=nullptr;if(_wfopen_s(&file,appearanceScenePath.c_str(),L"ab")||!file)return;
    for(auto uid:appearanceSceneUnits){test::LayerSnapshot value;if(!observe(uid,&value))continue;value.lastCommit={};
        test::SceneHeader header;header.uid=uid;header.state=state;header.tick=now;
        std::fwrite(&header,1,sizeof header,file);std::fwrite(&value,1,sizeof value,file);
        static std::array<bool,32> captures{};
        if((state==18||state==22)&&!captures[state]&&GetEnvironmentVariableW(L"SDGO_APPEARANCE_CAPTURE_SCENES",nullptr,0)&&
           (state==18?value.roomDraws:value.battleDraws)>100){
            const auto request=reinterpret_cast<int(__cdecl*)(const wchar_t*)>(GetProcAddress(GetModuleHandleW(L"game_test_client.dll"),"SdgoGameTestCaptureFrameV1"));
            const auto slash=appearanceScenePath.find_last_of(L'\\');const auto path=appearanceScenePath.substr(0,slash+1)+(state==18?L"appearance-room.bmp":L"appearance-battle.bmp");
            if(request&&request(path.c_str()))captures[state]=true;
        }}
    std::fclose(file);
}
LRESULT CALLBACK AppearanceSceneMessage(int code,WPARAM wp,LPARAM lp){
    const auto error=GetLastError();
    if(code>=0&&!appearanceSceneTimer){appearanceSceneTimer=SetTimer(nullptr,0,250,&AppearanceSceneTick);if(appearanceSceneTimer&&appearanceSceneHook){const auto hook=appearanceSceneHook;appearanceSceneHook=nullptr;UnhookWindowsHookEx(hook);}}
    SetLastError(error);return CallNextHookEx(nullptr,code,wp,lp);
}
bool PrepareAppearanceSceneObserver(){
    wchar_t units[512]{},output[MAX_PATH]{},image[MAX_PATH]{};
    const auto count=GetEnvironmentVariableW(L"SDGO_APPEARANCE_SCENE_UNITS",units,512);
    if(!count)return true;
    if(count>=512||!GetEnvironmentVariableW(L"SDGO_APPEARANCE_SCENE_RECEIPT",output,MAX_PATH)||!GetModuleFileNameW(nullptr,image,MAX_PATH))return false;
    auto* end=std::wcsrchr(image,L'\\');if(!end)return false;*end=0;
    const auto expected=std::wstring(image)+L"\\sdgo\\gametest\\";
    if(!PrepareAppearancePointer(expected))return false;
    if(_wcsnicmp(output,expected.c_str(),expected.size())||std::wcsstr(output,L"..")||std::wcsstr(output,L"/"))return false;
    appearanceScenePath=output;
    const wchar_t* at=units;while(*at){wchar_t* next=nullptr;const auto uid=_wcstoui64(at,&next,10);
        if(!uid||uid>INT64_MAX||next==at||(*next&&*next!=L',')||appearanceSceneUnits.size()>=12)return false;
        appearanceSceneUnits.push_back(uid);at=*next?next+1:next;}
    HMODULE self=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&AppearanceSceneMessage),&self))return false;
    appearanceSceneHook=SetWindowsHookExW(WH_GETMESSAGE,&AppearanceSceneMessage,self,ownerApi->process_initial_thread_id);
    return appearanceSceneHook!=nullptr;
}
