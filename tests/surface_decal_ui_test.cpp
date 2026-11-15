#include "../mods/unit_appearance_client/decal/native_ui.h"
#include <cstring>
#include <cstdlib>
#include <iostream>
using namespace sdgo::surface_decal;
using String=sdgo::hangar::ui::NativeString;
namespace {
unsigned checks=0,queries=0;int buttonTable=0;
std::array<unsigned char,0x300> service{},dialog{},preview{},button{};
void* result=nullptr;
template<class T,class A>void Put(A& a,unsigned offset,T value){std::memcpy(a.data()+offset,&value,sizeof value);}
void Check(bool ok,const char* why){++checks;if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
void* __fastcall Find(void* root,void*,const String* name,int kind){
    ++queries;Check(root==dialog.data(),"lookup uses the live page root");
    const auto* text=name->capacity<8?name->data.small:name->data.pointer;
    Check(!std::wcscmp(text,kind==16?L"Multi3DRender":L"SaveBtn"),"native lookup preserves short and long string ABI");
    return result;
}
void* Symbol(SdgoMod*,const char* name){
    if(!std::strcmp(name,"hangar.ui_button_vtable"))return &buttonTable;
    return reinterpret_cast<void*>(&Find); // other UI functions are never invoked
}
}
int main(){
    for(float width:{260.f,274.f,320.f})for(auto tab:{ui::Tab::layers,ui::Tab::properties,ui::Tab::library}){
        ui::LayerLayout layout;Check(layout.Set(width,530),"twenty-layer panel fits native dimensions");
        std::vector<ui::Rect> controls(layout.tabs.begin(),layout.tabs.end());
        if(tab==ui::Tab::properties){for(auto r:{layout.place,layout.orbit,layout.rotation,layout.size,layout.wear,layout.wrap,layout.pattern,layout.visible})controls.push_back(r);}
        else{controls.insert(controls.end(),layout.rows.begin(),layout.rows.end());controls.push_back(layout.previous);controls.push_back(layout.next);
            if(tab==ui::Tab::layers){controls.insert(controls.end(),layout.tools.begin(),layout.tools.end());controls.insert(controls.end(),layout.ordering.begin(),layout.ordering.end());controls.push_back(layout.hideNative);}}
        controls.push_back(layout.save);controls.push_back(layout.cancel);
        for(std::size_t i=0;i<controls.size();++i){const auto& r=controls[i];Check(r.x>=0&&r.y>=0&&r.x+r.w<=width&&r.y+r.h<=530,"active-tab controls remain bounded");
            for(std::size_t j=0;j<i;++j)Check(!r.Overlaps(controls[j]),"no twenty-layer active-tab controls overlap");}
        layers::Design design;design.unitSerial=11;design.unitType=10002;
        for(unsigned i=1;i<=20;++i){layers::Layer item;item.id=i;item.sticker=7;item.placed=true;design.items.push_back(item);}
        ui::LayerGesture gesture;auto c=gesture.Down(layout,ui::Tab::layers,14,40,layout.rows[5].y+10,design,1,false);
        Check(c.action==ui::Action::select&&c.slot==1,"scrolled duplicate artwork selects its stable layer ID");
        c=gesture.Move(layout,0,20,40,layout.rows[0].y+10);Check(c.action==ui::Action::reorder&&c.slot==1&&c.value==19,"captured drag can scroll from twentieth layer to top");
        gesture.Cancel();Check(!gesture.Captured(),"layer drag releases capture");
        c=gesture.Down(layout,ui::Tab::properties,0,layout.wear.x+layout.wear.w/2,layout.wear.y+5,design,19,false);
        Check(c.action==ui::Action::wear&&c.slot==19&&std::abs(c.value-.5)<1e-6,"selected independent layer owns its wear slider");
        c=gesture.Down(layout,ui::Tab::properties,0,layout.wear.x,layout.wear.y+5,design,19,true);Check(!gesture.Captured(),"quote/commit locks parameter gestures");
    }
    for(float width:{260.f,274.f,320.f})for(float height:{530.f,560.f,640.f}){
        ui::Layout layout;Check(layout.Set(width,height),"native side-panel dimensions are supported");
        std::vector<ui::Rect> controls(layout.rows.begin(),layout.rows.end());controls.insert(controls.end(),layout.ordering.begin(),layout.ordering.end());
        for(auto rect:{layout.place,layout.orbit,layout.rotation,layout.size,layout.wear,layout.wrap,layout.pattern,layout.save,layout.cancel})controls.push_back(rect);
        for(std::size_t i=0;i<controls.size();++i){const auto& r=controls[i];Check(r.x>=0&&r.y>=0&&r.x+r.w<=width&&r.y+r.h<=height,"interactive controls stay inside the native panel");
            for(std::size_t j=0;j<i;++j)Check(!r.Overlaps(controls[j]),"editor controls never overlap each other");}
        Appearance a;a.unitSerial=11;a.unitType=10001;a.slots[0].sticker=7;a.slots[1].sticker=7;a.slots[2].sticker=8;
        a.slots[0].positioned=true;a.slots[0].placement.anchor.geometry=1;
        ui::Gesture gesture;auto c=gesture.Down(layout,layout.rows[2].x+60,layout.rows[2].y+20,a,0,false);
        Check(c.action==ui::Action::slot0&&c.slot==0,"drag uses native slot identity despite duplicate sticker artwork");
        c=gesture.Move(layout,layout.rows[0].x+60,layout.rows[0].y+20);
        Check(c.action==ui::Action::reorder&&c.slot==0&&c.value==2,"bottom layer can move directly to top");
        gesture.Cancel();Check(!gesture.Captured(),"release/focus loss ends layer capture");
        c=gesture.Down(layout,layout.wear.x+layout.wear.w/2,layout.wear.y+8,a,0,false);
        Check(c.action==ui::Action::wear&&std::abs(c.value-.5)<1e-6,"wear slider tracks real local position");
        c=gesture.Move(layout,-100,layout.wear.y);Check(c.value==0,"captured slider clamps outside left edge");
        c=gesture.Move(layout,5000,layout.wear.y);Check(c.value==1,"captured slider clamps outside right edge");
        gesture.Cancel();gesture.Down(layout,layout.rows[0].x+5,layout.rows[0].y+5,a,0,true);Check(!gesture.Captured(),"pending save cannot start another editing gesture");
    }
    ui::Page page;SdgoApi api{};api.Symbol=Symbol;Check(page.Bind(&api,nullptr),"bind native lookup");
    // Reproduce the reported construction state: stale UTF-16 bytes occupied
    // the cached preview field before the factory became active.
    Put(service,0xd4,0x72006900u);Put(service,0xd0,reinterpret_cast<void*>(1));
    Check(!page.Ready(service.data())&&!page.Preview(service.data())&&queries==0,"inactive page never dereferences uninitialized controls");
    Put<unsigned char>(service,0xac,1);
    Check(!page.Ready(service.data())&&!page.Preview(service.data()),"unreadable pending root fails closed");
    Put(service,0xd0,dialog.data());
    Check(!page.Preview(service.data())&&queries==0,"hidden dialog never searches its controls");
    Put<unsigned char>(dialog,0x162,1);result=preview.data();
    Put(preview,0x15c,dialog.data());Put(preview,0x134,16u);
    Check(page.Preview(service.data())==preview.data(),"ready page resolves the actual named preview instead of stale cached bytes");
    Put(preview,0x15c,service.data());Check(!page.Preview(service.data()),"foreign dialog control rejected");
    Put(preview,0x15c,dialog.data());Put(preview,0x134,4u);Check(!page.Preview(service.data()),"wrong control type rejected");
    result=button.data();Put(button,0,&buttonTable);Put(button,0x15c,dialog.data());Put(button,0x134,4u);
    Check(page.Footer(service.data(),0)==button.data(),"footer resolves by native control name");
    result=nullptr;Check(!page.Preview(service.data()),"missing preview waits for native initialization");
    Put<unsigned char>(service,0xac,0);const auto before=queries;
    Check(!page.Preview(service.data())&&queries==before,"page exit stops all control access");
    Put<unsigned char>(service,0xac,1);result=preview.data();Put(preview,0x134,16u);
    Check(page.Preview(service.data())==preview.data(),"re-entered page resolves fresh controls");
    std::cout<<checks<<" checks passed\n";
}
