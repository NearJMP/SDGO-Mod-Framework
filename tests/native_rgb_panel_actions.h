// Real pointer/keyboard interaction shared by native RGB acceptance providers.
bool ExercisePanel(const SdgoGameTestContextV1* context,void* page){
    using App=void*(__cdecl*)();
    const auto app=reinterpret_cast<App>(ownerApi->Symbol(ownerMod,"game.app"));
    const auto hwnd=app&&app()?At<HWND>(app(),0x3c):nullptr;
    RECT client{};if(!hwnd||!GetClientRect(hwnd,&client)||!client.right||!client.bottom)return false;
    auto click=[&](int x,int y){const auto position=MAKELPARAM(x*client.right/1024,y*client.bottom/768);SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,position);SendMessageW(hwnd,WM_LBUTTONUP,0,position);};
    auto type=[&](const wchar_t* text){for(;*text;++text)SendMessageW(hwnd,WM_CHAR,*text,0);SendMessageW(hwnd,WM_KEYDOWN,VK_RETURN,0);};
    auto select=[&](unsigned i){auto* c=At<void*>(page,0x14c+i*4);float x=At<float>(c,0x148),y=At<float>(c,0x14c);for(auto* p=At<void*>(c,0x15c);p;p=At<void*>(p,0x15c)){x+=At<float>(p,0x148);y+=At<float>(p,0x14c);}click(static_cast<int>(x)+25,static_cast<int>(y)+25);};
    SdgoRgbPaintObservationV1 originalState;ObservePanel(originalState);
    if(!originalState.editing){click(940,640);if(!ObservePanel(originalState)||!originalState.editing)return false;}
    click(970,410);type(L"256");SdgoRgbPaintObservationV1 invalid;ObservePanel(invalid);
    bool ok=invalid.invalid_input&&std::memcmp(invalid.rgb,originalState.rgb,sizeof invalid.rgb)==0;
    context->Assert(ok,"native RGB numeric input rejects 256 without changing preview");
    SendMessageW(hwnd,WM_KEYDOWN,VK_ESCAPE,0);
    click(970,410);type(L"17");click(970,448);type(L"34");click(970,486);type(L"51");
    SdgoRgbPaintObservationV1 numeric;ObservePanel(numeric);
    const bool channels=numeric.rgb[0][0]==17&&numeric.rgb[0][1]==34&&numeric.rgb[0][2]==51;
    context->Assert(channels,"native RGB numeric fields edit all three channels");ok=channels&&ok;
    click(850,410);SdgoRgbPaintObservationV1 slider;ObservePanel(slider);
    const bool moved=slider.rgb[0][0]>80&&slider.rgb[0][0]<110&&slider.rgb[0][1]==34;
    context->Assert(moved,"native RGB slider responds to mouse input");ok=moved&&ok;
    click(990,260);click(900,220);SdgoRgbPaintObservationV1 square;ObservePanel(square);
    const bool picked=std::memcmp(square.rgb[0],slider.rgb[0],3)!=0;context->Assert(picked,"native hue and square change the selected color");ok=picked&&ok;
    click(850,570);SdgoRgbPaintObservationV1 cleared;ObservePanel(cleared);
    context->Assert(!(cleared.present_mask&1),"native clear restores selected material");ok=!(cleared.present_mask&1)&&ok;
    click(940,570);SdgoRgbPaintObservationV1 restored;ObservePanel(restored);
    const bool reset=restored.present_mask==originalState.present_mask&&std::memcmp(restored.rgb,originalState.rgb,sizeof restored.rgb)==0;
    context->Assert(reset,"native restore returns all six regions to entry colors");ok=reset&&ok;
    click(940,640);SdgoRgbPaintObservationV1 nativeMode;ObservePanel(nativeMode);
    context->Assert(!nativeMode.editing,"return to stock palette disables RGB editing");ok=!nativeMode.editing&&ok;
    click(940,640);SdgoRgbPaintObservationV1 reopened;ObservePanel(reopened);
    const bool fresh=reopened.editing&&reopened.present_mask==originalState.present_mask&&std::memcmp(reopened.rgb,originalState.rgb,sizeof reopened.rgb)==0;
    context->Assert(fresh,"reopening RGB starts with current native paint");ok=fresh&&ok;
    const wchar_t* colors[]={L"FF0048",L"FFE0F5",L"FF9BDA",L"FFC0C0",L"FFC0C0",L"FF0080"};
    for(unsigned i=0;i<6;++i){select(i);click(850,530);type(colors[i]);SdgoRgbPaintObservationV1 row;ObservePanel(row);auto* c=At<void*>(page,0x14c+i*4);ownerApi->Log(ownerMod,SDGO_LOG_INFO,"RGB region input=%u selected=%u rect=%.0f,%.0f,%d,%d rgb=%u,%u,%u",i,row.selected,At<float>(c,0x148),At<float>(c,0x14c),At<int>(c,0x154),At<int>(c,0x158),row.rgb[i][0],row.rgb[i][1],row.rgb[i][2]);}
    select(0);SdgoRgbPaintObservationV1 result;ObservePanel(result);
    bool six=result.present_mask==63&&result.selected==0;
    for(unsigned i=0;i<6;++i)six=six&&result.rgb[i][0]==palette[i].r&&result.rgb[i][1]==palette[i].g&&result.rgb[i][2]==palette[i].b;
    context->Assert(six,"six native paint regions retain independently entered RGB hex colors");ok=six&&ok;
    ownerApi->Log(ownerMod,SDGO_LOG_INFO,"RGB panel input validation=%s frames=%u",ok?"pass":"fail",result.frames);
    if(ok)regions=63;
    return ok;
}
