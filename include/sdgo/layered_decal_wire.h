#pragma once
#include "sdgo/layered_decal.h"
#include "sdgo/surface_decal_protocol.h"
#include "sdgo/friend_unit_gift_protocol.h"
#include <cmath>
#include <cstring>

namespace sdgo::surface_decal::layers::wire {
inline constexpr std::uint16_t get=0x1022,quote=0x1023,commit=0x1024,catalogue=0x1025;
inline constexpr std::size_t headerBytes=40,layerBytes=72,maxBytes=headerBytes+maximum*layerBytes;
inline constexpr std::uint32_t version=3;
using Token=std::array<std::uint8_t,16>;
using surface_decal::wire::Get;using surface_decal::wire::Put;
inline void Float(std::uint8_t* p,double value){const float f=static_cast<float>(value);std::uint32_t bits;std::memcpy(&bits,&f,4);Put(p,bits,4);}
inline double Float(const std::uint8_t* p){const auto bits=static_cast<std::uint32_t>(Get(p,4));float f;std::memcpy(&f,&bits,4);return f;}
inline double Rounded(double value,double lower,double upper){
    float f=static_cast<float>(value);if(double(f)<lower)f=std::nextafter(f,INFINITY);if(double(f)>upper)f=std::nextafter(f,-INFINITY);return f;
}
// Native graphics are float32. Canonicalize before requesting a quote so the
// displayed draft, database and acknowledgement all refer to the same values.
// Twenty bounded records fit the existing native carrier without fragmentation.
inline bool Encode(const Design& a,std::vector<std::uint8_t>& out){
    if(!Valid(a))return false;std::vector<std::uint8_t> b(headerBytes+a.items.size()*layerBytes,0);
    Put(b.data(),version,4);Put(b.data()+4,a.unitType,4);Put(b.data()+8,a.unitSerial,8);Put(b.data()+16,a.revision,8);
    for(unsigned i=0;i<3;++i)Put(b.data()+24+i*4,a.equipped[i],4);b[36]=a.hiddenNative;b[37]=static_cast<std::uint8_t>(a.items.size());
    for(std::size_t i=0;i<a.items.size();++i){const auto& l=a.items[i];auto* p=b.data()+headerBytes+i*layerBytes;
        Put(p,l.id,4);Put(p+4,l.sticker,4);Put(p+8,(l.placed?1u:0u)|(l.visible?2u:0u)|(l.style.wrap?4u:0u),4);p[12]=l.source;
        Put(p+14,l.style.wear,2);Put(p+16,l.style.seed,4);if(!l.placed)continue;
        const auto& placement=l.placement;Put(p+20,placement.anchor.node,4);Put(p+24,placement.anchor.geometry,8);Put(p+32,placement.anchor.triangle,4);
        const auto b0=Rounded(placement.anchor.barycentric[0],0,1);auto b1=Rounded(placement.anchor.barycentric[1],0,1);
        if(b0+b1>1)b1=std::nextafter(static_cast<float>(1-b0),0.f);
        Float(p+36,b0);Float(p+40,b1);Float(p+44,placement.anchor.tangent.x);Float(p+48,placement.anchor.tangent.y);Float(p+52,placement.anchor.tangent.z);
        const Limits limits;Float(p+56,Rounded(placement.width,limits.minSize,limits.maxSize));Float(p+60,Rounded(placement.height,limits.minSize,limits.maxSize));
        Float(p+64,Rounded(placement.rotation,-3.14159265358979323846,3.14159265358979323846));Float(p+68,Rounded(placement.depth,limits.minSize,limits.maxSize));
    }
    out=std::move(b);return true;
}
inline bool Decode(const std::uint8_t* b,std::size_t n,Design& out){
    if(!b||n<headerBytes||n>maxBytes||Get(b,4)!=version||b[38]||b[39]||b[37]>maximum||n!=headerBytes+b[37]*layerBytes)return false;
    Design a;a.unitType=static_cast<std::uint32_t>(Get(b+4,4));a.unitSerial=Get(b+8,8);a.revision=Get(b+16,8);a.hiddenNative=b[36];
    for(unsigned i=0;i<3;++i)a.equipped[i]=static_cast<std::uint32_t>(Get(b+24+i*4,4));
    for(unsigned i=0;i<b[37];++i){const auto* p=b+headerBytes+i*layerBytes;Layer l;l.id=static_cast<std::uint32_t>(Get(p,4));l.sticker=static_cast<std::uint32_t>(Get(p+4,4));const auto flags=Get(p+8,4);if(flags>7||p[13])return false;
        l.placed=(flags&1)!=0;l.visible=(flags&2)!=0;l.style.wrap=(flags&4)!=0;l.source=p[12];l.style.wear=static_cast<std::uint16_t>(Get(p+14,2));l.style.seed=static_cast<std::uint32_t>(Get(p+16,4));
        if(l.placed){auto& placement=l.placement;placement.anchor.node=static_cast<std::uint32_t>(Get(p+20,4));placement.anchor.geometry=Get(p+24,8);placement.anchor.triangle=static_cast<std::uint32_t>(Get(p+32,4));
            placement.anchor.barycentric[0]=Float(p+36);placement.anchor.barycentric[1]=Float(p+40);placement.anchor.barycentric[2]=1-placement.anchor.barycentric[0]-placement.anchor.barycentric[1];
            placement.anchor.tangent={Float(p+44),Float(p+48),Float(p+52)};placement.width=Float(p+56);placement.height=Float(p+60);placement.rotation=Float(p+64);placement.depth=Float(p+68);
        }else for(unsigned j=20;j<layerBytes;++j)if(p[j])return false;
        a.items.push_back(l);
    }
    if(!Valid(a))return false;out=std::move(a);return true;
}
inline bool Canonical(const Design& a,Design& out){std::vector<std::uint8_t> b;return Encode(a,b)&&Decode(b.data(),b.size(),out);}
enum class Result:std::uint32_t {ok=0,missing=6,denied=9,conflict=12,insufficient=18,expired=19,policyChanged=20,limit=21};
struct Response {Result result=Result::ok;Policy policy;Token token{};Design authority;std::vector<Cost> costs;};
inline bool EncodeResponse(const Response& r,std::vector<std::uint8_t>& out){
    std::vector<std::uint8_t> design;if(!Encode(r.authority,design)||r.costs.size()>maximum||r.policy.maxLayers<1||r.policy.maxLayers>maximum)return false;
    std::vector<std::uint8_t> b(48+design.size()+r.costs.size()*12,0);if(b.size()>friend_unit_gift::kMaximumExtensionPayloadBytes)return false;
    Put(b.data(),static_cast<std::uint32_t>(r.result),4);Put(b.data()+4,r.policy.consume?1:0,4);Put(b.data()+8,r.policy.maxLayers,4);Put(b.data()+12,design.size(),4);
    std::copy(r.token.begin(),r.token.end(),b.begin()+16);Put(b.data()+32,r.costs.size(),4);Put(b.data()+40,60000,4);
    std::copy(design.begin(),design.end(),b.begin()+48);
    for(std::size_t i=0;i<r.costs.size();++i){const auto& c=r.costs[i];if(!c.sticker||c.sticker>INT32_MAX||c.required>maximum)return false;
        auto* p=b.data()+48+design.size()+i*12;Put(p,c.sticker,4);Put(p+4,c.required,4);Put(p+8,c.available,4);}
    out=std::move(b);return true;
}
inline bool DecodeResponse(const std::uint8_t* b,std::size_t n,Response& out){
    if(!b||n<48||n>friend_unit_gift::kMaximumExtensionPayloadBytes||Get(b+4,4)>1||Get(b+8,4)<1||Get(b+8,4)>maximum||Get(b+32,4)>maximum||Get(b+36,4)||Get(b+44,4))return false;
    const auto bytes=Get(b+12,4),count=Get(b+32,4);if(bytes>maxBytes||48+bytes+count*12!=n)return false;
    const auto result=Get(b,4);if(result!=0&&result!=6&&result!=9&&result!=12&&result!=18&&result!=19&&result!=20&&result!=21)return false;
    Response r;r.result=static_cast<Result>(result);r.policy.consume=Get(b+4,4)!=0;r.policy.maxLayers=static_cast<std::uint32_t>(Get(b+8,4));
    std::copy(b+16,b+32,r.token.begin());if(!Decode(b+48,static_cast<std::size_t>(bytes),r.authority))return false;
    std::set<std::uint32_t> ids;
    for(unsigned i=0;i<count;++i){const auto* p=b+48+bytes+i*12;Cost c;c.sticker=static_cast<std::uint32_t>(Get(p,4));c.required=static_cast<std::uint32_t>(Get(p+4,4));c.available=static_cast<std::uint32_t>(Get(p+8,4));if(!c.sticker||c.sticker>INT32_MAX||c.required>maximum||!ids.insert(c.sticker).second)return false;r.costs.push_back(c);}
    out=std::move(r);return true;
}
static_assert(maxBytes+48+maximum*12<=friend_unit_gift::kMaximumExtensionPayloadBytes,"20-layer quote does not fit the authenticated carrier");
}
