// Native page lifecycle regression. Draft edits are cancelled; no RGB save is sent.
int RgbEntryCase(const SdgoGameTestContextV1* context){
    using String=sdgo::hangar::ui::NativeString;
    using Find=sdgo::hangar::ui::Bindings::Find;
    using Event=void(__stdcall*)(void*,unsigned,std::uintptr_t,void*);
    static unsigned phase=0;static std::uint64_t entered=0;
    const auto switchTab=[&](const wchar_t* name){
        auto* menu=Service("test.factory_tab_menu_vtable");if(!menu||!At<std::uint8_t>(menu,0xac))return false;
        auto* root=At<void*>(menu,0xd0);const auto find=reinterpret_cast<Find>(ownerApi->Symbol(ownerMod,"hangar.ui_find_control"));
        String key;key.data.pointer=name;key.length=unsigned(std::wcslen(name));key.capacity=key.length;
        auto* control=root&&find?find(root,&key,12):nullptr;
        if(!control||!At<std::uint8_t>(control,0x162)||!At<std::uint8_t>(control,0x167))return false;
        auto event=At<Event>(At<void*>(menu),14*4);event(menu,0x301,reinterpret_cast<std::uintptr_t>(control)+0x118,control);return true;
    };
    const auto toggle=[](){return ClickRgbMode(Service("rgb_paint.page_vtable"));};
    auto* paintPage=Service("rgb_paint.page_vtable");
    auto* paintRoot=paintPage?At<void*>(paintPage,0xd0):nullptr;
    const float slide=paintRoot?At<float>(paintRoot,0x148):0;
    static bool capturedIn=false,capturedOut=false;
    if((phase==1||phase==2)&&paintRoot&&slide<-40&&slide>-220){
        auto& captured=phase==1?capturedOut:capturedIn;
        if(!captured){
            wchar_t directory[MAX_PATH]{};
            if(GetEnvironmentVariableW(L"SDGO_SURFACE_DEMO_DIR",directory,MAX_PATH)){
                const auto request=reinterpret_cast<int(__cdecl*)(const wchar_t*)>(GetProcAddress(GetModuleHandleW(L"game_test_client.dll"),"SdgoGameTestCaptureFrameV1"));
                const auto path=std::wstring(directory)+(phase==1?L"\\rgb-slide-out.bmp":L"\\rgb-slide-in.bmp");
                captured=request&&request(path.c_str());
                if(captured){char text[120];std::snprintf(text,sizeof text,"native RGB transition frame requested: phase=%u pageX=%.1f",phase,slide);context->Observe(text);}
            }
        }
    }
    SdgoRgbPaintObservationV1 value;const bool visible=ObservePanel(value);
    if(context->tick%120==0){char text[96];std::snprintf(text,sizeof text,"RGB entry phase=%u visible=%u editing=%u ready=%u",phase,visible?1:0,value.editing,value.authority_ready);context->Observe(text);}
    if(phase==1||phase==5){
        auto* sticker=Service("surface_decal.sticker_vtable");
        if(!sticker||!At<std::uint8_t>(sticker,0xac)||visible)return SDGO_GAME_TEST_WAIT;
        context->Assert(1,"native emblem tab remains reachable from the native paint page");
        if(!switchTab(L"StickerRadio"))return SDGO_GAME_TEST_WAIT;++phase;return SDGO_GAME_TEST_WAIT;
    }
    if(!visible)return SDGO_GAME_TEST_WAIT;
    if(phase==0){
        context->Assert(!value.editing,"direct factory paint entry defaults to the native palette");if(value.editing)return SDGO_GAME_TEST_FAIL;
        const auto frame=CaptureScene(L"rgb-default-native");if(frame<1)return frame<0?SDGO_GAME_TEST_FAIL:SDGO_GAME_TEST_WAIT;
        if(!switchTab(L"PaintRadio"))return SDGO_GAME_TEST_WAIT;phase=1;
    }else if(phase==2){
        if(std::fabs(slide)>.5f)return SDGO_GAME_TEST_WAIT;
        context->Assert(!value.editing,"returning from another native tab keeps the stock palette");if(value.editing)return SDGO_GAME_TEST_FAIL;
        if(!toggle())return SDGO_GAME_TEST_FAIL;entered=GetTickCount64();phase=3;
    }else if(phase==3){
        context->Assert(value.editing!=0,"only the RGB button enters the RGB editor");if(!value.editing)return SDGO_GAME_TEST_FAIL;
        if(!value.authority_ready||GetTickCount64()-entered<3000)return SDGO_GAME_TEST_WAIT;
        const auto frame=CaptureScene(L"rgb-opened-by-button");if(frame<1)return frame<0?SDGO_GAME_TEST_FAIL:SDGO_GAME_TEST_WAIT;
        auto* page=Service("rgb_paint.page_vtable");if(!page||!ExercisePanel(context,page))return SDGO_GAME_TEST_FAIL;
        if(!toggle())return SDGO_GAME_TEST_FAIL;phase=4;
    }else if(phase==4){
        context->Assert(!value.editing,"native mode button returns after draft RGB adjustments");if(value.editing)return SDGO_GAME_TEST_FAIL;
        if(!switchTab(L"PaintRadio"))return SDGO_GAME_TEST_WAIT;phase=5;
    }else if(phase==6){
        if(std::fabs(slide)>.5f)return SDGO_GAME_TEST_WAIT;
        if(GetEnvironmentVariableW(L"SDGO_SURFACE_DEMO_DIR",nullptr,0)){
            context->Assert(capturedIn&&capturedOut,"native entry and exit animation frames were captured for visual review");
            if(!capturedIn||!capturedOut)return SDGO_GAME_TEST_FAIL;
        }
        context->Assert(!value.editing,"re-entering paint after RGB edits again defaults to native mode");if(value.editing)return SDGO_GAME_TEST_FAIL;
        if(!value.authority_ready)return SDGO_GAME_TEST_WAIT;
        const auto frame=CaptureScene(L"rgb-returned-native");if(frame<1)return frame<0?SDGO_GAME_TEST_FAIL:SDGO_GAME_TEST_WAIT;
        context->Assert(1,"native paint, emblem and explicit RGB transitions complete without saving the draft");return SDGO_GAME_TEST_PASS;
    }
    return SDGO_GAME_TEST_WAIT;
}
