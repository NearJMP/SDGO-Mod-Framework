#pragma once
#include "sdgo/surface_decal.h"
#include "sdgo/layered_decal.h"
#include <algorithm>
#include <cmath>

namespace sdgo::surface_decal::ui {
struct Rect {
    float x=0,y=0,w=0,h=0;
    bool Contains(float px,float py)const{return std::isfinite(px)&&std::isfinite(py)&&px>=x&&py>=y&&px<x+w&&py<y+h;}
    bool Overlaps(const Rect& b)const{return x<b.x+b.w&&b.x<x+w&&y<b.y+b.h&&b.y<y+h;}
};
enum class Action {none,open,slot0,slot1,slot2,place,orbit,left,right,smaller,larger,reset,save,cancel,
    top,raise,lower,bottom,wrap,pattern,rotation,size,wear,reorder,
    layers,properties,library,add,duplicate,remove,visible,hideNative,previous,next,select,choose};
struct Layout {
    float width=274,height=530;
    std::array<Rect,3> rows{};
    std::array<Rect,4> ordering{};
    Rect place,orbit,rotation,size,wear,wrap,pattern,save,cancel;
    bool Set(float w,float h){
        if(!std::isfinite(w)||!std::isfinite(h)||w<260||h<530||w>1024||h>768)return false;
        width=w;height=h;const float inner=w-28,half=(inner-8)/2,quarter=(inner-18)/4;
        for(unsigned i=0;i<3;++i)rows[i]={14,60+44.f*i,inner,40};
        for(unsigned i=0;i<4;++i)ordering[i]={14+(quarter+6)*i,200,quarter,26};
        place={14,236,half,28};orbit={22+half,236,half,28};
        rotation={14,298,inner,18};size={14,348,inner,18};wear={14,398,inner,18};
        wrap={14,422,half,24};pattern={22+half,422,half,24};
        save={14,h-34,half,26};cancel={22+half,h-34,half,26};return true;
    }
    Layout(){Set(274,530);}
    unsigned DropRow(float y)const{return static_cast<unsigned>(std::clamp(static_cast<int>(std::floor((y-rows[0].y)/44)),0,2));}
    const Rect& Slider(Action a)const{return a==Action::rotation?rotation:a==Action::size?size:wear;}
};
struct Command {Action action=Action::none;unsigned slot=0;double value=0;};
enum class Tab {layers,properties,library};
struct LayerLayout {
    static constexpr unsigned visibleRows=6;
    float width=274,height=530;
    std::array<Rect,3> tabs{},tools{};
    std::array<Rect,visibleRows> rows{};
    std::array<Rect,4> ordering{};
    Rect previous,next,place,orbit,rotation,size,wear,wrap,pattern,visible,hideNative,save,cancel;
    bool Set(float w,float h){
        if(!std::isfinite(w)||!std::isfinite(h)||w<260||h<530||w>1024||h>768)return false;
        width=w;height=h;const float inner=w-28,half=(inner-8)/2,third=(inner-12)/3,quarter=(inner-18)/4;
        for(unsigned i=0;i<3;++i){tabs[i]={14+(third+6)*i,42,third,26};tools[i]={14+(third+6)*i,368,third,26};}
        for(unsigned i=0;i<visibleRows;++i)rows[i]={14,100+40.f*i,inner-14,36};
        for(unsigned i=0;i<4;++i)ordering[i]={14+(quarter+6)*i,404,quarter,26};
        previous={14,344,half,20};next={22+half,344,half,20};
        place={14,148,half,28};orbit={22+half,148,half,28};
        rotation={14,210,inner,18};size={14,270,inner,18};wear={14,330,inner,18};
        wrap={14,370,half,26};pattern={22+half,370,half,26};visible={14,406,inner,26};hideNative={14,436,inner,24};
        save={14,h-34,half,26};cancel={22+half,h-34,half,26};return true;
    }
    LayerLayout(){Set(274,530);}
    const Rect& Slider(Action a)const{return a==Action::rotation?rotation:a==Action::size?size:wear;}
    unsigned Row(float y)const{return unsigned(std::clamp(int(std::floor((y-rows[0].y)/40)),0,int(visibleRows)-1));}
};
class LayerGesture {
public:
    Command Down(const LayerLayout& l,Tab tab,unsigned offset,float x,float y,const layers::Design& design,unsigned selected,bool busy){
        Cancel();if(busy)return {};
        if(tab==Tab::layers)for(unsigned row=0;row<l.visibleRows;++row)if(l.rows[row].Contains(x,y)&&offset+row<design.items.size()){
            id_=design.items[design.items.size()-1-offset-row].id;action_=Action::reorder;startY_=y;return {Action::select,id_,0};
        }
        const auto* item=layers::Find(design,selected);
        if(tab==Tab::properties&&item&&item->placed)for(auto action:{Action::rotation,Action::size,Action::wear})if(l.Slider(action).Contains(x,y)){
            action_=action;id_=selected;return Move(l,offset,unsigned(design.items.size()),x,y);
        }
        return {};
    }
    Command Move(const LayerLayout& l,unsigned offset,unsigned count,float x,float y){
        if(!std::isfinite(x)||!std::isfinite(y))return {};
        if(action_==Action::reorder&&count){if(std::abs(y-startY_)<5&&!moved_)return {};moved_=true;
            const auto visual=std::min(count-1,offset+l.Row(y));return {action_,id_,double(count-1-visual)};}
        if(action_==Action::rotation||action_==Action::size||action_==Action::wear){const auto& r=l.Slider(action_);return {action_,id_,std::clamp<double>((x-r.x)/r.w,0,1)};}return {};
    }
    void Cancel(){action_=Action::none;moved_=false;}
    bool Captured()const{return action_!=Action::none;}
private:
    Action action_=Action::none;unsigned id_=0;float startY_=0;bool moved_=false;
};
// Pure gesture state. Slot identities remain stable while their visual rows
// move, and a captured drag may finish outside the panel without hitting items.
class Gesture {
public:
    Command Down(const Layout& l,float x,float y,const Appearance& a,unsigned selected,bool busy){
        Cancel();if(busy)return {};
        for(unsigned row=0;row<3;++row)if(l.rows[row].Contains(x,y)){
            slot_=a.order[2-row];if(!a.slots[slot_].sticker)return {};
            action_=Action::reorder;startY_=y;return {static_cast<Action>(static_cast<unsigned>(Action::slot0)+slot_),slot_,0};
        }
        if(selected<3&&a.slots[selected].positioned)for(auto action:{Action::rotation,Action::size,Action::wear})if(l.Slider(action).Contains(x,y)){
            action_=action;slot_=selected;return Move(l,x,y);
        }
        return {};
    }
    Command Move(const Layout& l,float x,float y){
        if(!std::isfinite(x)||!std::isfinite(y))return {};
        if(action_==Action::reorder){if(std::abs(y-startY_)<5&&!moved_)return {};moved_=true;return {action_,slot_,double(2-l.DropRow(y))};}
        if(action_==Action::rotation||action_==Action::size||action_==Action::wear){const auto& r=l.Slider(action_);return {action_,slot_,std::clamp<double>((x-r.x)/r.w,0,1)};}
        return {};
    }
    void Cancel(){action_=Action::none;moved_=false;}
    bool Captured()const{return action_!=Action::none;}
    Action Active()const{return action_;}
private:
    Action action_=Action::none;unsigned slot_=0;float startY_=0;bool moved_=false;
};
}
