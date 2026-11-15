// Explicit, isolated virtual input for native appearance scene tests. The
// existing input adapter feeds the original mouse scan and UI hit testing.
// No popup, invitation, room state or server reply is constructed here.
#include "../mods/game_test_client/shop_pointer_input.h"
ShopPointerInput appearancePointer;
std::wstring appearancePointerCommand,appearancePointerResult,appearancePointerRun;
void AppearancePointerTick(){
    if(appearancePointerCommand.empty())return;
    static unsigned sequence=0;static std::uint64_t heldAt=0;
    if(heldAt&&GetTickCount64()-heldAt>750){appearancePointer.SetLeftButton(false);heldAt=0;}
    wchar_t run[64]{};GetPrivateProfileStringW(L"run",L"id",L"",run,64,appearancePointerCommand.c_str());
    if(appearancePointerRun!=run)return;
    const auto next=GetPrivateProfileIntW(L"pointer",L"sequence",0,appearancePointerCommand.c_str());
    if(!next||next<=sequence)return;
    const auto x=GetPrivateProfileIntW(L"pointer",L"x",-1,appearancePointerCommand.c_str());
    const auto y=GetPrivateProfileIntW(L"pointer",L"y",-1,appearancePointerCommand.c_str());
    const auto down=GetPrivateProfileIntW(L"pointer",L"down",0,appearancePointerCommand.c_str());
    if(x>8192||y>8192||down>1||!appearancePointer.Install(ownerApi,ownerMod)||!appearancePointer.MoveToClientPoint(int(x),int(y)))return;
    appearancePointer.SetLeftButton(down!=0);heldAt=down?GetTickCount64():0;sequence=next;
    FILE* file=nullptr;if(!_wfopen_s(&file,appearancePointerResult.c_str(),L"wb")&&file){
        std::fprintf(file,"sequence=%u\nposition_reads=%u\nstate_reads=%u\n",next,appearancePointer.PositionReads(),appearancePointer.StateReads());std::fclose(file);
    }
}
bool PrepareAppearancePointer(const std::wstring& directory){
    if(!GetEnvironmentVariableW(L"SDGO_APPEARANCE_TEST_POINTER",nullptr,0))return true;
    wchar_t run[64]{};if(!GetEnvironmentVariableW(L"SDGO_GAME_TEST_RUN",run,64))return false;
    appearancePointerRun=run;appearancePointerCommand=directory+L"pointer-command.ini";appearancePointerResult=directory+L"pointer-result.txt";return true;
}
