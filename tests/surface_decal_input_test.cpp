// Exercise the production window input path and skinless editor controls.
// No game process, renderer, account or network is started.
#include "hangar_page_fixture.h"
#include "native_button_replay.h"
#include "../mods/unit_appearance_client/decal/native_ui.h"
#include <iostream>
#include <commctrl.h>
#include <windowsx.h>
#include <mutex>
namespace {
using page_fixture::Put;using page_fixture::Get;using page_fixture::Pointer;
namespace ui=sdgo::surface_decal::ui;
namespace layers=sdgo::surface_decal::layers;
using Draw=void(__thiscall*)(void*,void*);
using Mouse=uint8_t(__thiscall*)(void*,uint32_t,int,int,uint32_t,uint32_t);
using Update=uint32_t(__thiscall*)(void*);
ui::Page page;int clicks=0,labels=0;ui::Action last=ui::Action::none;
std::recursive_mutex gate;HWND inputWindow=nullptr;bool pointerOwned=false,editing=true,placementPointerOwned=false,dragging=false,mouseDown=false,placing=false,armed=false;
constexpr UINT_PTR inputSubclass=731;
std::vector<ui::Command> commands;
std::vector<std::pair<double,double>> placements;bool placementResult=true;
bool PlaceAt(double x,double y){placements.push_back({x,y});return placementResult;}
bool PreviewContains(float x,float y){return ui::Rect{600,100,350,500}.Contains(x,y);}
void ApplyCommand(const ui::Command& command){if(command.action!=ui::Action::none)commands.push_back(command);}
void Action(ui::Action action){commands.push_back({action,0,0});}
#include "../mods/unit_appearance_client/decal/input.inl"
void* preview=nullptr;bool inputHeld=false;std::uint8_t previewInput=0,previewPitch=0;
struct LastErrorGuard {DWORD value=GetLastError();~LastErrorGuard(){SetLastError(value);}};
template<class T>T Read(const void* p,unsigned offset=0){return Get<T>(p,offset);}
void RestoreInput(){inputHeld=false;preview=nullptr;}
#include "../mods/unit_appearance_client/decal/preview_input.inl"
std::array<void*,34> buttons{};std::array<void*,16> roots{};
void __fastcall RootDraw(void*,void*,void*){}
void __fastcall LabelDraw(void* control,void*,void*) {
    assert(Get<void*>(control,0x1a4)==Get<void*>(control,0x1a8));++labels;
}
void __fastcall Text(void*,void*,int,const page_fixture::NativeString*,int,int,int,int,uint32_t,uint32_t,uint8_t,uint32_t,int){}
void* Symbol(SdgoMod*,const char* name){
    if(!strcmp(name,"hangar.ui_add_button"))return reinterpret_cast<void*>(&page_fixture::Add);
    if(!strcmp(name,"hangar.ui_find_control"))return reinterpret_cast<void*>(&page_fixture::Find);
    if(!strcmp(name,"hangar.ui_text_item"))return reinterpret_cast<void*>(&Text);
    if(!strcmp(name,"hangar.ui_add_skin"))return reinterpret_cast<void*>(&page_fixture::Skin);
    if(!strcmp(name,"hangar.ui_button_vtable"))return buttons.data();
    return nullptr;
}
void __fastcall Base(void*,void*,uint32_t,int,int,uint32_t,uint32_t){}
void __fastcall Event(void* owner,void*,uint32_t event,uint32_t force,void* control){
    assert(owner==page_fixture::dialog.data()&&force==1);
    if(event==0x101){last=page.Click(control);assert(last!=ui::Action::none);++clicks;}
}
void __fastcall Focus(void*,void*,void* control){Put<uint8_t>(control,0x165,1);}
uint32_t __fastcall NoDraw(void*,void*){return 1;}
int __fastcall Hit(void* control,void*,int x,int y){return x>=0&&y>=0&&x<Get<int>(control,0x154)&&y<Get<int>(control,0x158);}
void Setup(){
    buttons[13]=reinterpret_cast<void*>(&LabelDraw);buttons[24]=reinterpret_cast<void*>(&Hit);
    page_fixture::buttonVtable=buttons.data();page_fixture::Setup();
    for(unsigned i=1;i<=14;++i)roots[i]=reinterpret_cast<void*>(&RootDraw);
    Put(page_fixture::dialog.data(),0,roots.data()+1);
    Put(page_fixture::dialog.data(),0x154,274);Put(page_fixture::dialog.data(),0x158,530);
    Put(page_fixture::service.data(),0xd0,page_fixture::dialog.data());Put<uint8_t>(page_fixture::service.data(),0xac,1);
}
void Skins(void* control){
    auto** first=Get<void**>(control,0x1a4);assert(first&&Get<void**>(control,0x1a8)==first+3);
    for(unsigned i=0;i<3;++i)assert(first[i]&&Get<void*>(first[i],0));
}
}
int wmain(int argc,wchar_t** argv){
    try{
        std::array<std::uint8_t,0x300> previewA{},previewB{};
        AcquirePreviewInput(previewA.data());assert(inputHeld&&preview==previewA.data());
        assert(previewA[0x263]&&previewA[0x264]); // no model vector exists yet
        placing=true;placementPointerOwned=pointerOwned=true;
        previewA[0x262]=1;PreviewInput(previewA.data());assert(!previewA[0x262]); // left places, never rotates
        previewA[0x261]=1;PreviewInput(previewA.data());assert(previewA[0x262]); // right still orbits
        placementPointerOwned=false;PreviewInput(previewA.data());assert(!previewA[0x261]&&!previewA[0x262]); // panel owns pointer
        pointerOwned=false;placing=false;previewA[0x262]=1;PreviewInput(previewA.data());assert(previewA[0x262]);
        AcquirePreviewInput(previewB.data());assert(preview==previewB.data()&&previewB[0x263]&&previewB[0x264]);
        placing=true;previewA[0x262]=1;PreviewInput(previewA.data());assert(previewA[0x262]); // stale control untouched
        previewB[0x262]=1;PreviewInput(previewB.data());assert(!previewB[0x262]);
        RestoreInput();placing=false;
        Setup();auto& p=page;SdgoApi api{};api.Symbol=&Symbol;api.host_base=GetModuleHandleW(nullptr);
        assert(p.Bind(&api,nullptr)&&p.Attach(page_fixture::service.data(),Pointer(L"UnitDeleteBtn")));
        Put(page_fixture::dialog.data(),0x148,300.f);Put(page_fixture::dialog.data(),0x14c,100.f);
        layers::Design design;design.unitSerial=11;design.unitType=10002;
        auto render=[&](bool busy=false,bool dirty=false,bool quoted=false){p.Render(editing,false,design.items.empty()?0:design.items.back().id,busy,dirty,quoted,&design,L"");};
        render();Skins(Pointer(L"SurfaceOpen"));assert(page_fixture::skinCalls==3);
        const auto noEditorSkins=[&](){for(const auto& pair:page_fixture::children)if(pair.first.rfind(L"Surface",0)==0&&pair.first!=L"SurfaceOpen"){
            auto* c=pair.second->memory.data();assert(Get<void*>(c,0x1a4)==Get<void*>(c,0x1a8));assert(!Get<uint8_t>(c,0x168));
        }};
        noEditorSkins();const auto before=labels;Get<Draw>(Get<void*>(page_fixture::dialog.data(),0),13*4)(page_fixture::dialog.data(),nullptr);
        assert(labels>before);noEditorSkins();
        WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"DecalInputRegression";
        assert(RegisterClassW(&cls));inputWindow=CreateWindowW(cls.lpszClassName,L"",WS_POPUP,0,0,1280,960,nullptr,nullptr,cls.hInstance,nullptr);assert(inputWindow);
        assert(SetWindowSubclass(inputWindow,&InputProc,inputSubclass,0));
        const auto send=[&](UINT message,float x,float y,WPARAM heldButtons=0){SendMessageW(inputWindow,message,message==WM_LBUTTONDOWN?MK_LBUTTON|heldButtons:heldButtons,MAKELPARAM(int(x*1.25f),int(y*1.25f)));};
        const auto point=[&](const wchar_t* name){auto* c=Pointer(name);return std::pair<float,float>{300+Get<float>(c,0x148)+Get<int>(c,0x154)/2.f,100+Get<float>(c,0x14c)+Get<int>(c,0x158)/2.f};};
        const auto click=[&](const wchar_t* name,ui::Action action,unsigned slot=0){const auto xy=point(name);const auto n=commands.size();
            send(WM_LBUTTONDOWN,xy.first,xy.second);assert(pointerOwned&&p.Captured());
            send(WM_LBUTTONUP,xy.first,xy.second);assert(!pointerOwned&&!p.Captured());
            if(action==ui::Action::none)assert(commands.size()==n);
            else{assert(commands.size()==n+1&&commands.back().action==action);if(slot)assert(commands.back().slot==slot);}
        };
        click(L"SurfaceTab0",ui::Action::layers);click(L"SurfaceTab1",ui::Action::properties);click(L"SurfaceTab2",ui::Action::library);
        click(L"SurfaceAdd",ui::Action::add);click(L"SurfaceNative",ui::Action::hideNative);click(L"SurfaceCancel",ui::Action::cancel);
        click(L"SurfaceDuplicate",ui::Action::none);click(L"SurfaceSave",ui::Action::none);
        auto xy=point(L"SurfaceCancel");auto count=commands.size();send(WM_LBUTTONDOWN,xy.first,xy.second);send(WM_LBUTTONUP,900,740);assert(commands.size()==count&&!pointerOwned);
        // Releasing over another control, losing focus/capture, or leaving the page cannot click.
        send(WM_LBUTTONDOWN,xy.first,xy.second);auto tab=point(L"SurfaceTab2");send(WM_LBUTTONUP,tab.first,tab.second);assert(commands.size()==count);
        for(auto message:{WM_KILLFOCUS,WM_CANCELMODE,WM_CAPTURECHANGED}){send(WM_LBUTTONDOWN,xy.first,xy.second);SendMessageW(inputWindow,message,0,0);send(WM_LBUTTONUP,xy.first,xy.second);assert(commands.size()==count&&!pointerOwned);}
        send(WM_LBUTTONDOWN,xy.first,xy.second);editing=false;render();send(WM_LBUTTONUP,xy.first,xy.second);assert(commands.size()==count&&!pointerOwned);editing=true;render();
        // Blank space consumes the complete click; the preview outside remains available.
        send(WM_LBUTTONDOWN,320,190);assert(pointerOwned);send(WM_LBUTTONUP,320,190);assert(commands.size()==count);
        send(WM_LBUTTONDOWN,900,300);assert(!pointerOwned);send(WM_LBUTTONUP,900,300);
        p.SetStock({{7,3,false}},{});assert(p.Navigate(ui::Action::library));render();click(L"SurfaceRow0",ui::Action::choose,7);
        xy=point(L"SurfaceRow0");count=commands.size();send(WM_LBUTTONDOWN,xy.first,xy.second);p.SetStock({{8,3,false}},{});render();send(WM_LBUTTONUP,xy.first,xy.second);assert(commands.size()==count);
        layers::Layer layer;layer.id=1;layer.sticker=7;layer.placed=true;design.items.push_back(layer);
        assert(p.Navigate(ui::Action::layers));render(false,true);
        click(L"SurfaceRow0",ui::Action::select,1);click(L"SurfaceDuplicate",ui::Action::duplicate);click(L"SurfaceRemove",ui::Action::remove);click(L"SurfaceTop",ui::Action::top);click(L"SurfaceSave",ui::Action::save);
        assert(p.Navigate(ui::Action::properties));render(false,true);
        click(L"SurfacePlace",ui::Action::place);click(L"SurfaceWrap",ui::Action::wrap);click(L"SurfacePattern",ui::Action::pattern);
        const auto r=p.Geometry().wear;count=commands.size();send(WM_LBUTTONDOWN,300+r.x+r.w/2,100+r.y+5);assert(pointerOwned&&commands.back().action==ui::Action::wear);send(WM_MOUSEMOVE,100,100+r.y+5);assert(commands.back().value==0);send(WM_LBUTTONUP,100,100+r.y+5);assert(!pointerOwned&&commands.size()==count+2);
        render(true,true);click(L"SurfaceTab2",ui::Action::none);click(L"SurfaceCancel",ui::Action::none);
        render(false,true,true);click(L"SurfaceTab2",ui::Action::none);click(L"SurfaceSave",ui::Action::save);click(L"SurfaceCancel",ui::Action::cancel);
        // A complete short preview click between frames must reach placement.
        render();placing=true;count=commands.size();
        send(WM_LBUTTONDOWN,800,300);assert(pointerOwned&&placementPointerOwned&&dragging&&placements.size()==1);
        assert(placements.back().first==1000&&placements.back().second==375); // viewport/client pixels, not logical panel coordinates
        send(WM_LBUTTONUP,800,300);assert(!pointerOwned&&!placementPointerOwned&&commands.size()==count&&placements.size()==1);
        placementResult=false;send(WM_LBUTTONDOWN,800,300);assert(pointerOwned&&!dragging&&placements.size()==2);
        placementResult=true;send(WM_MOUSEMOVE,810,310,MK_LBUTTON);assert(dragging&&placements.size()==3);
        send(WM_MOUSEMOVE,820,310,MK_LBUTTON|MK_RBUTTON);assert(placements.size()==3); // right-button orbit never stamps
        send(WM_LBUTTONUP,980,700);assert(!pointerOwned&&!placementPointerOwned);
        send(WM_LBUTTONDOWN,800,300,MK_RBUTTON);assert(!pointerOwned&&placements.size()==3);send(WM_LBUTTONUP,800,300);
        armed=true;send(WM_LBUTTONDOWN,800,300);assert(!pointerOwned&&placements.size()==3);send(WM_LBUTTONUP,800,300);armed=false;
        placing=false;send(WM_LBUTTONDOWN,800,300);assert(!pointerOwned&&placements.size()==3);send(WM_LBUTTONUP,800,300);placing=true;
        send(WM_LBUTTONDOWN,980,700);assert(!pointerOwned&&placements.size()==3);send(WM_LBUTTONUP,980,700); // outside native preview
        for(auto message:{WM_KILLFOCUS,WM_CANCELMODE,WM_CAPTURECHANGED}){
            send(WM_LBUTTONDOWN,800,300);const auto n=placements.size();SendMessageW(inputWindow,message,0,0);
            send(WM_MOUSEMOVE,810,310,MK_LBUTTON);send(WM_LBUTTONUP,810,310);assert(!pointerOwned&&!placementPointerOwned&&placements.size()==n);
        }
        placing=false;noEditorSkins();DestroyWindow(inputWindow);assert(!inputWindow);
        if(argc==2){
            const auto file=room_title_native::detail::ReadFile(argv[1]);
            if(room_title_native::detail::Sha256(file.data(),file.size())!="e2dba73c10302b50cb0e6f2e58dd0a14effa8d224989ae570a7f8e05af79ff92")throw std::runtime_error("unsupported client");
            using native_button_replay::Snippet;
            Snippet mouse(file,0xC69080,512,"e69debfef6104196f8d5008caa9b157036d027efdba76fef166f0ba243b92c5d");
            mouse.Call(0x1D,&Base);mouse.Call(0x101,&Focus);for(size_t offset:{0x7Au,0x9Cu,0x174u,0x1ABu})mouse.Call(offset,&Event);mouse.MouseTable();mouse.Seal();
            Snippet update(file,0xC6B400,548,"7f59b79ffa250df33f572d7fb392c20ad17325d550466300cd9792e8b8873341");
            for(size_t offset:{0x3u,0x214u,0x21Bu})update.Call(offset,&NoDraw);update.Seal();
            auto nativeMouse=mouse.Function<Mouse>();auto nativeUpdate=update.Function<Update>();
            const auto cycle=[&](void* c){nativeMouse(c,WM_MOUSEMOVE,-1,-1,0,0);nativeUpdate(c);nativeMouse(c,WM_MOUSEMOVE,4,4,0,0);nativeUpdate(c);nativeMouse(c,WM_LBUTTONDOWN,4,4,0,0);nativeUpdate(c);nativeMouse(c,WM_LBUTTONUP,4,4,0,0);};
            render();cycle(Pointer(L"SurfaceTab2"));assert(clicks==0); // No duplicate native click on a custom button.
            editing=false;render();cycle(Pointer(L"SurfaceOpen"));assert(clicks==1&&last==ui::Action::open);
            std::cout<<"Native entry still clicks; skinless editor cannot emit duplicate native clicks.\n";
        }
        p.Detach();Setup();page_fixture::failSkin=true;ui::Page failed;
        assert(failed.Bind(&api,nullptr)&&!failed.Attach(page_fixture::service.data(),Pointer(L"UnitDeleteBtn")));
        std::cout<<"Production window messages passed: scaled coordinates, buttons, stock rows, sliders, capture, cancellation, disabled states; no editor sprites loaded.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
