// Real native preview edits, cancelled at the end. Existing content is retained.
int CornerWearCase(const SdgoGameTestContextV1* context,void* service,void* root){
    using namespace sdgo::surface_decal;namespace l=layers;
    const auto module=GetModuleHandleW(L"unit_appearance_client.dll");
    const auto observe=reinterpret_cast<test::LayerObserve>(GetProcAddress(module,test::layerExport));
    const auto place=reinterpret_cast<test::Place>(GetProcAddress(module,test::placeExport));
    test::LayerSnapshot snapshot;if(!observe||!place||!observe(0,&snapshot))return SDGO_GAME_TEST_FAIL;
    static unsigned phase=0,visual=1,offset=0,attempt=0;static l::Design baseline;static std::uint32_t keep=0;
    l::Design authority,draft;if(!l::wire::Decode(snapshot.saved.data(),snapshot.savedBytes,authority))return SDGO_GAME_TEST_WAIT;
    if(phase==11){const bool preserved=!snapshot.editing&&authority.revision==baseline.revision&&l::Same(authority,baseline);context->Assert(preserved,"cancelling corner and wear previews retains the original server design");return preserved?SDGO_GAME_TEST_PASS:SDGO_GAME_TEST_FAIL;}
    if(!snapshot.editing||snapshot.pending||!l::wire::Decode(snapshot.preview.data(),snapshot.previewBytes,draft))return SDGO_GAME_TEST_WAIT;
    using String=sdgo::hangar::ui::NativeString;using Find=void*(__thiscall*)(void*,const String*,int);using Event=void(__stdcall*)(void*,unsigned,std::uintptr_t,void*);
    const auto find=reinterpret_cast<Find>(ownerApi->Symbol(ownerMod,"hangar.ui_find_control"));
    const auto click=[&](const wchar_t* name){String key;key.length=unsigned(std::wcslen(name));key.capacity=key.length;key.data.pointer=name;
        auto* c=find(root,&key,4);if(!c||!At<std::uint8_t>(c,0x162)||!At<std::uint8_t>(c,0x167))return false;At<Event>(At<void*>(service),14*4)(service,0x101,reinterpret_cast<std::uintptr_t>(c)+0x118,c);return true;};
    const auto app=reinterpret_cast<void*(__cdecl*)()>(ownerApi->Symbol(ownerMod,"game.app"));RECT rect{};const auto window=app&&app()?At<HWND>(app(),0x3c):nullptr;if(!window||!GetClientRect(window,&rect))return SDGO_GAME_TEST_FAIL;
    const auto slider=[&](ui::Action action,double value){ui::LayerLayout layout;if(!layout.Set(float(At<int>(root,0x154)),float(At<int>(root,0x158))))return false;
        const auto r=layout.Slider(action);const auto xy=MAKELPARAM(int((At<float>(root,0x148)+r.x+r.w*value)*rect.right/1024),int((At<float>(root,0x14c)+r.y+8)*rect.bottom/768));
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,xy);SendMessageW(window,WM_LBUTTONUP,0,xy);return true;};
    if(context->tick%120==0){char message[160];std::snprintf(message,sizeof message,"native corner/wear phase=%u visual=%u offset=%u folded_faces=%u",phase,visual,offset,snapshot.foldedTriangles);context->Observe(message);}
    if(!phase){if(authority.items.size()!=20)return SDGO_GAME_TEST_FAIL;baseline=authority;keep=authority.items.back().id;if(!click(L"SurfaceTab0"))return SDGO_GAME_TEST_WAIT;
        const auto capture=CaptureScene(L"layers-overview");if(capture<1)return capture<0?SDGO_GAME_TEST_FAIL:SDGO_GAME_TEST_WAIT;phase=1;}
    else if(phase==1){if(visual>=20){phase=5;return SDGO_GAME_TEST_WAIT;}
        if(visual>=offset+6){if(!click(L"SurfaceNext"))return SDGO_GAME_TEST_WAIT;offset=std::min(14u,offset+6);return SDGO_GAME_TEST_WAIT;}
        const auto name=L"SurfaceRow"+std::to_wstring(visual-offset);if(!click(name.c_str()))return SDGO_GAME_TEST_WAIT;phase=2;}
    else if(phase==2){if(!click(L"SurfaceTab1"))return SDGO_GAME_TEST_WAIT;phase=3;}
    else if(phase==3){const auto* item=l::Find(draft,snapshot.selected);if(!item||item->id==keep)return SDGO_GAME_TEST_FAIL;
        if(item->visible&&!click(L"SurfaceVisibility"))return SDGO_GAME_TEST_WAIT;phase=4;}
    else if(phase==4){if(!click(L"SurfaceTab0"))return SDGO_GAME_TEST_WAIT;++visual;phase=1;}
    else if(phase==5){if(offset){if(!click(L"SurfacePrevious"))return SDGO_GAME_TEST_WAIT;offset=offset>6?offset-6:0;return SDGO_GAME_TEST_WAIT;}
        if(!click(L"SurfaceRow0"))return SDGO_GAME_TEST_WAIT;phase=6;}
    else if(phase==6){if(!click(L"SurfaceTab1"))return SDGO_GAME_TEST_WAIT;phase=7;}
    else if(phase==7){if(!slider(ui::Action::size,.45)||!slider(ui::Action::wear,0)||!click(L"SurfacePlace"))return SDGO_GAME_TEST_WAIT;phase=8;}
    else if(phase==8){
        if(!snapshot.foldedTriangles){if(attempt>=25){context->Assert(0,"native armor surface supplies an actual corner for this decal");return SDGO_GAME_TEST_FAIL;}
            const double x=.44+(attempt%5)*.03,y=.30+(attempt/5)*.04;if(place(rect.right*x,rect.bottom*y))++attempt;else ++attempt;return SDGO_GAME_TEST_WAIT;}
        context->Assert(1,"real wrapped native geometry includes faces beyond the old planar normal cutoff");
        const auto capture=CaptureScene(L"corner-pristine");if(capture<1)return capture<0?SDGO_GAME_TEST_FAIL:SDGO_GAME_TEST_WAIT;
        if(!slider(ui::Action::wear,.6))return SDGO_GAME_TEST_FAIL;phase=9;
    }else if(phase==9){const auto* item=l::Find(draft,keep);if(!item||item->style.wear<590)return SDGO_GAME_TEST_WAIT;
        const auto capture=CaptureScene(L"corner-worn");if(capture<1)return capture<0?SDGO_GAME_TEST_FAIL:SDGO_GAME_TEST_WAIT;
        if(!slider(ui::Action::wear,0))return SDGO_GAME_TEST_FAIL;phase=10;
    }else if(phase==10){const auto* item=l::Find(draft,keep);if(!item||item->style.wear)return SDGO_GAME_TEST_WAIT;
        const auto capture=CaptureScene(L"corner-restored");if(capture<1)return capture<0?SDGO_GAME_TEST_FAIL:SDGO_GAME_TEST_WAIT;
        if(!click(L"SurfaceCancel"))return SDGO_GAME_TEST_WAIT;phase=11;
    }
    return SDGO_GAME_TEST_WAIT;
}
