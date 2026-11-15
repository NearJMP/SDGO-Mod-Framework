// Included by the isolated native appearance provider after its UI helpers.
// Every edit uses the real page event or the production pointer handler.
struct LayeredReplay {bool done=false;std::int32_t status=-1;std::vector<std::uint8_t> bytes;};
LayeredReplay layeredReplay;
void __cdecl LayeredReplayReply(void*,std::uint16_t,std::int32_t status,const std::uint8_t* bytes,std::uint32_t count){layeredReplay.status=status;if(bytes)layeredReplay.bytes.assign(bytes,bytes+count);layeredReplay.done=true;}
int LayeredEditorCase(const SdgoGameTestContextV1* context,void* service,void* root){
    using namespace sdgo::surface_decal;
    namespace l=layers;namespace w=l::wire;
    using String=sdgo::hangar::ui::NativeString;
    using Find=void*(__thiscall*)(void*,const String*,int);
    using Event=void(__stdcall*)(void*,unsigned,std::uintptr_t,void*);
    const auto find=reinterpret_cast<Find>(ownerApi->Symbol(ownerMod,"hangar.ui_find_control"));
    const auto module=GetModuleHandleW(L"unit_appearance_client.dll");
    const auto observe=reinterpret_cast<test::LayerObserve>(GetProcAddress(module,test::layerExport));
    const auto place=reinterpret_cast<test::Place>(GetProcAddress(module,test::placeExport));
    test::LayerSnapshot snapshot;if(!find||!observe||!place||!observe(0,&snapshot))return SDGO_GAME_TEST_FAIL;
    if(!snapshot.editing||snapshot.pending)return SDGO_GAME_TEST_WAIT;
    const auto click=[&](const wchar_t* name){
        String key;key.length=unsigned(std::wcslen(name));key.capacity=key.length;key.data.pointer=name;
        if(key.length<8){key.capacity=7;std::memcpy(key.data.small,name,(key.length+1)*2);}
        auto* c=find(root,&key,4);if(!c||!At<std::uint8_t>(c,0x162)||!At<std::uint8_t>(c,0x167))return false;
        At<Event>(At<void*>(service),14*4)(service,0x101,reinterpret_cast<std::uintptr_t>(c)+0x118,c);return true;
    };
    const auto slider=[&](ui::Action action,double fraction){
        const auto app=reinterpret_cast<void*(__cdecl*)()>(ownerApi->Symbol(ownerMod,"game.app"));if(!app||!app())return false;
        const auto window=At<HWND>(app(),0x3c);RECT client{};if(!GetClientRect(window,&client)||client.right<=0||client.bottom<=0)return false;
        ui::LayerLayout layout;if(!layout.Set(float(At<int>(root,0x154)),float(At<int>(root,0x158))))return false;
        const auto rect=layout.Slider(action);const auto x=int((At<float>(root,0x148)+rect.x+rect.w*fraction)*client.right/1024),y=int((At<float>(root,0x14c)+rect.y+8)*client.bottom/768);
        const auto point=MAKELPARAM(x,y);SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(window,WM_LBUTTONUP,0,point);return true;
    };
    static unsigned phase=0;static l::Design baseline,proposal;static std::uint64_t phaseAt=0,retryAt=0;static w::Token paidToken{};
    l::Design authority,draft;
    if(!w::Decode(snapshot.saved.data(),snapshot.savedBytes,authority))return SDGO_GAME_TEST_FAIL;
    const bool hasDraft=w::Decode(snapshot.preview.data(),snapshot.previewBytes,draft);
    if(context->tick%120==0){char text[180];std::snprintf(text,sizeof text,"layered editor phase=%u layers=%u stock=%u armed=%u cost=%u draws=%u triangles=%u",phase,snapshot.previewLayers,snapshot.stockTypes,snapshot.armed,snapshot.quotedItems,snapshot.draws,snapshot.triangles);context->Observe(text);}
    if(phase==0){
        if(!authority.items.empty()){context->Assert(0,"twenty-layer authoring case requires an existing uncustomized test unit");return SDGO_GAME_TEST_FAIL;}
        baseline=authority;if(!click(L"SurfaceAdd"))return SDGO_GAME_TEST_WAIT;phase=1;
    }else if(phase==1){wchar_t configured[8]{};GetEnvironmentVariableW(L"SDGO_SURFACE_STICKER_ROW",configured,8);const auto row=std::wcstoul(configured,nullptr,10);if(row>=ui::LayerLayout::visibleRows)return SDGO_GAME_TEST_FAIL;
        const auto key=L"SurfaceRow"+std::to_wstring(row);if(!snapshot.stockTypes||!click(key.c_str()))return SDGO_GAME_TEST_WAIT;phase=2;}
    else if(phase==2){
        if(!snapshot.selected||!snapshot.previewLayers)return SDGO_GAME_TEST_WAIT;
        const auto app=reinterpret_cast<void*(__cdecl*)()>(ownerApi->Symbol(ownerMod,"game.app"));RECT client{};if(!app||!app()||!GetClientRect(At<HWND>(app(),0x3c),&client))return SDGO_GAME_TEST_FAIL;
        bool placed=false;for(double y:{.43,.38,.48,.33,.53}){for(double x:{.5,.47,.53,.44,.56})if(place(client.right*x,client.bottom*y)){placed=true;break;}if(placed)break;}
        if(!placed)return SDGO_GAME_TEST_WAIT;
        context->Assert(1,"new independent layer is anchored by the production ray picker on real unit geometry");phase=3;
    }else if(phase==3){if(!hasDraft)return SDGO_GAME_TEST_WAIT;if(!click(L"SurfaceTab0"))return SDGO_GAME_TEST_WAIT;phase=4;}
    else if(phase==4){
        if(!hasDraft)return SDGO_GAME_TEST_WAIT;
        if(draft.items.size()<20){if(!click(L"SurfaceDuplicate"))return SDGO_GAME_TEST_WAIT;phase=3;}
        else{context->Assert(draft.items.size()==20,"native UI creates all twenty stable independent layers");if(!click(L"SurfaceTab1"))return SDGO_GAME_TEST_WAIT;phase=5;}
    }else if(phase==5){
        if(!slider(ui::Action::wear,.45)||!slider(ui::Action::rotation,.62))return SDGO_GAME_TEST_FAIL;
        const auto captured=CaptureScene(L"00-properties");if(captured<1)return captured<0?SDGO_GAME_TEST_FAIL:SDGO_GAME_TEST_WAIT;
        if(!click(L"SurfaceTab0"))return SDGO_GAME_TEST_WAIT;phase=6;
    }else if(phase==6){
        const auto captured=CaptureScene(L"01-twenty-layers");if(captured<1)return captured<0?SDGO_GAME_TEST_FAIL:SDGO_GAME_TEST_WAIT;
        if(!click(L"SurfaceBottom"))return SDGO_GAME_TEST_WAIT;phase=7;
    }else if(phase==7){
        if(!hasDraft||draft.items.size()!=20)return SDGO_GAME_TEST_WAIT;
        bool worn=false;for(const auto& item:draft.items)worn=worn||item.style.wear>400&&item.style.wear<500;
        context->Assert(worn,"production pointer gestures edit local scratched coverage");if(!worn)return SDGO_GAME_TEST_FAIL;
        proposal=draft;if(!click(L"SurfaceSave"))return SDGO_GAME_TEST_WAIT;phase=8;
    }else if(phase==8){
        if(!snapshot.armed)return SDGO_GAME_TEST_WAIT;const unsigned expected=GetEnvironmentVariableW(L"SDGO_APPEARANCE_REUSABLE_TEST",nullptr,0)?0:20;
        context->Assert(snapshot.quotedItems==expected,"World quote applies the configured consumption policy to all twenty new layers");
        if(snapshot.quotedItems!=expected)return SDGO_GAME_TEST_FAIL;
        const auto captured=CaptureScene(L"02-consumption-quote");if(captured<1)return captured<0?SDGO_GAME_TEST_FAIL:SDGO_GAME_TEST_WAIT;
        if(!click(L"SurfaceSave"))return SDGO_GAME_TEST_WAIT;phase=9;
    }else if(phase==9){
        if(snapshot.armed||authority.revision==baseline.revision){
            if(snapshot.armed&&snapshot.state==unsigned(EditState::editing)&&GetTickCount64()-retryAt>1000){retryAt=GetTickCount64();click(L"SurfaceSave");}
            return SDGO_GAME_TEST_WAIT;}
        const bool exact=authority.revision==baseline.revision+1&&l::Same(authority,proposal);
        context->Assert(exact,"real commit acknowledges the exact twenty-layer proposal and one newer revision");if(!exact)return SDGO_GAME_TEST_FAIL;
        if(!phaseAt)phaseAt=GetTickCount64();bool allDrawn=true;for(unsigned i=0;i<20;++i)allDrawn=allDrawn&&snapshot.layerDraws[i]>=10;
        if(!allDrawn||GetTickCount64()-phaseAt<1000)return SDGO_GAME_TEST_WAIT;
        context->Assert(1,"every one of twenty independent layer IDs is drawn on the genuine native mesh");
        paidToken=snapshot.lastCommit;
        const auto captured=CaptureScene(L"03-authoritative-saved");if(captured<1)return captured<0?SDGO_GAME_TEST_FAIL:SDGO_GAME_TEST_WAIT;
        if(!click(L"SurfaceTab1"))return SDGO_GAME_TEST_WAIT;phase=10;
    }else if(phase==10){if(!slider(ui::Action::wear,.7))return SDGO_GAME_TEST_FAIL;phase=11;}
    else if(phase==11){if(!click(L"SurfaceSave"))return SDGO_GAME_TEST_WAIT;phase=12;}
    else if(phase==12){if(!snapshot.armed)return SDGO_GAME_TEST_WAIT;context->Assert(snapshot.quotedItems==0,"subsequent style edits quote zero inventory consumption");if(snapshot.quotedItems)return SDGO_GAME_TEST_FAIL;if(!click(L"SurfaceSave"))return SDGO_GAME_TEST_WAIT;phase=13;}
    else if(phase==13){
        if(snapshot.armed||authority.revision<baseline.revision+2)return SDGO_GAME_TEST_WAIT;
        const auto path=std::wstring(context->directory)+L"\\layered-authority.bin";FILE* file=nullptr;if(_wfopen_s(&file,path.c_str(),L"wb")||!file)return SDGO_GAME_TEST_FAIL;
        const bool written=std::fwrite(snapshot.saved.data(),1,snapshot.savedBytes,file)==snapshot.savedBytes;std::fclose(file);if(!written)return SDGO_GAME_TEST_FAIL;
        phase=14;
    }else if(phase==14){
        if(paidToken==w::Token{})return SDGO_GAME_TEST_FAIL;
        const auto carrier=GetModuleHandleW(L"friend_unit_gift_client.dll");
        using namespace sdgo::friend_unit_gift;
        const auto send=reinterpret_cast<ClientExtensionSendFn>(GetProcAddress(carrier,SDGO_FRIEND_UNIT_GIFT_CLIENT_EXTENSION_SEND_EXPORT));
        const auto state=reinterpret_cast<ClientExtensionStateFn>(GetProcAddress(carrier,SDGO_FRIEND_UNIT_GIFT_CLIENT_EXTENSION_STATE_EXPORT));
        if(!send||!state)return SDGO_GAME_TEST_FAIL;if(state()!=1)return SDGO_GAME_TEST_WAIT;
        std::uint8_t request[24];w::Put(request,authority.unitSerial,8);std::copy(paidToken.begin(),paidToken.end(),request+8);
        if(!send(w::commit,request,sizeof request,&LayeredReplayReply,nullptr))return SDGO_GAME_TEST_WAIT;phase=15;
    }else if(phase==15){
        if(!layeredReplay.done)return SDGO_GAME_TEST_WAIT;w::Response receipt;
        const bool exact=!layeredReplay.status&&w::DecodeResponse(layeredReplay.bytes.data(),layeredReplay.bytes.size(),receipt)&&receipt.result==w::Result::ok&&receipt.token==paidToken&&receipt.authority.revision==baseline.revision+1&&l::Same(receipt.authority,proposal);
        context->Assert(exact,"replaying the first commit after a later save returns its original durable receipt");if(!exact)return SDGO_GAME_TEST_FAIL;
        if(!click(L"SurfaceCancel"))return SDGO_GAME_TEST_WAIT;context->Assert(1,"normal cancel closes the saved native editor");return SDGO_GAME_TEST_PASS;
    }
    return SDGO_GAME_TEST_WAIT;
}
