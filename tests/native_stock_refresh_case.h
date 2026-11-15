// Replace one saved layer through native controls and observe the original
// inventory response buffer. No inventory/cache value is written by this case.
int NativeStockRefreshCase(const SdgoGameTestContextV1* context,void* service,void* root){
    using namespace sdgo::surface_decal;namespace l=layers;
    const auto observe=reinterpret_cast<test::LayerObserve>(GetProcAddress(GetModuleHandleW(L"unit_appearance_client.dll"),test::layerExport));
    test::LayerSnapshot snapshot;if(!observe||!observe(0,&snapshot))return SDGO_GAME_TEST_FAIL;
    l::Design authority,draft;if(!l::wire::Decode(snapshot.saved.data(),snapshot.savedBytes,authority)||!l::wire::Decode(snapshot.preview.data(),snapshot.previewBytes,draft))return SDGO_GAME_TEST_WAIT;
    if(snapshot.pending)return SDGO_GAME_TEST_WAIT;
    using String=sdgo::hangar::ui::NativeString;using Find=void*(__thiscall*)(void*,const String*,int);using Event=void(__stdcall*)(void*,unsigned,std::uintptr_t,void*);
    const auto find=reinterpret_cast<Find>(ownerApi->Symbol(ownerMod,"hangar.ui_find_control"));
    const auto click=[&](const wchar_t* name){String key;key.length=unsigned(std::wcslen(name));key.capacity=key.length;key.data.pointer=name;auto* c=find(root,&key,4);
        if(!c||!At<std::uint8_t>(c,0x162)||!At<std::uint8_t>(c,0x167))return false;At<Event>(At<void*>(service),14*4)(service,0x101,reinterpret_cast<std::uintptr_t>(c)+0x118,c);return true;};
    static unsigned phase=0,page=0;static std::uint64_t revision=0;static std::array<std::uint8_t,313> before{};
    if(context->tick%120==0){char text[160];std::snprintf(text,sizeof text,"native stock phase=%u cached=%u current=%u pending=%u total=%u layers=%u",phase,snapshot.nativeStockKnown,snapshot.nativeStockPage,snapshot.nativeStockPending,snapshot.nativeStockTotal,unsigned(draft.items.size()));context->Observe(text);}
    if(!phase){if(authority.items.size()!=20)return SDGO_GAME_TEST_FAIL;if(!snapshot.nativeStockKnown||snapshot.nativeStockPending)return SDGO_GAME_TEST_WAIT;
        before=snapshot.nativeStock;page=snapshot.nativeStockPage;revision=authority.revision;if(!click(L"SurfaceTab0"))return SDGO_GAME_TEST_WAIT;phase=1;}
    else if(phase==1){if(!click(L"SurfaceRemove"))return SDGO_GAME_TEST_WAIT;phase=2;}
    else if(phase==2){if(draft.items.size()!=19){phase=1;return SDGO_GAME_TEST_WAIT;}if(!click(L"SurfaceDuplicate"))return SDGO_GAME_TEST_WAIT;phase=3;}
    else if(phase==3){if(draft.items.size()!=20)return SDGO_GAME_TEST_WAIT;if(!click(L"SurfaceSave"))return SDGO_GAME_TEST_WAIT;phase=4;}
    else if(phase==4){if(!snapshot.armed)return SDGO_GAME_TEST_WAIT;context->Assert(snapshot.quotedItems==1,"one replaced layer consumes one owned sticker");if(snapshot.quotedItems!=1)return SDGO_GAME_TEST_FAIL;if(!click(L"SurfaceSave"))return SDGO_GAME_TEST_WAIT;phase=5;}
    else if(phase==5){if(snapshot.armed||authority.revision<=revision||snapshot.nativeStockPending||!snapshot.nativeStockKnown||snapshot.nativeStock==before)return SDGO_GAME_TEST_WAIT;
        const bool same=snapshot.nativeStockPage==page;context->Assert(same,"original native sticker inventory refreshes on the same page after commit");if(!same)return SDGO_GAME_TEST_FAIL;
        const auto capture=CaptureScene(L"native-stock-refreshed");if(capture<1)return capture<0?SDGO_GAME_TEST_FAIL:SDGO_GAME_TEST_WAIT;
        const auto path=std::wstring(context->directory)+L"\\native-stock-refresh.bin";FILE* file=nullptr;if(_wfopen_s(&file,path.c_str(),L"wb")||!file)return SDGO_GAME_TEST_FAIL;
        std::fwrite(before.data(),1,before.size(),file);std::fwrite(snapshot.nativeStock.data(),1,snapshot.nativeStock.size(),file);std::fclose(file);
        if(!click(L"SurfaceCancel"))return SDGO_GAME_TEST_WAIT;return SDGO_GAME_TEST_PASS;}
    return SDGO_GAME_TEST_WAIT;
}
