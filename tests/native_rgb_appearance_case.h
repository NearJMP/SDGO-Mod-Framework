// Uses the actual RGB page, material observer and authenticated save path.
const sdgo::rgb_paint::Rgb palette[]={{255,0,72},{255,224,245},{255,155,218},{255,192,192},{255,192,192},{255,0,128}};
unsigned regions=0;
bool ObservePanel(SdgoRgbPaintObservationV1& out){
    const auto observe=reinterpret_cast<SdgoRgbPaintObserveV1Fn>(GetProcAddress(GetModuleHandleW(L"unit_appearance_client.dll"),SDGO_RGB_PAINT_OBSERVE_EXPORT));
    return observe&&observe(&out)&&out.visible&&out.frames>0&&out.unit;
}
#include "native_rgb_panel_actions.h"
int RgbAppearanceCase(const SdgoGameTestContextV1* context,void* page){
    SdgoRgbPaintObservationV1 value;if(!ObservePanel(value)||!value.authority_ready)return SDGO_GAME_TEST_WAIT;
    static unsigned phase=0;static std::uint64_t revision=0;
    const auto save=[](){const auto app=reinterpret_cast<void*(__cdecl*)()>(ownerApi->Symbol(ownerMod,"game.app"));
        if(!app||!app())return false;const auto hwnd=At<HWND>(app(),0x3c);RECT r{};if(!GetClientRect(hwnd,&r)||r.right<=0||r.bottom<=0)return false;
        const auto point=MAKELPARAM(825*r.right/1024,642*r.bottom/768);SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(hwnd,WM_LBUTTONUP,0,point);return true;};
    if(!phase){revision=value.revision;if(!ExercisePanel(context,page))return SDGO_GAME_TEST_FAIL;phase=1;}
    else if(phase==1){const auto capture=CaptureScene(L"rgb-edited");if(capture<1)return capture<0?SDGO_GAME_TEST_FAIL:SDGO_GAME_TEST_WAIT;
        if(!save())return SDGO_GAME_TEST_FAIL;SdgoRgbPaintObservationV1 confirm;ObservePanel(confirm);context->Assert(confirm.state==2,"RGB save keeps its own preview confirmation");if(confirm.state!=2)return SDGO_GAME_TEST_FAIL;
        if(!save())return SDGO_GAME_TEST_FAIL;phase=2;}
    else if(phase==2){if(value.state==3||value.state==4||value.revision<=revision)return SDGO_GAME_TEST_WAIT;
        bool exact=value.saved_mask==63&&value.matched_material_mask==63&&value.draws>0;
        for(unsigned i=0;i<6;++i)exact=exact&&value.saved_rgb[i][0]==palette[i].r&&value.saved_rgb[i][1]==palette[i].g&&value.saved_rgb[i][2]==palette[i].b;
        context->Assert(exact,"all six real RGB material regions match the acknowledged saved palette");if(!exact)return SDGO_GAME_TEST_FAIL;phase=3;}
    else{const auto capture=CaptureScene(L"rgb-confirmed");if(capture<1)return capture<0?SDGO_GAME_TEST_FAIL:SDGO_GAME_TEST_WAIT;return SDGO_GAME_TEST_PASS;}
    return SDGO_GAME_TEST_WAIT;
}
