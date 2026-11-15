#pragma once
#include "sdgo/surface_decal.h"
#include <map>
#include <set>

namespace sdgo::surface_decal::layers {
inline constexpr std::size_t maximum=20;
inline constexpr std::uint32_t nativeIdBase=0xf0000000u;
inline constexpr std::uint8_t independent=255;
struct Layer {
    std::uint32_t id=0,sticker=0;
    std::uint8_t source=independent;
    bool placed=false,visible=true;
    Placement placement;
    Style style;
};
struct Design {
    std::uint64_t unitSerial=0,revision=0;
    std::uint32_t unitType=0;
    std::array<std::uint32_t,3> equipped{};
    std::uint8_t hiddenNative=0;
    std::vector<Layer> items; // bottom-to-top; IDs, never positions, define identity
};
struct Policy {bool consume=true;std::uint32_t maxLayers=20;};
struct Stock {std::uint32_t sticker=0,remaining=0;bool unlocked=false;};
struct Cost {std::uint32_t sticker=0,required=0,available=0;};
bool Valid(const Design&,bool allowIncomplete=false);
bool Same(const Design&,const Design&);
bool SamePlacement(const Layer&,const Layer&);
Design FromLegacy(const Appearance&);
Appearance ToLegacy(const Design&);
const Layer* Find(const Design&,std::uint32_t id);
std::map<std::uint32_t,unsigned> RequiredItems(const Design& saved,const Design& proposal);
class Editor {
public:
    bool Open(const Design&);
    bool Add(std::uint32_t sticker,std::uint32_t& id);
    bool Duplicate(std::uint32_t id,std::uint32_t& added);
    bool Remove(std::uint32_t id);
    bool Place(std::uint32_t id,const Placement&);
    bool StyleValue(std::uint32_t id,const Style&);
    bool Move(std::uint32_t id,std::size_t destination);
    bool Visible(std::uint32_t id,bool);
    bool HideNative(bool);
    bool Cancel();
    bool ReplaceCanonicalDraft(const Design&);
    bool BeginSave(std::uint64_t request,Design&);
    bool Acknowledge(std::uint64_t,Reply,const Design&);
    bool Reconcile(const Design&);
    void Timeout(){if(state_==EditState::awaitingReply)state_=EditState::uncertain;}
    const Design& Preview()const{return draft_;}
    const Design& Saved()const{return saved_;}
    EditState State()const{return state_;}
    bool Dirty()const{return !Same(saved_,draft_);}
private:
    Design saved_,draft_;EditState state_=EditState::closed;
    std::uint32_t next_=1;std::uint64_t request_=0,lastRequest_=0;
    Layer* Editable(std::uint32_t);
};
}
