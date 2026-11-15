#pragma once
#include "../../hangar_manager/native_ui.h"
#include "sdgo/surface_decal.h"
#include "sdgo/surface_decal_ui.h"
#include <d3d9.h>
#ifdef small
#undef small
#endif
#include <array>

namespace sdgo::surface_decal::ui {
class Page {
public:
    using Thumbnail=IDirect3DBaseTexture9*(*)(unsigned); // returns one temporary AddRef
    bool Bind(const SdgoApi*,SdgoMod*);
    bool Ready(void* service) const noexcept;
    void* FactoryControl(void* service,const wchar_t* name,int kind) const noexcept;
    void* Preview(void* factory) const noexcept {return FactoryControl(factory,L"Multi3DRender",16);}
    void* Footer(void* factory,unsigned index) const noexcept {return index<2?FactoryControl(factory,index?L"ReturnBtn":L"SaveBtn",4):nullptr;}
    bool Attach(void* service,void* buttonSource);
    void Detach(); // before the native page releases its children
    void Render(bool editing,bool placing,unsigned slot,bool busy,bool dirty,bool armed,const layers::Design*,const std::wstring& status);
    void Render(bool e,bool p,unsigned s,bool b,bool d,bool a,const Appearance* value,const std::wstring& text){auto design=value?layers::FromLegacy(*value):layers::Design{};Render(e,p,s,b,d,a,value?&design:nullptr,text);}
    void SetStock(const std::vector<layers::Stock>&,layers::Policy);
    void SetCosts(const std::vector<layers::Cost>& costs){costs_=costs;quoteOffset_=0;}
    void SetNameSource(std::wstring(*source)(unsigned)){name_=source;}
    bool Navigate(Action);
    bool Scroll(float x,float y,int lines);
    Command Choose(void* control)const;
    Action Click(void* control) const;
    void* Control(Action) const;
    bool Visible() const;
    bool Contains(float x,float y) const;
    Command PointerDown(float x,float y);
    Command PointerMove(float x,float y);
    void PointerUp(){gesture_.Cancel();}
    bool Captured()const{return gesture_.Captured();}
    void SetThumbnailSource(Thumbnail source){thumbnail_=source;}
    const LayerLayout& Geometry()const{return layout_;}
    void* Owner() const {return service_;}
private:
    struct ControlState {void* pointer=nullptr;Action action=Action::none;std::wstring caption;int width=0;bool edit=false;std::uint32_t color=0,renderedColor=0;std::wstring name;Rect rect;};
    struct Original {void* pointer;std::uint8_t visible;float x,y;};
    sdgo::hangar::ui::Bindings bind_;
    void *service_=nullptr,*root_=nullptr,*table_=nullptr;
    std::vector<ControlState> controls_;
    std::vector<Original> originals_;
    std::uint32_t font_=1,color_=0xff8ed9ff,shadow_=0xff000000;
    bool editing_=false;
    bool placing_=false,busy_=false,armed_=false,dirty_=false;unsigned selected_=0;
    layers::Design appearance_{};bool hasAppearance_=false;
    LayerLayout layout_;LayerGesture gesture_;float hoverX_=-1,hoverY_=-1;
    Tab tab_=Tab::layers;unsigned offset_=0,stockOffset_=0;std::uint64_t scrollAt_=0;
    std::vector<layers::Stock> stock_;layers::Policy policy_;
    std::vector<layers::Cost> costs_;unsigned quoteOffset_=0;
    std::wstring(*name_)(unsigned)=nullptr;
    unsigned Count()const;unsigned& Offset();std::wstring Label(unsigned)const;
    const SdgoApi* api_=nullptr;SdgoMod* mod_=nullptr;void** renderer_=nullptr;
    std::array<void*,162> drawTable_{};void** originalDrawTable_=nullptr;
    using NativeDraw=void(__thiscall*)(void*,void*);NativeDraw originalDraw_=nullptr;
    static Page* drawing_;
    static void __fastcall DrawHook(void*,void*,void*);
    Thumbnail thumbnail_=nullptr;
    bool AdoptDraw();void RestoreDraw()noexcept;void DrawCanvas();void DrawLabels(void*);
    void Position(ControlState&,Rect);
    bool Add(void* source,const wchar_t* name,const wchar_t* caption,Action,int x,int y,int width,bool edit,int height=22);
    void Text(ControlState&,const std::wstring&);
};
}
