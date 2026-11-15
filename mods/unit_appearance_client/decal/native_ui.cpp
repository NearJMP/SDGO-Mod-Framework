#include "native_ui.h"
#include "canvas.h"
#include <windows.h>
#include <cstring>
#include <cmath>

namespace sdgo::surface_decal::ui {
namespace {
using String=sdgo::hangar::ui::NativeString;
template<class T>T Read(const void* p,unsigned offset=0){T v{};std::memcpy(&v,static_cast<const std::uint8_t*>(p)+offset,sizeof v);return v;}
template<class T>void Put(void* p,unsigned offset,T value){std::memcpy(static_cast<std::uint8_t*>(p)+offset,&value,sizeof value);}
String View(const std::wstring& value){String s;s.length=static_cast<std::uint32_t>(value.size());if(s.length<8)std::memcpy(s.data.small,value.c_str(),(s.length+1)*2);else{s.capacity=s.length;s.data.pointer=value.c_str();}return s;}
constexpr DWORD text=0xffd7e8ef,muted=0xff7f9baa,accent=0xff77dcf0;
std::wstring FitLabel(const std::wstring& value,unsigned limit=18){
    unsigned columns=0;for(auto ch:value)columns+=ch<128?1:2;if(columns<=limit)return value;
    columns=0;std::size_t count=0;for(;count<value.size();++count){const unsigned width=value[count]<128?1:2;if(columns+width+2>limit)break;columns+=width;}return value.substr(0,count)+L"…";
}
}
Page* Page::drawing_=nullptr;
bool Page::Bind(const SdgoApi* api,SdgoMod* mod){
    using B=sdgo::hangar::ui::Bindings;api_=api;mod_=mod;
    bind_.add=reinterpret_cast<B::Add>(api->Symbol(mod,"hangar.ui_add_button"));bind_.find=reinterpret_cast<B::Find>(api->Symbol(mod,"hangar.ui_find_control"));
    bind_.text=reinterpret_cast<B::Text>(api->Symbol(mod,"hangar.ui_text_item"));bind_.skin=reinterpret_cast<B::Skin>(api->Symbol(mod,"hangar.ui_add_skin"));
    bind_.buttonVtable=api->Symbol(mod,"hangar.ui_button_vtable");renderer_=static_cast<void**>(api->Symbol(mod,"surface_decal.renderer"));
    return bind_.add&&bind_.find&&bind_.text&&bind_.skin&&bind_.buttonVtable;
}
bool Page::Ready(void* service)const noexcept{
    __try{if(!service||!Read<std::uint8_t>(service,0xac))return false;auto* root=Read<void*>(service,0xd0);return root&&Read<std::uint8_t>(root,0x162)!=0;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void* Page::FactoryControl(void* service,const wchar_t* name,int kind)const noexcept{
    __try{
        if(!bind_.find||!Ready(service))return nullptr;auto* root=Read<void*>(service,0xd0);
        String key;key.length=static_cast<unsigned>(std::wcslen(name));if(key.length<8)std::memcpy(key.data.small,name,(key.length+1)*2);else{key.data.pointer=name;key.capacity=key.length;}
        auto* child=bind_.find(root,&key,kind);
        if(!child||Read<void*>(child,0x15c)!=root||Read<unsigned>(child,0x134)!=static_cast<unsigned>(kind))return nullptr;
        if(kind==4&&Read<void*>(child)!=bind_.buttonVtable)return nullptr;return child;
    }__except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
}
void Page::RestoreDraw()noexcept{
    __try{if(root_&&Read<void**>(root_)==&drawTable_[1])Put(root_,0,originalDrawTable_);}
    __except(EXCEPTION_EXECUTE_HANDLER){}
}
void Page::Detach(){RestoreDraw();if(drawing_==this)drawing_=nullptr;service_=root_=table_=nullptr;originalDraw_=nullptr;originalDrawTable_=nullptr;controls_.clear();originals_.clear();editing_=false;gesture_.Cancel();}
void Page::Position(ControlState& c,Rect r){c.rect=r;c.width=static_cast<int>(r.w);Put<float>(c.pointer,0x148,r.x);Put<float>(c.pointer,0x14c,r.y);Put<int>(c.pointer,0x154,c.width);Put<int>(c.pointer,0x158,static_cast<int>(r.h));}
bool Page::Add(void* source,const wchar_t* name,const wchar_t* caption,Action action,int x,int y,int width,bool edit,int height){
    const std::wstring key=name;const auto native=View(key);bind_.add(root_,&native,x,y,width,height,0);
    auto* control=bind_.find(root_,&native,4);if(!control||Read<void*>(control)!=bind_.buttonVtable||Read<void*>(control,0x15c)!=root_)return false;
    Put<std::uint8_t>(control,0x162,0);Put<std::uint8_t>(control,0x167,0);Put<std::uint8_t>(control,0x168,0);
    ControlState state;state.pointer=control;state.action=action;state.width=width;state.edit=edit;state.name=name;state.rect={float(x),float(y),float(width),float(height)};state.color=text;controls_.push_back(std::move(state));
    // Only the native-mode entry retains the original fixed-width sprite.
    if(action==Action::open){auto** skins=Read<std::uint8_t**>(source,0x1a4);auto** end=Read<std::uint8_t**>(source,0x1a8);
        if(!skins||end-skins<3||end-skins>16)return false;for(unsigned i=0;i<3;++i){if(!skins[i])return false;bind_.skin(control,reinterpret_cast<String*>(skins[i]+4),Read<std::uint8_t>(skins[i],0x44),Read<std::uint32_t>(skins[i],0x3c),Read<std::uint32_t>(skins[i],0x40));}}
    Text(controls_.back(),caption);return true;
}
void Page::Text(ControlState& c,const std::wstring& caption){
    if(c.caption==caption&&c.renderedColor==c.color)return;
    const auto s=View(caption);const bool row=c.name.rfind(L"SurfaceRow",0)==0;const bool hint=c.name.rfind(L"SurfaceHint",0)==0;const int left=row?44:3;
    bind_.text(c.pointer,0,&s,left,row||hint?1:4,c.width-left-3,16,c.color,0xff07131c,0,font_,row||c.action==Action::none?1:2);
    c.caption=caption;c.renderedColor=c.color;
}
bool Page::AdoptDraw(){
    if(!api_||!api_->host_base||!root_)return false;auto** vtable=Read<void**>(root_);
    auto* base=static_cast<std::uint8_t*>(api_->host_base);const auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);const auto* nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    std::uintptr_t lo=0,hi=0;const auto* section=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)if(section[i].Characteristics&IMAGE_SCN_MEM_EXECUTE){lo=reinterpret_cast<std::uintptr_t>(base)+section[i].VirtualAddress;hi=lo+section[i].Misc.VirtualSize;break;}
    if(!lo)return false;unsigned count=0;while(count<160){const auto p=reinterpret_cast<std::uintptr_t>(vtable[count]);if(p<lo||p>=hi)break;++count;}if(count<=13)return false;
    drawTable_[0]=vtable[-1];std::memcpy(&drawTable_[1],vtable,count*sizeof(void*));originalDrawTable_=vtable;originalDraw_=reinterpret_cast<NativeDraw>(vtable[13]);drawTable_[14]=reinterpret_cast<void*>(&DrawHook);
    drawing_=this;Put(root_,0,&drawTable_[1]);return true;
}
unsigned Page::Count()const{return armed_?unsigned(costs_.size()):tab_==Tab::library?unsigned(stock_.size()):unsigned(appearance_.items.size());}
unsigned& Page::Offset(){return armed_?quoteOffset_:tab_==Tab::library?stockOffset_:offset_;}
std::wstring Page::Label(unsigned sticker)const{auto label=name_?name_(sticker):std::wstring{};return label.empty()?L"纹章 "+std::to_wstring(sticker):label;}
void Page::SetStock(const std::vector<layers::Stock>& stock,layers::Policy policy){stock_=stock;policy_=policy;}
bool Page::Navigate(Action action){
    if(!armed_&&(action==Action::layers||action==Action::properties||action==Action::library)){tab_=action==Action::layers?Tab::layers:action==Action::properties?Tab::properties:Tab::library;gesture_.Cancel();return true;}
    if(action==Action::previous||action==Action::next){auto& at=Offset();const auto last=Count()>layout_.visibleRows?Count()-layout_.visibleRows:0;
        at=action==Action::previous?(at>layout_.visibleRows?at-layout_.visibleRows:0):std::min(last,at+layout_.visibleRows);return true;}return false;
}
bool Page::Scroll(float x,float y,int lines){
    if(!editing_||!Contains(x,y)||(!armed_&&tab_==Tab::properties)||!lines)return false;
    auto& at=Offset();const auto last=Count()>layout_.visibleRows?Count()-layout_.visibleRows:0;at=unsigned(std::clamp(int(at)-lines,0,int(last)));return true;
}
bool Page::Attach(void* service,void* source){
    if(service_==service&&root_==Read<void*>(service,0xd0))return true;Detach();
    if(!service||!source||Read<void*>(source)!=bind_.buttonVtable)return false;auto* root=Read<void*>(service,0xd0);
    if(!root||!layout_.Set(float(Read<int>(root,0x154)),float(Read<int>(root,0x158))))return false;
    service_=service;root_=root;table_=Read<void*>(service);tab_=Tab::layers;offset_=stockOffset_=0;
    auto* head=Read<void*>(root,0x23c);auto* link=head?Read<void*>(head):nullptr;
    for(unsigned i=0;link&&link!=head&&i<256;++i){auto* c=Read<void*>(link,8);if(c)originals_.push_back({c,Read<std::uint8_t>(c,0x162),Read<float>(c,0x148),Read<float>(c,0x14c)});link=Read<void*>(link);}
    auto** labels=Read<std::uint8_t**>(source,0x190);auto** end=Read<std::uint8_t**>(source,0x194);if(labels&&end>labels&&*labels)font_=Read<unsigned>(*labels,0x6c);
    const auto add=[&](const wchar_t* name,const wchar_t* label,Action a,Rect r,bool edit=true){return Add(source,name,label,a,int(r.x),int(r.y),int(r.w),edit,int(r.h));};
    bool ok=add(L"SurfaceOpen",L"编辑自由贴花",Action::open,{65,456,138,24},false);
    ok=add(L"SurfaceHeading",L"自由贴花",Action::none,{24,26,96,20})&&ok;
    const wchar_t* tabs[]={L"图层",L"属性",L"纹章库"};const Action tabActions[]={Action::layers,Action::properties,Action::library};
    for(unsigned i=0;i<3;++i){const auto name=L"SurfaceTab"+std::to_wstring(i);ok=add(name.c_str(),tabs[i],tabActions[i],layout_.tabs[i])&&ok;}
    ok=add(L"SurfaceSubtitle",L"",Action::none,{24,86,layout_.width-60,20})&&ok;
    for(unsigned row=0;row<layout_.visibleRows;++row){const auto name=L"SurfaceRow"+std::to_wstring(row);ok=add(name.c_str(),L"",Action::select,layout_.rows[row])&&ok;
        const auto hint=L"SurfaceHint"+std::to_wstring(row);ok=add(hint.c_str(),L"",Action::none,{68,layout_.rows[row].y+18,layout_.rows[row].w-44,16})&&ok;}
    ok=add(L"SurfacePrevious",L"上一页",Action::previous,layout_.previous)&&ok;ok=add(L"SurfaceNext",L"下一页",Action::next,layout_.next)&&ok;
    const wchar_t* tools[]={L"SurfaceAdd",L"SurfaceDuplicate",L"SurfaceRemove"};const wchar_t* captions[]={L"添加",L"复制",L"删除"};const Action toolActions[]={Action::add,Action::duplicate,Action::remove};
    for(unsigned i=0;i<3;++i)ok=add(tools[i],captions[i],toolActions[i],layout_.tools[i])&&ok;
    const wchar_t* names[]={L"SurfaceTop",L"SurfaceRaise",L"SurfaceLower",L"SurfaceBottom"};const wchar_t* orderLabels[]={L"置顶",L"上移",L"下移",L"置底"};const Action actions[]={Action::top,Action::raise,Action::lower,Action::bottom};
    for(unsigned i=0;i<4;++i)ok=add(names[i],orderLabels[i],actions[i],layout_.ordering[i])&&ok;
    ok=add(L"SurfacePlace",L"放置贴花",Action::place,layout_.place)&&ok;ok=add(L"SurfaceOrbit",L"调整视角",Action::orbit,layout_.orbit)&&ok;
    const wchar_t* keys[]={L"SurfaceRotation",L"SurfaceSize",L"SurfaceWear"};const wchar_t* titles[]={L"旋转",L"尺寸",L"磨损"};
    for(unsigned i=0;i<3;++i){const float y=198+60.f*i;ok=add(keys[i],titles[i],Action::none,{24,y,90,20})&&ok;
        const auto value=std::wstring(keys[i])+L"Value";ok=add(value.c_str(),L"",Action::none,{layout_.width-126,y,90,20})&&ok;}
    ok=add(L"SurfaceWrap",L"贴合拐角",Action::wrap,layout_.wrap)&&ok;ok=add(L"SurfacePattern",L"更换划痕",Action::pattern,layout_.pattern)&&ok;
    ok=add(L"SurfaceVisibility",L"显示当前图层",Action::visible,layout_.visible)&&ok;
    ok=add(L"SurfaceNative",L"隐藏原生槽位",Action::hideNative,layout_.hideNative)&&ok;
    ok=add(L"SurfaceStatus",L"",Action::none,{24,470,layout_.width-60,20})&&ok;
    ok=add(L"SurfaceSave",L"保存外观",Action::save,layout_.save)&&ok;ok=add(L"SurfaceCancel",L"取消",Action::cancel,layout_.cancel)&&ok;
    if(!ok||!AdoptDraw()){for(auto& c:controls_)Put<std::uint8_t>(c.pointer,0x162,0);Detach();return false;}return true;
}
bool Page::Visible()const{return service_&&Read<void*>(service_)==table_&&Read<void*>(service_,0xd0)==root_&&Read<std::uint8_t>(service_,0xac)&&Read<std::uint8_t>(root_,0x162);}
bool Page::Contains(float x,float y)const{if(!Visible())return false;return Rect{Read<float>(root_,0x148),Read<float>(root_,0x14c),layout_.width,layout_.height}.Contains(x,y);}
Command Page::PointerDown(float x,float y){if(!Visible()||!editing_)return {};x-=Read<float>(root_,0x148);y-=Read<float>(root_,0x14c);hoverX_=x;hoverY_=y;return gesture_.Down(layout_,tab_,offset_,x,y,appearance_,selected_,busy_||armed_);}
Command Page::PointerMove(float x,float y){
    if(!Visible())return {};x-=Read<float>(root_,0x148);y-=Read<float>(root_,0x14c);hoverX_=x;hoverY_=y;
    if(gesture_.Captured()&&tab_==Tab::layers&&GetTickCount64()-scrollAt_>160){const auto last=Count()>layout_.visibleRows?Count()-layout_.visibleRows:0;
        if(y<layout_.rows.front().y&&offset_)--offset_;else if(y>layout_.rows.back().y+layout_.rows.back().h&&offset_<last)++offset_;scrollAt_=GetTickCount64();}
    return gesture_.Move(layout_,offset_,Count(),x,y);
}
Command Page::Choose(void* control)const{
    for(unsigned row=0;row<layout_.visibleRows;++row){const auto name=L"SurfaceRow"+std::to_wstring(row);
        for(const auto& c:controls_)if(c.pointer==control&&c.name==name){
            if(tab_==Tab::library&&stockOffset_+row<stock_.size())return {Action::choose,stock_[stockOffset_+row].sticker,0};
            if(tab_==Tab::layers&&offset_+row<appearance_.items.size())return {Action::select,appearance_.items[appearance_.items.size()-1-offset_-row].id,0};}}
    return {};
}
void Page::Render(bool edit,bool placing,unsigned id,bool busy,bool dirty,bool armed,const layers::Design* a,const std::wstring& status){
    if(!service_||Read<void*>(service_)!=table_||Read<void*>(service_,0xd0)!=root_){Detach();return;}
    if(edit!=editing_){for(const auto& o:originals_){Put<std::uint8_t>(o.pointer,0x162,edit?0:o.visible);Put<float>(o.pointer,0x148,o.x);Put<float>(o.pointer,0x14c,o.y);}gesture_.Cancel();}
    editing_=edit;placing_=placing;selected_=id;busy_=busy;dirty_=dirty;armed_=armed;hasAppearance_=a!=nullptr;if(a)appearance_=*a;
    if(edit)for(const auto& o:originals_)Put<std::uint8_t>(o.pointer,0x162,0);
    if(armed_||tab_!=Tab::properties){auto& at=Offset();at=std::min(at,Count()>layout_.visibleRows?Count()-layout_.visibleRows:0);}
    const auto* selected=a?layers::Find(*a,id):nullptr;const bool positioned=selected&&selected->placed;
    for(auto& c:controls_){bool show=c.edit==edit,enable=c.action!=Action::none&&!busy&&!armed;c.color=text;
        if(c.action==Action::open){enable=a&&!busy;Text(c,a?L"编辑自由贴花":L"贴花连接中…");}
        if(c.name==L"SurfaceHeading")Text(c,L"自由贴花");
        if(c.name==L"SurfaceSubtitle"){c.color=muted;Text(c,FitLabel(armed_?(costs_.empty()?L"调整参数不消耗纹章":L"确认后扣除所列物品"):tab_==Tab::layers?std::to_wstring(a?a->items.size():0)+L" / "+std::to_wstring(policy_.maxLayers)+L" 层 · 拖动排序":tab_==Tab::library?(policy_.consume?L"新增或换图需要库存":L"复用已拥有或解锁纹章"):selected?Label(selected->sticker):L"请先选择图层",26));}
        if(c.action==Action::layers||c.action==Action::properties||c.action==Action::library){enable=!busy&&!armed;const auto t=c.action==Action::layers?Tab::layers:c.action==Action::properties?Tab::properties:Tab::library;c.color=t==tab_?accent:text;}
        const bool row=c.name.rfind(L"SurfaceRow",0)==0,hint=c.name.rfind(L"SurfaceHint",0)==0;
        if(row||hint){const auto i=unsigned(c.name.back()-L'0');show=show&&(armed_||tab_!=Tab::properties)&&Offset()+i<Count();enable=enable&&show;
            if(show){std::wstring label,detail;
                if(armed_){const auto& cost=costs_[quoteOffset_+i];label=FitLabel(Label(cost.sticker));detail=L"消耗 "+std::to_wstring(cost.required)+L" / 库存 "+std::to_wstring(cost.available);enable=false;}
                else if(tab_==Tab::library){const auto& stock=stock_[stockOffset_+i];label=FitLabel(Label(stock.sticker));detail=L"库存 "+std::to_wstring(stock.remaining)+(stock.unlocked?L" · 已解锁":L"");enable=enable&&(!policy_.consume||stock.remaining>0);}
                else{const auto& layer=a->items[a->items.size()-1-offset_-i];label=FitLabel(Label(layer.sticker));detail=layer.placed?(layer.visible?L"已放置":L"已隐藏"):L"待放置";c.color=layer.id==id?accent:text;}
                Text(c,hint?detail:label);if(hint)c.color=muted;}}
        if(c.action==Action::previous||c.action==Action::next){show=show&&(armed_||tab_!=Tab::properties)&&Count()>layout_.visibleRows;enable=!busy&&(c.action==Action::previous?Offset()>0:Offset()+layout_.visibleRows<Count());}
        if(c.action==Action::add||c.action==Action::duplicate||c.action==Action::remove){show=show&&!armed_&&tab_==Tab::layers;enable=enable&&a&&(c.action==Action::remove?selected!=nullptr:a->items.size()<policy_.maxLayers&&(c.action==Action::add||selected));}
        if(c.action>=Action::top&&c.action<=Action::bottom){show=show&&!armed_&&tab_==Tab::layers;enable=enable&&selected;}
        if(c.action==Action::hideNative){show=show&&!armed_&&tab_==Tab::layers;Text(c,a&&a->hiddenNative?L"✓ 隐藏原生槽位":L"显示原生槽位");}
        const bool property=c.action==Action::place||c.action==Action::orbit||c.action==Action::wrap||c.action==Action::pattern||c.action==Action::visible||c.name.rfind(L"SurfaceRotation",0)==0||c.name.rfind(L"SurfaceSize",0)==0||c.name.rfind(L"SurfaceWear",0)==0;
        if(property){show=show&&!armed_&&tab_==Tab::properties;enable=enable&&selected;if(c.action==Action::wrap||c.action==Action::pattern)enable=enable&&positioned;}
        if(c.action==Action::wrap)Text(c,selected&&selected->style.wrap?L"✓ 贴合拐角":L"平面投影");
        if(c.action==Action::visible)Text(c,selected&&selected->visible?L"✓ 显示当前图层":L"当前图层已隐藏");
        if(c.action==Action::place||c.action==Action::orbit)c.color=(c.action==Action::place)==placing?accent:text;
        if(c.action==Action::save){enable=!busy&&dirty;Text(c,busy?L"等待服务器…":armed?L"确认保存":L"核对并保存");}
        if(c.action==Action::cancel){enable=!busy;Text(c,armed?L"返回编辑":L"取消");}
        if(c.name==L"SurfaceRotationValue")Text(c,positioned?std::to_wstring(int(std::lround(selected->placement.rotation*180/3.141592653589793)))+L"°":L"—");
        if(c.name==L"SurfaceSizeValue")Text(c,positioned?std::to_wstring(int(std::lround(selected->placement.width*50)))+L"%":L"—");
        if(c.name==L"SurfaceWearValue")Text(c,positioned?std::to_wstring(unsigned(std::lround(selected->style.wear/10.)))+L"%":L"—");
        if(c.name==L"SurfaceStatus"){c.color=armed?accent:muted;Text(c,FitLabel(status,24));}
        if(!enable&&c.action!=Action::none)c.color=muted;
        Text(c,c.caption);Put<std::uint8_t>(c.pointer,0x162,show?1:0);Put<std::uint8_t>(c.pointer,0x167,enable?1:0);
    }
}
void Page::DrawCanvas(){
    if(!editing_||!Visible()||!renderer_||!*renderer_)return;Canvas canvas(Read<IDirect3DDevice9*>(*renderer_,8));if(!canvas.ready)return;
    const float left=Read<float>(root_,0x148),top=Read<float>(root_,0x14c);const auto shift=[&](Rect r){r.x+=left;r.y+=top;return r;};
    // The native panel supplies the only outer frame. Rows use quiet fills;
    // another full-panel outline or boxed row would collide with its inner rim.
    canvas.Quad(shift({0,0,layout_.width,layout_.height}),0xff0b202e);
    if(armed_||tab_!=Tab::properties){
        for(unsigned row=0;row<layout_.visibleRows;++row){const auto r=layout_.rows[row];if(Offset()+row>=Count())break;
            const auto* layer=!armed_&&tab_==Tab::layers?&appearance_.items[appearance_.items.size()-1-offset_-row]:nullptr;
            const auto sticker=armed_?costs_[quoteOffset_+row].sticker:layer?layer->sticker:stock_[stockOffset_+row].sticker;const bool selected=layer&&layer->id==selected_;
            canvas.Quad(shift(r),selected?0xff1a4056:0xff102b3a);canvas.Quad(shift({r.x+4,r.y+3,30,30}),0xff081a25);
            if(thumbnail_)if(auto* texture=thumbnail_(sticker)){canvas.Quad(shift({r.x+4,r.y+3,30,30}),0xffffffff,texture);texture->Release();}
            if(selected)canvas.Quad(shift({r.x,r.y,2,r.h}),accent);
        }
        if(Count()>layout_.visibleRows){const auto track=layout_.rows.back().y+layout_.rows.back().h-layout_.rows.front().y;
            const auto h=track*layout_.visibleRows/Count(),y=layout_.rows.front().y+(track-h)*Offset()/(Count()-layout_.visibleRows);
            canvas.Quad(shift({layout_.width-40,layout_.rows.front().y,4,track}),0xff173746);canvas.Quad(shift({layout_.width-40,y,4,h}),0xff5394aa);}
    }else if(const auto* layer=layers::Find(appearance_,selected_))if(thumbnail_)if(auto* texture=thumbnail_(layer->sticker)){canvas.Quad(shift({(layout_.width-12)/2-18,112,36,36}),0xffffffff,texture);texture->Release();}
    for(const auto& c:controls_)if(c.edit&&Read<std::uint8_t>(c.pointer,0x162)&&c.action!=Action::none&&c.name.rfind(L"SurfaceRow",0)!=0){
        const bool enabled=Read<std::uint8_t>(c.pointer,0x167)!=0,hover=c.rect.Contains(hoverX_,hoverY_),primary=c.action==Action::save;
        canvas.Quad(shift(c.rect),enabled?(primary?0xff1d5368:hover?0xff254d63:0xff183749):0xff112733);canvas.Border(shift(c.rect),enabled?(primary?0xff61c9df:hover?0xff508fa7:0xff315c70):0xff263e4c);
    }
    const auto* layer=layers::Find(appearance_,selected_);const bool positioned=layer&&layer->placed;
    if(!armed_&&tab_==Tab::properties)for(auto action:{Action::rotation,Action::size,Action::wear}){const auto r=layout_.Slider(action);double value=.5;
        if(positioned){const auto& p=layer->placement;value=action==Action::rotation?p.rotation/(2*3.141592653589793)+.5:action==Action::size?(p.width-.2)/7.8:layer->style.wear/1000.;}
        value=std::clamp(value,0.,1.);canvas.Quad(shift({r.x,r.y+6,r.w,4}),0xff243f50);canvas.Quad(shift({r.x,r.y+6,float(r.w*value),4}),positioned?0xff3e91ac:0xff294958);
        canvas.Quad(shift({r.x+float(r.w*value)-3,r.y+1,6,14}),positioned?0xffc4edf5:0xff577687);
    }
}
void Page::DrawLabels(void* argument){for(auto& c:controls_)if(Read<std::uint8_t>(c.pointer,0x162))Read<NativeDraw>(Read<void*>(c.pointer),13*4)(c.pointer,argument);}
void __fastcall Page::DrawHook(void* self,void*,void* argument){
    auto* page=drawing_;if(!page||self!=page->root_||!page->originalDraw_)return;if(!page->editing_){page->originalDraw_(self,argument);return;}
    // Retain the game's panel and the native text rendering context. The
    // editor draws only its own controls inside that panel's content area.
    std::vector<std::uint8_t> visibility;visibility.reserve(page->controls_.size());
    for(auto& c:page->controls_){visibility.push_back(Read<std::uint8_t>(c.pointer,0x162));Put<std::uint8_t>(c.pointer,0x162,0);}
    page->originalDraw_(self,argument);
    for(std::size_t i=0;i<visibility.size();++i)Put<std::uint8_t>(page->controls_[i].pointer,0x162,visibility[i]);
    page->DrawCanvas();page->DrawLabels(argument);
}
Action Page::Click(void* control)const{if(!Visible())return Action::none;for(const auto& c:controls_)if(c.pointer==control&&Read<std::uint8_t>(control,0x162))return Read<std::uint8_t>(control,0x167)?c.action:Action::none;return Action::none;}
void* Page::Control(Action action)const{for(const auto& c:controls_)if(c.action==action)return c.pointer;return nullptr;}
}
