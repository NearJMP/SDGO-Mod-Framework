#include "sdgo/layered_decal.h"
#include <algorithm>

namespace sdgo::surface_decal::layers {
const Layer* Find(const Design& a,std::uint32_t id){const auto i=std::find_if(a.items.begin(),a.items.end(),[&](const auto& l){return l.id==id;});return i==a.items.end()?nullptr:&*i;}
bool SamePlacement(const Layer& a,const Layer& b){
    Appearance x,y;x.unitType=y.unitType=1;x.unitSerial=y.unitSerial=1;
    x.slots[0]={1,a.placed,a.placement};y.slots[0]={1,b.placed,b.placement};x.styles[0].wrap=a.style.wrap;y.styles[0].wrap=b.style.wrap;
    return SamePlacements(x,y);
}
bool Valid(const Design& a,bool incomplete){
    if(!a.unitSerial||a.unitSerial>INT64_MAX||!a.unitType||a.unitType>INT32_MAX||a.revision>INT64_MAX||a.hiddenNative>7||a.items.size()>maximum)return false;
    std::set<std::uint32_t> ids,sources;
    for(auto id:a.equipped)if(id>INT32_MAX)return false;
    for(const auto& l:a.items){
        if(!l.id||!ids.insert(l.id).second||!l.sticker||l.sticker>INT32_MAX||l.style.wear>1000)return false;
        if(l.source!=independent){if(l.source>=3||l.id!=nativeIdBase+1+l.source||!sources.insert(l.source).second||a.equipped[l.source]!=l.sticker)return false;}
        else if(l.id>=nativeIdBase)return false;
        if(l.placed){if(!surface_decal::Valid(l.placement))return false;}
        else if(!incomplete&&(l.source==independent||l.style.wear||l.style.wrap))return false;
    }
    return true;
}
bool Same(const Design& a,const Design& b){
    if(a.unitSerial!=b.unitSerial||a.unitType!=b.unitType||a.equipped!=b.equipped||a.hiddenNative!=b.hiddenNative||a.items.size()!=b.items.size())return false;
    for(std::size_t i=0;i<a.items.size();++i){const auto& x=a.items[i];const auto& y=b.items[i];
        if(x.id!=y.id||x.sticker!=y.sticker||x.source!=y.source||x.visible!=y.visible||x.style.wear!=y.style.wear||x.style.seed!=y.style.seed||!SamePlacement(x,y))return false;}
    return true;
}
Design FromLegacy(const Appearance& a){
    Design out;out.unitSerial=a.unitSerial;out.unitType=a.unitType;out.revision=a.revision;
    for(unsigned i=0;i<3;++i)out.equipped[i]=a.slots[i].sticker;
    for(const auto i:a.order)if(a.slots[i].positioned){Layer l;l.id=nativeIdBase+1+i;l.source=i;l.sticker=a.slots[i].sticker;l.placed=true;l.placement=a.slots[i].placement;l.style=a.styles[i];out.items.push_back(l);out.hiddenNative|=std::uint8_t(1u<<i);}
    return out;
}
Appearance ToLegacy(const Design& a){
    Appearance out;out.unitSerial=a.unitSerial;out.unitType=a.unitType;out.revision=a.revision;
    for(unsigned i=0;i<3;++i)out.slots[i].sticker=a.equipped[i];
    std::vector<std::uint8_t> order;
    for(const auto& l:a.items)if(l.source<3){out.slots[l.source]={l.sticker,l.placed,l.placement};out.styles[l.source]=l.style;if(l.placed&&!l.visible)out.styles[l.source].wear=1000;order.push_back(l.source);}
    for(unsigned i=0;i<3;++i)if(std::find(order.begin(),order.end(),i)==order.end())order.insert(order.begin(),std::uint8_t(i));
    std::copy(order.begin(),order.end(),out.order.begin());return out;
}
std::map<std::uint32_t,unsigned> RequiredItems(const Design& saved,const Design& proposal){
    std::map<std::uint32_t,unsigned> result;
    for(const auto& l:proposal.items){if(l.source<3)continue;const auto* old=Find(saved,l.id);if(!old||old->sticker!=l.sticker)++result[l.sticker];}
    return result;
}
Layer* Editor::Editable(std::uint32_t id){if(state_!=EditState::editing)return nullptr;auto i=std::find_if(draft_.items.begin(),draft_.items.end(),[&](const auto& x){return x.id==id;});return i==draft_.items.end()?nullptr:&*i;}
bool Editor::Open(const Design& a){
    if(!Valid(a)||state_==EditState::awaitingReply||state_==EditState::uncertain||(state_==EditState::editing&&Dirty()))return false;
    saved_=draft_=a;state_=EditState::editing;request_=0;next_=1;for(const auto& l:a.items)if(l.id<nativeIdBase)next_=std::max(next_,l.id+1);return true;
}
bool Editor::Add(std::uint32_t sticker,std::uint32_t& id){
    if(state_!=EditState::editing||!sticker||sticker>INT32_MAX||draft_.items.size()>=maximum||next_>=nativeIdBase)return false;
    Layer l;l.id=next_++;l.sticker=sticker;l.style.wrap=true;draft_.items.push_back(l);if(saved_.items.empty())draft_.hiddenNative=7;id=l.id;return true;
}
bool Editor::Duplicate(std::uint32_t id,std::uint32_t& added){
    const auto* source=Editable(id);if(!source)return false;auto copy=*source;if(!Add(copy.sticker,added))return false;
    copy.id=added;copy.source=independent;draft_.items.back()=copy;return true;
}
bool Editor::Remove(std::uint32_t id){
    auto* item=Editable(id);if(!item)return false;if(item->source<3)draft_.hiddenNative|=std::uint8_t(1u<<item->source);
    draft_.items.erase(std::find_if(draft_.items.begin(),draft_.items.end(),[&](const auto& x){return x.id==id;}));return true;
}
bool Editor::Place(std::uint32_t id,const Placement& p){auto* l=Editable(id);if(!l||!surface_decal::Valid(p))return false;l->placement=p;l->placed=true;return true;}
bool Editor::StyleValue(std::uint32_t id,const Style& style){auto* l=Editable(id);if(!l||style.wear>1000)return false;l->style=style;return true;}
bool Editor::Move(std::uint32_t id,std::size_t destination){
    auto* item=Editable(id);if(!item||destination>=draft_.items.size())return false;auto begin=draft_.items.begin();const auto at=std::size_t(item-&draft_.items[0]);
    if(at<destination)std::rotate(begin+at,begin+at+1,begin+destination+1);else if(at>destination)std::rotate(begin+destination,begin+at,begin+at+1);return true;
}
bool Editor::Visible(std::uint32_t id,bool value){auto* l=Editable(id);if(!l)return false;l->visible=value;return true;}
bool Editor::HideNative(bool value){if(state_!=EditState::editing)return false;draft_.hiddenNative=value?7:0;return true;}
bool Editor::Cancel(){if(state_!=EditState::editing)return false;draft_=saved_;state_=EditState::closed;return true;}
bool Editor::ReplaceCanonicalDraft(const Design& a){if(state_!=EditState::editing||!Valid(a)||a.unitSerial!=draft_.unitSerial||a.unitType!=draft_.unitType||a.revision!=draft_.revision)return false;draft_=a;return true;}
bool Editor::BeginSave(std::uint64_t id,Design& proposal){if(state_!=EditState::editing||!Dirty()||!Valid(draft_)||!id||id<=lastRequest_)return false;request_=lastRequest_=id;proposal=draft_;state_=EditState::awaitingReply;return true;}
bool Editor::Acknowledge(std::uint64_t id,Reply reply,const Design& a){
    if(reply!=Reply::saved&&reply!=Reply::rejected&&reply!=Reply::conflict)return false;
    if((state_!=EditState::awaitingReply&&state_!=EditState::uncertain)||id!=request_||!Valid(a)||a.unitSerial!=saved_.unitSerial||a.unitType!=saved_.unitType||a.revision<saved_.revision)return false;
    if(a.revision==saved_.revision&&!Same(a,saved_))return false;
    if(reply==Reply::saved&&(a.revision<=saved_.revision||!Same(a,draft_)))return false;
    saved_=a;if(reply!=Reply::rejected)draft_=a;request_=0;state_=EditState::editing;return true;
}
bool Editor::Reconcile(const Design& a){
    if(state_!=EditState::uncertain||!Valid(a)||a.unitSerial!=saved_.unitSerial||a.unitType!=saved_.unitType||a.revision<saved_.revision||(a.revision==saved_.revision&&!Same(a,saved_)))return false;
    saved_=draft_=a;request_=0;state_=EditState::editing;return true;
}
}
