// Real native preview edits, cancelled at the end. Existing content is retained.
int CornerWearCase(const SdgoGameTestContextV1* context,void* service,void* root){
    using namespace sdgo::surface_decal;namespace l=layers;
    const auto module=GetModuleHandleW(L"unit_appearance_client.dll");
    const auto observe=reinterpret_cast<test::LayerObserve>(GetProcAddress(module,test::layerExport));
    const auto place=reinterpret_cast<test::Place>(GetProcAddress(module,test::placeExport));
    test::LayerSnapshot snapshot;if(!observe||!place||!observe(0,&snapshot))return SDGO_GAME_TEST_FAIL;
    static unsigned phase=0,visual=1,offset=0,attempt=0,restore=1;static l::Design baseline,expectedSaved;static std::uint32_t keep=0;static std::uint64_t probeAt=0;
    l::Design authority,draft;if(!l::wire::Decode(snapshot.saved.data(),snapshot.savedBytes,authority))return SDGO_GAME_TEST_WAIT;
    if(phase==11){const bool preserved=!snapshot.editing&&authority.revision==baseline.revision&&l::Same(authority,baseline);context->Assert(preserved,"cancelling corner and wear previews retains the original server design");return preserved?SDGO_GAME_TEST_PASS:SDGO_GAME_TEST_FAIL;}
    if(phase==19){const bool closed=!snapshot.editing&&authority.revision==baseline.revision+1&&l::Same(authority,expectedSaved);context->Assert(closed,"the saved fixed-seam design closes through the normal native editor");return closed?SDGO_GAME_TEST_PASS:SDGO_GAME_TEST_FAIL;}
    if(!snapshot.editing||snapshot.pending||!l::wire::Decode(snapshot.preview.data(),snapshot.previewBytes,draft))return SDGO_GAME_TEST_WAIT;
    using String=sdgo::hangar::ui::NativeString;using Find=void*(__thiscall*)(void*,const String*,int);using Event=void(__stdcall*)(void*,unsigned,std::uintptr_t,void*);
    const auto find=reinterpret_cast<Find>(ownerApi->Symbol(ownerMod,"hangar.ui_find_control"));
    const auto click=[&](const wchar_t* name){String key;key.length=unsigned(std::wcslen(name));key.capacity=key.length;key.data.pointer=name;
        auto* c=find(root,&key,4);if(!c||!At<std::uint8_t>(c,0x162)||!At<std::uint8_t>(c,0x167))return false;At<Event>(At<void*>(service),14*4)(service,0x101,reinterpret_cast<std::uintptr_t>(c)+0x118,c);return true;};
    const auto app=reinterpret_cast<void*(__cdecl*)()>(ownerApi->Symbol(ownerMod,"game.app"));RECT rect{};const auto window=app&&app()?At<HWND>(app(),0x3c):nullptr;if(!window||!GetClientRect(window,&rect))return SDGO_GAME_TEST_FAIL;
    const auto slider=[&](ui::Action action,double value){ui::LayerLayout layout;if(!layout.Set(float(At<int>(root,0x154)),float(At<int>(root,0x158))))return false;
        const auto r=layout.Slider(action);const auto xy=MAKELPARAM(int((At<float>(root,0x148)+r.x+r.w*value)*rect.right/1024),int((At<float>(root,0x14c)+r.y+8)*rect.bottom/768));
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,xy);SendMessageW(window,WM_LBUTTONUP,0,xy);return true;};
    const bool fixedSeam=GetEnvironmentVariableW(L"SDGO_APPEARANCE_FIXED_SEAM_TEST",nullptr,0)>0;
    if(context->tick%120==0){char message[200];std::snprintf(message,sizeof message,"native corner/wear phase=%u visual=%u offset=%u folded_faces=%u fixed_parts=%u fixed_draws=%u",phase,visual,offset,snapshot.foldedTriangles,snapshot.fixedParts,snapshot.fixedPartDraws);context->Observe(message);}
    if(!phase){if(authority.items.size()!=20)return SDGO_GAME_TEST_FAIL;baseline=authority;keep=authority.items.back().id;if(!click(L"SurfaceTab0"))return SDGO_GAME_TEST_WAIT;
        if(GetEnvironmentVariableW(L"SDGO_APPEARANCE_BINDINGS_PROBE",nullptr,0)){
            const auto dump=reinterpret_cast<test::DumpBindings>(GetProcAddress(module,test::bindingsExport));const auto output=std::wstring(context->directory)+L"\\fixed-node-bindings.json";
            static bool dumped=false;if(!dumped){dumped=dump&&dump(authority.unitSerial,output.c_str());if(!dumped)return SDGO_GAME_TEST_WAIT;}
        }
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
        if(fixedSeam&&snapshot.fixedParts&&snapshot.fixedPartDraws<10&&GetTickCount64()-probeAt<1500)return SDGO_GAME_TEST_WAIT;
        const bool found=fixedSeam?snapshot.fixedParts&&snapshot.fixedPartDraws>=10:snapshot.foldedTriangles>0;
        if(!found){const unsigned limit=fixedSeam?165:25;if(attempt>=limit){context->Assert(0,fixedSeam?"a real adjacent fixed native part receives the same decal":"native armor surface supplies an actual corner for this decal");return SDGO_GAME_TEST_FAIL;}
            const double x=fixedSeam?.35+(attempt%11)*.03:.44+(attempt%5)*.03,y=fixedSeam?.25+(attempt/11)*.035:.30+(attempt/5)*.04;
            place(rect.right*x,rect.bottom*y);probeAt=GetTickCount64();++attempt;return SDGO_GAME_TEST_WAIT;}
        context->Assert(1,fixedSeam?"real native rendering draws this single layer on more than one verified fixed part":"real wrapped native geometry includes faces beyond the old planar normal cutoff");
        const auto capture=CaptureScene(L"corner-pristine");if(capture<1)return capture<0?SDGO_GAME_TEST_FAIL:SDGO_GAME_TEST_WAIT;
        if(!slider(ui::Action::wear,.6))return SDGO_GAME_TEST_FAIL;phase=9;
    }else if(phase==9){const auto* item=l::Find(draft,keep);if(!item||item->style.wear<590)return SDGO_GAME_TEST_WAIT;
        const auto capture=CaptureScene(L"corner-worn");if(capture<1)return capture<0?SDGO_GAME_TEST_FAIL:SDGO_GAME_TEST_WAIT;
        if(!slider(ui::Action::wear,0))return SDGO_GAME_TEST_FAIL;phase=10;
    }else if(phase==10){const auto* item=l::Find(draft,keep);if(!item||item->style.wear)return SDGO_GAME_TEST_WAIT;
        const auto capture=CaptureScene(L"corner-restored");if(capture<1)return capture<0?SDGO_GAME_TEST_FAIL:SDGO_GAME_TEST_WAIT;
        if(fixedSeam&&GetEnvironmentVariableW(L"SDGO_APPEARANCE_FIXED_SEAM_SAVE",nullptr,0)){if(!click(L"SurfaceTab0"))return SDGO_GAME_TEST_WAIT;phase=12;}
        else{if(!click(L"SurfaceCancel"))return SDGO_GAME_TEST_WAIT;phase=11;}
    }else if(phase==12){
        if(restore>=20){phase=16;return SDGO_GAME_TEST_WAIT;}
        if(restore>=offset+6){if(!click(L"SurfaceNext"))return SDGO_GAME_TEST_WAIT;offset=std::min(14u,offset+6);return SDGO_GAME_TEST_WAIT;}
        const auto name=L"SurfaceRow"+std::to_wstring(restore-offset);if(!click(name.c_str()))return SDGO_GAME_TEST_WAIT;phase=13;
    }else if(phase==13){if(!click(L"SurfaceTab1"))return SDGO_GAME_TEST_WAIT;phase=14;}
    else if(phase==14){const auto* item=l::Find(draft,snapshot.selected);const auto* priorItem=l::Find(baseline,snapshot.selected);if(!item||!priorItem)return SDGO_GAME_TEST_FAIL;
        if(item->visible!=priorItem->visible&&!click(L"SurfaceVisibility"))return SDGO_GAME_TEST_WAIT;phase=15;}
    else if(phase==15){if(!click(L"SurfaceTab0"))return SDGO_GAME_TEST_WAIT;++restore;phase=12;}
    else if(phase==16){if(!click(L"SurfaceSave"))return SDGO_GAME_TEST_WAIT;phase=17;}
    else if(phase==17){if(!snapshot.armed)return SDGO_GAME_TEST_WAIT;
        context->Assert(snapshot.quotedItems==0,"moving one existing layer across a fixed seam quotes zero item consumption");if(snapshot.quotedItems)return SDGO_GAME_TEST_FAIL;
        expectedSaved=draft;if(!click(L"SurfaceSave"))return SDGO_GAME_TEST_WAIT;phase=18;
    }else if(phase==18){if(snapshot.armed||authority.revision<=baseline.revision)return SDGO_GAME_TEST_WAIT;
        const bool exact=authority.revision==baseline.revision+1&&l::Same(authority,expectedSaved);context->Assert(exact,"World acknowledges the exact fixed-seam placement with all twenty original visibility states");if(!exact)return SDGO_GAME_TEST_FAIL;
        FILE* file=nullptr;const auto path=std::wstring(context->directory)+L"\\fixed-seam-authority.bin";if(_wfopen_s(&file,path.c_str(),L"wb")||!file)return SDGO_GAME_TEST_FAIL;
        const bool written=std::fwrite(snapshot.saved.data(),1,snapshot.savedBytes,file)==snapshot.savedBytes;std::fclose(file);if(!written)return SDGO_GAME_TEST_FAIL;
        const auto capture=CaptureScene(L"fixed-authoritative-saved");if(capture<1)return capture<0?SDGO_GAME_TEST_FAIL:SDGO_GAME_TEST_WAIT;
        if(!click(L"SurfaceCancel"))return SDGO_GAME_TEST_WAIT;phase=19;
    }
    return SDGO_GAME_TEST_WAIT;
}
