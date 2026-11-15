#include "native_ui.h"
#include "render.h"
#include "textures.h"
#include "sdgo/layered_decal_wire.h"
#include "sdgo/fixed_surface_decal.h"
#include "visibility.h"
#include "sdgo/surface_decal_native.h"
#include "sdgo/surface_decal_protocol.h"
#include "sdgo/surface_decal_observer.h"
#include "sdgo/surface_decal_battle.h"
#include "sdgo/surface_decal_room.h"
#include "sdgo/friend_unit_gift_protocol.h"
#include <windows.h>
#include <intrin.h>
#include <map>
#include <set>
#include <mutex>
#include <cstring>
#include <limits>
#include <windowsx.h>
#include <commctrl.h>
#include <atomic>
#include <cstdio>

namespace {
using namespace sdgo::surface_decal;
using namespace sdgo::friend_unit_gift;
namespace decalwire=sdgo::surface_decal::wire;
namespace layer=sdgo::surface_decal::layers;
namespace layerwire=layer::wire;
const SdgoApi* g_api=nullptr;SdgoMod* g_mod=nullptr;
native::GeometryReader geometry;
ui::Page page;native::Textures textures;
std::recursive_mutex gate;
BattleBindings battleBindings;
RoomBindings roomBindings;
void* humanSiteOriginal=nullptr;void* battleStyleOriginal=nullptr;void* previewInputOriginal=nullptr;
void* previewZoomOriginal=nullptr;void* previewZoomEnd=nullptr;
const unsigned defaultMxcsr=0x1f80;
struct LastErrorGuard {DWORD value=GetLastError();~LastErrorGuard(){SetLastError(value);}};
template<class T>T Read(const void* p,unsigned offset=0){T v{};std::memcpy(&v,static_cast<const std::uint8_t*>(p)+offset,sizeof v);return v;}
template<class T>void Put(void* p,unsigned offset,T v){std::memcpy(static_cast<std::uint8_t*>(p)+offset,&v,sizeof v);}
bool Copy(const void* p,void* out,std::size_t bytes) noexcept {__try{if(!p)return false;std::memcpy(out,p,bytes);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
using Style=void(__cdecl*)(void*,const std::uint16_t*,std::uint16_t,const std::uint32_t*);
using Callback=void(__thiscall*)(void*,void*,unsigned);
using Simple=void(__thiscall*)(void*);
using Destroy=void*(__thiscall*)(void*,unsigned);
using UiEvent=void(__stdcall*)(void*,unsigned,std::uintptr_t,void*);
using Material=void*(__thiscall*)(void*,unsigned,int,int);
using Indexed=bool(__thiscall*)(void*,unsigned,int,unsigned,unsigned,unsigned,unsigned);
Indexed indexedOriginal=nullptr;
thread_local bool suppressNative=false;
Style styleOriginal=nullptr;Callback callbackOriginal=nullptr;
Simple modelDestroyOriginal=nullptr,pageUpdateOriginal=nullptr;
Simple previewLock=nullptr,previewUnlock=nullptr;
Destroy pageDestroyOriginal=nullptr;UiEvent eventOriginal=nullptr;
Material material=nullptr;
using InventoryPage=void(__thiscall*)(void*,int);
InventoryPage inventoryPage=nullptr;void* inventoryTable=nullptr;
using InventoryTick=void(__thiscall*)(void*);using InventoryAction=unsigned(__thiscall*)(void*);
InventoryTick inventoryTick=nullptr;InventoryAction inventoryAction=nullptr;
std::uint64_t inventoryRefreshUntil=0,inventorySettleUntil=0;
void **renderer=nullptr,**services=nullptr;
void *stickerTable=nullptr,*factoryTable=nullptr;
using App=std::uint8_t*(__cdecl*)();App app=nullptr;
ClientExtensionSendFn send=nullptr;ClientExtensionStateFn transport=nullptr;
using Session=void*(__cdecl*)();Session session=nullptr;
std::uint64_t sessionKey=0;
struct Node {native::Node surface;View view;std::uint64_t viewed=0;};
using FixedPatchKey=std::pair<std::uint32_t,std::uint32_t>; // layer, native node
struct FixedProjection {Patch patch;std::uint64_t geometry=0,group=0;Matrix toAnchor{};std::array<double,3> maxCoordinate{};};
struct Model {
    bool battle=false,room=false,geometryPending=false;
    std::uint32_t draws=0;
    std::uint64_t uid=0,geometryRetry=0,seenAt=0;
    std::uint32_t type=0;
    std::array<std::uint32_t,3> stickers{};
    std::vector<Node> nodes;
    std::map<std::uint32_t,Patch> patches;
    std::map<FixedPatchKey,FixedProjection> fixedPatches;
    std::map<FixedPatchKey,unsigned> fixedDraws;
    std::uint64_t bindingsAt=0;
    std::map<std::uint32_t,unsigned> layerDraws;
    layer::Design projected{};
};
struct Cached {layer::Design appearance;std::uint64_t fetched=0;};
std::map<void*,Model> models;
std::map<std::uint64_t,Cached> saved;
std::set<std::uint64_t> wanted;
layer::Editor editor;
Appearance pageRecord{};
std::wstring notice=L"正在连接贴花服务…";
bool editing=false,placing=false,armed=false,dragging=false,mouseDown=false;
unsigned selected=0;
std::uint64_t sequence=0,pendingRequest=0,pendingUid=0,pendingAt=0,retryAt=0;
std::uint16_t pendingCommand=0;
bool BlockingRequest(){return pendingUid&&pendingCommand!=layerwire::get;}
std::uintptr_t pendingToken=0,nextToken=0;
UINT_PTR timer=0;
HHOOK messageHook=nullptr;
bool observing=false;
bool legacyServer=false;
layer::Policy policy;std::vector<layer::Stock> stock;bool catalogueWanted=false;std::uint32_t catalogueCursor=0;
layerwire::Token quoteToken{};std::vector<layer::Cost> quotedCosts;unsigned commitRetries=0;
bool saveQueued=false;
layerwire::Token lastCommit{};
HWND inputWindow=nullptr;bool pointerOwned=false;
constexpr UINT_PTR inputSubclass=0x53444332;
std::atomic<bool> inventoryBlocked{false};
std::uint32_t drawCount=0;
std::uint32_t battleDrawCount=0;
void* preview=nullptr;std::uint8_t previewInput=0,previewPitch=0;bool inputHeld=false;
struct Footer {void* factory=nullptr;std::array<void*,2> buttons{};std::array<std::uint8_t,2> visible{};} footer;

bool Record(const void* source,Appearance& out){
    std::array<std::uint8_t,0x99> bytes{};if(!Copy(source,bytes.data(),bytes.size()))return false;
    Appearance a;a.unitType=Read<std::uint32_t>(bytes.data());a.unitSerial=Read<std::uint64_t>(bytes.data(),8);
    for(unsigned i=0;i<3;++i)a.slots[i].sticker=Read<std::uint32_t>(bytes.data(),60+i*4);
    decalwire::Bytes encoded;if(!decalwire::Encode(a,encoded))return false;out=a;return true;
}
std::uint32_t RoomUnit(std::uint32_t getter,std::uint32_t room,std::uint32_t slot) noexcept {
    using Get=std::uint8_t*(__thiscall*)(void*,int);
    __try{return reinterpret_cast<std::uint32_t>(reinterpret_cast<Get>(getter)(reinterpret_cast<void*>(room),static_cast<int>(slot)));}
    __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
bool RoomRecord(void* model,const std::uint16_t* colors,const std::array<std::uint32_t,3>& ids,Appearance& out){
    const auto source=roomBindings.Resolve(reinterpret_cast<std::uint32_t>(app()),reinterpret_cast<std::uint32_t>(model),
        [](std::uint32_t address,void* target,std::size_t size){return Copy(reinterpret_cast<void*>(address),target,size);},RoomUnit);
    Appearance record;std::uint32_t type=0;
    if(!source||!Record(reinterpret_cast<void*>(source),record)||
       !Copy(reinterpret_cast<const std::uint8_t*>(colors)-4,&type,sizeof type)||type!=record.unitType)return false;
    for(unsigned i=0;i<3;++i)if(record.slots[i].sticker!=ids[i])return false;
    out=record;return true;
}
void* Service(void* table){
    if(!services||!*services)return nullptr;auto* head=Read<void*>(*services,0x44);auto* link=head?Read<void*>(head):nullptr;
    for(unsigned i=0;link&&link!=head&&i<512;++i){auto* s=Read<void*>(link,8);if(s&&Read<void*>(s)==table)return s;link=Read<void*>(link);}return nullptr;
}
void RefreshNativeStock(){
    const auto now=GetTickCount64();if(now>=inventoryRefreshUntil&&now>=inventorySettleUntil)return;
    auto* stockPage=Service(inventoryTable);
    if(!stockPage||!Read<std::uint8_t>(stockPage,0xac)||!Read<std::uint8_t>(stockPage,0xe4)||Read<unsigned>(stockPage,0x178)!=5)return;
    // The input guard intentionally suspends the interactive inventory update.
    // Advance only its original page-read state while the editor owns input.
    if(editing&&inventoryAction(static_cast<std::uint8_t*>(stockPage)+0x44)==10)inventoryTick(stockPage);
    const auto current=Read<unsigned>(stockPage,0x180),pending=Read<unsigned>(stockPage,0x184),total=Read<unsigned>(stockPage,0x188);
    if(pending||!current||!total||total>255)return;
    if(now<inventoryRefreshUntil&&current<=total){
        // Native delta zero queues a fresh request for the current page. Its
        // original 05FF -> 09F2 path reloads real stacks and redraws the tiles.
        inventoryPage(stockPage,0);
        if(Read<unsigned>(stockPage,0x184)==current){inventoryRefreshUntil=0;inventorySettleUntil=now+10000;
            if(observing)g_api->Log(g_mod,SDGO_LOG_INFO,"surface native sticker inventory refresh queued page=%u",current);}
    }else if(now<inventorySettleUntil&&current>total){
        // Consuming the last stack can remove the old final page. Reuse the
        // same native navigation path for the remaining last valid page.
        inventoryPage(stockPage,int(total)-int(current));inventorySettleUntil=now+10000;
    }else if(now<inventorySettleUntil&&inventoryAction(static_cast<std::uint8_t*>(stockPage)+0x44)!=10){
        inventorySettleUntil=0;
    }
}
void RestoreInput(){
    // Resolve the live factory each time. Never touch the saved pointer after
    // a page transition, model destruction or a native factory reload.
    if(inputHeld){auto* factory=Service(factoryTable);if(preview&&page.Preview(factory)==preview){
        Put<std::uint8_t>(preview,0x263,previewInput);Put<std::uint8_t>(preview,0x264,previewPitch);
        Put<std::uint8_t>(preview,0x261,0);Put<std::uint8_t>(preview,0x262,0);
    }}
    inputHeld=false;preview=nullptr;dragging=false;mouseDown=false;
}
void RestoreFooter(){
    auto* factory=Service(factoryTable);
    if(factory&&factory==footer.factory)for(unsigned i=0;i<2;++i)
        if(footer.buttons[i]&&page.Footer(factory,i)==footer.buttons[i])Put<std::uint8_t>(footer.buttons[i],0x162,footer.visible[i]);
    footer={};
}
void Close(){
    if(editor.State()==EditState::editing)editor.Cancel();
    editing=placing=armed=false;saveQueued=false;quoteToken={};quotedCosts.clear();page.PointerUp();inventoryBlocked=false;RestoreInput();RestoreFooter();
    if(pointerOwned){pointerOwned=false;if(inputWindow&&GetCapture()==inputWindow)ReleaseCapture();}
}
const layer::Design* Authority(std::uint64_t uid){const auto i=saved.find(uid);return i==saved.end()?nullptr:&i->second.appearance;}
const layer::Design* AppearanceFor(const Model& m){
    if(editing&&editor.Preview().unitSerial==m.uid)return &editor.Preview();return Authority(m.uid);
}
bool SameStickers(const layer::Design& a,const Model& m){return a.unitSerial==m.uid&&a.unitType==m.type&&a.equipped==m.stickers;}
bool CurrentModelPose(void* owner,unsigned index,Matrix& output){
    std::array<std::uintptr_t,3> range{};if(!Copy(static_cast<const std::uint8_t*>(owner)+0x124,range.data(),sizeof range)||
        !range[0]||range[1]<range[0]||range[2]<range[1]||range[0]%4||range[1]%4||range[2]%4||(range[1]-range[0])/4>4096||index>=(range[1]-range[0])/4)return false;
    void* node=nullptr;void* actualOwner=nullptr;std::array<float,16> pose{};
    if(!Copy(reinterpret_cast<const void*>(range[0]+index*4),&node,4)||!node||!Copy(static_cast<const std::uint8_t*>(node)+0x118,&actualOwner,4)||actualOwner!=owner||!Copy(static_cast<const std::uint8_t*>(node)+0x10,pose.data(),sizeof pose))return false;
    for(auto v:pose)if(!std::isfinite(v))return false;
    if(pose[3]!=0||pose[7]!=0||pose[11]!=0||pose[15]!=1)return false;
    for(unsigned i=0;i<16;++i)output[i]=pose[i];return true;
}
bool StableFixedPose(const FixedProjection& projected,const Matrix& current,const Placement& placement){
    const auto tolerance=std::min(placement.width,placement.height)*.0001;
    return FixedPoseWithinDisplacement(projected.maxCoordinate,projected.toAnchor,current,tolerance);
}
bool InvertModelPose(const Matrix& pose,Matrix& inverse){
    if(pose[3]!=0||pose[7]!=0||pose[11]!=0||pose[15]!=1||!Invert(pose,inverse))return false;
    // These entries are algebraically exact for an already validated affine
    // input; avoid Gauss-Jordan roundoff inventing a perspective component.
    inverse[3]=inverse[7]=inverse[11]=0;inverse[15]=1;return true;
}
void Rebuild(void* owner,Model& m,const layer::Design& a){
    if(!SameStickers(a,m)){m.patches.clear();m.fixedPatches.clear();m.projected={};return;}
    if(layer::Same(m.projected,a))return;
    std::map<std::uint32_t,Patch> next;
    std::map<FixedPatchKey,FixedProjection> fixed;
    for(const auto& item:a.items){if(!item.placed)continue;const auto old=m.patches.find(item.id);const auto* previous=layer::Find(m.projected,item.id);
        if(old!=m.patches.end()&&previous&&layer::SamePlacement(*previous,item)){next[item.id]=std::move(old->second);
            for(auto& p:m.fixedPatches)if(p.first.first==item.id)fixed.emplace(p.first,std::move(p.second));continue;}
        Patch patch;for(const auto& n:m.nodes)if(n.surface.index==item.placement.anchor.node){
            bool assembled=false;
            if(item.style.wrap&&n.surface.fixedGroup&&n.surface.modelPoseValid){
                // Use accumulated model-space poses, before the actor's world
                // translation, so seams do not depend on large map coordinates.
                Matrix anchorPose{},inverse{};
                if(CurrentModelPose(owner,n.surface.index,anchorPose)&&InvertModelPose(anchorPose,inverse)){
                    std::vector<FixedPart> parts;
                    for(const auto& other:m.nodes)if(other.surface.fixedGroup==n.surface.fixedGroup&&other.surface.modelPoseValid){
                        Matrix transform{};if(other.surface.index==n.surface.index)transform[0]=transform[5]=transform[10]=transform[15]=1;
                        else{Matrix pose{};if(!CurrentModelPose(owner,other.surface.index,pose))continue;transform=Multiply(pose,inverse);}
                        parts.push_back({other.surface.index,other.surface.geometry,other.surface.fixedGroup,&other.surface.mesh,transform});
                    }
                    std::vector<PartPatch> output;
                    if(ProjectFixedWrapped(parts,item.placement,output)==Result::ok){
                        for(auto& part:output)if(part.node==n.surface.index){patch=std::move(part.patch);assembled=true;}
                        else{const auto source=std::find_if(parts.begin(),parts.end(),[&](const auto& p){return p.node==part.node;});if(source==parts.end())continue;
                            FixedProjection value;value.geometry=part.geometry;value.group=n.surface.fixedGroup;value.toAnchor=source->toAnchor;value.patch=std::move(part.patch);
                            for(const auto& v:value.patch.vertices){value.maxCoordinate[0]=std::max(value.maxCoordinate[0],std::abs(v.position.x));value.maxCoordinate[1]=std::max(value.maxCoordinate[1],std::abs(v.position.y));value.maxCoordinate[2]=std::max(value.maxCoordinate[2],std::abs(v.position.z));}
                            fixed[{item.id,part.node}]=std::move(value);}
                    }
                }
            }
            if(!assembled){if(item.style.wrap)ProjectWrapped(n.surface.mesh,n.surface.geometry,n.surface.index,item.placement,patch);
                else Project(n.surface.mesh,n.surface.geometry,n.surface.index,item.placement,patch);}break;}
        next[item.id]=std::move(patch);
    }
    m.patches=std::move(next);m.fixedPatches=std::move(fixed);m.projected=a;
    for(auto i=m.fixedDraws.begin();i!=m.fixedDraws.end();)if(!m.fixedPatches.count(i->first))i=m.fixedDraws.erase(i);else ++i;
    for(auto i=m.layerDraws.begin();i!=m.layerDraws.end();)if(!layer::Find(a,i->first))i=m.layerDraws.erase(i);else ++i;
}
void InvalidateQuote(){armed=false;quoteToken={};quotedCosts.clear();}
void RefreshStock(){stock.clear();catalogueCursor=0;catalogueWanted=true;}
void __cdecl ReplyReceived(void*,std::uint16_t,std::int32_t,const std::uint8_t*,std::uint32_t);
bool Request(std::uint16_t command,std::uint64_t uid,const std::uint8_t* bytes,std::uint32_t size){
    if(!send||!transport||transport()!=1||pendingUid)return false;
    pendingUid=uid;pendingCommand=command;pendingAt=GetTickCount64();pendingToken=++nextToken;
    if(send(command,bytes,size,&ReplyReceived,reinterpret_cast<void*>(pendingToken)))return true;
    pendingUid=0;pendingCommand=0;return false;
}
void Commit(){
    std::uint8_t bytes[24];layerwire::Put(bytes,editor.Preview().unitSerial,8);std::copy(quoteToken.begin(),quoteToken.end(),bytes+8);
    if(Request(layerwire::commit,editor.Preview().unitSerial,bytes,sizeof bytes))notice=L"正在保存并确认库存…";
}
void __cdecl ReplyReceived(void* token,std::uint16_t command,std::int32_t status,const std::uint8_t* data,std::uint32_t bytes){
    std::lock_guard<std::recursive_mutex> hold(gate);
    if(!pendingUid||pendingCommand!=command||reinterpret_cast<std::uintptr_t>(token)!=pendingToken)return;
    const auto uid=pendingUid,request=pendingRequest;pendingUid=0;pendingCommand=0;
    if(observing&&command!=layerwire::get&&command!=layerwire::catalogue)g_api->Log(g_mod,SDGO_LOG_INFO,"surface reply command=%04X status=%d bytes=%u",command,status,bytes);
    if(command==layerwire::catalogue){
        if(uid!=pageRecord.unitSerial||!editing)return;
        if(status||!data||bytes<20||layerwire::Get(data,4)||layerwire::Get(data+4,4)>1||layerwire::Get(data+8,4)<1||layerwire::Get(data+8,4)>20||layerwire::Get(data+12,4)>64||bytes!=20+layerwire::Get(data+12,4)*12){notice=L"纹章库读取失败，请稍后重试";catalogueWanted=false;return;}
        policy={layerwire::Get(data+4,4)!=0,unsigned(layerwire::Get(data+8,4))};std::uint32_t last=catalogueCursor;
        for(unsigned i=0;i<layerwire::Get(data+12,4);++i){const auto* p=data+20+i*12;layer::Stock item{unsigned(layerwire::Get(p,4)),unsigned(layerwire::Get(p+4,4)),layerwire::Get(p+8,4)!=0};
            if(item.sticker<=last||item.sticker>INT32_MAX||layerwire::Get(p+8,4)>1||stock.size()>=2048){stock.clear();catalogueWanted=false;notice=L"纹章库数据无效";return;}last=item.sticker;stock.push_back(item);}
        const auto next=unsigned(layerwire::Get(data+16,4));catalogueWanted=next!=0&&next==last&&next>catalogueCursor;catalogueCursor=next;
        page.SetStock(stock,policy);return;
    }
    if(command==layerwire::get&&status==static_cast<int>(WireStatus::invalid_argument)){legacyServer=true;notice=L"自由贴花需要更新配套服务端";retryAt=GetTickCount64()+30000;return;}
    layerwire::Response response;const bool valid=!status&&layerwire::DecodeResponse(data,bytes,response)&&response.authority.unitSerial==uid;
    if(!valid){
        if(command==layerwire::commit){editor.Timeout();notice=L"结果待确认，正在查询原保存凭据";retryAt=GetTickCount64()+1000;}
        else{InvalidateQuote();notice=L"服务暂未就绪，稍后重试";wanted.insert(uid);retryAt=GetTickCount64()+5000;}
        if(status==static_cast<int>(WireStatus::unit_unavailable)||status==static_cast<int>(WireStatus::not_authorized)){
            saved.erase(uid);wanted.erase(uid);if(editor.Preview().unitSerial==uid){Close();editor=layer::Editor{};}notice=L"机体归属已变化，请重新选择";}
        return;
    }
    policy=response.policy;page.SetStock(stock,policy);
    const auto previous=saved.find(uid);const auto& authority=response.authority;
    if(previous!=saved.end()&&(authority.revision<previous->second.appearance.revision||(authority.revision==previous->second.appearance.revision&&!layer::Same(authority,previous->second.appearance)))){
        if(command==layerwire::commit)editor.Timeout();InvalidateQuote();wanted.insert(uid);retryAt=GetTickCount64()+1000;notice=L"版本发生变化，正在重新读取";return;}
    saved[uid]={authority,GetTickCount64()};
    if(command==layerwire::quote){
        if(!editing||editor.Preview().unitSerial!=uid)return;
        if(response.result==layerwire::Result::ok&&layer::Same(authority,editor.Saved())){quoteToken=response.token;quotedCosts=response.costs;armed=true;page.SetCosts(quotedCosts);notice=policy.consume?L"请核对所列消耗，再确认保存":L"本次不消耗纹章，确认保存";}
        else{InvalidateQuote();notice=response.result==layerwire::Result::insufficient?L"纹章库存不足，请调整图层":L"机体已变化，请取消后重新编辑";RefreshStock();}return;
    }
    if(command==layerwire::commit){
        if(response.result==layerwire::Result::ok)lastCommit=response.token;
        if(response.result==layerwire::Result::ok&&std::any_of(response.costs.begin(),response.costs.end(),[](const auto& cost){return cost.required!=0;}))inventoryRefreshUntil=GetTickCount64()+15000;
        const auto result=response.result==layerwire::Result::ok?sdgo::surface_decal::Reply::saved:response.result==layerwire::Result::conflict?sdgo::surface_decal::Reply::conflict:sdgo::surface_decal::Reply::rejected;
        if(!editor.Acknowledge(request,result,authority)){editor.Timeout();wanted.insert(uid);notice=L"正在确认服务器版本";}
        else{InvalidateQuote();notice=result==sdgo::surface_decal::Reply::saved?L"贴花和库存已保存":response.result==layerwire::Result::insufficient?L"库存变化，未扣费或保存":L"请求未保存，请重新核对";RefreshStock();}
    }else if(editor.State()==EditState::uncertain&&editor.Preview().unitSerial==uid){if(editor.Reconcile(authority)){InvalidateQuote();notice=L"已读取服务器确认的外观";RefreshStock();}}
    else if(pageRecord.unitSerial==uid){if(!editing)notice=L"可添加最多二十层自由贴花";else if(!editor.Dirty())editor.Open(authority);}
}
void Save();
void Pump(){
    auto* nativeSession=session?session():nullptr;const int connection=nativeSession?Read<int>(nativeSession,0x14):-1;
    const auto key=connection<0?0:(std::uint64_t(reinterpret_cast<std::uintptr_t>(nativeSession))<<32)|std::uint32_t(connection);
    if(key!=sessionKey){Close();editor=layer::Editor{};saved.clear();wanted.clear();battleBindings.Clear();pageRecord={};pendingUid=0;pendingCommand=0;sessionKey=key;stock.clear();catalogueWanted=false;
        for(auto& item:models){item.second.uid=0;item.second.projected={};item.second.patches.clear();item.second.fixedPatches.clear();item.second.fixedDraws.clear();item.second.bindingsAt=0;}inventoryRefreshUntil=inventorySettleUntil=0;lastCommit={};legacyServer=false;notice=L"连接变化，正在重新读取";}
    RefreshNativeStock();
    while(saved.size()>256){auto oldest=saved.end();for(auto i=saved.begin();i!=saved.end();++i)if(i->first!=pageRecord.unitSerial&&i->first!=editor.Preview().unitSerial&&(oldest==saved.end()||i->second.fetched<oldest->second.fetched))oldest=i;if(oldest==saved.end())break;saved.erase(oldest);}
    while(wanted.size()>256){auto i=std::prev(wanted.end());if(*i==pageRecord.unitSerial){if(i==wanted.begin())break;--i;}wanted.erase(i);}
    const auto now=GetTickCount64();
    for(const auto& item:models){const auto& m=item.second;if(!m.uid||now-m.seenAt>1500)continue;const auto cache=saved.find(m.uid);if(cache==saved.end()||now-cache->second.fetched>=2500)wanted.insert(m.uid);}
    if(pendingUid||now<retryAt)return;
    if(!send||!transport){const auto carrier=GetModuleHandleW(L"friend_unit_gift_client.dll");if(carrier){send=reinterpret_cast<ClientExtensionSendFn>(GetProcAddress(carrier,SDGO_FRIEND_UNIT_GIFT_CLIENT_EXTENSION_SEND_EXPORT));transport=reinterpret_cast<ClientExtensionStateFn>(GetProcAddress(carrier,SDGO_FRIEND_UNIT_GIFT_CLIENT_EXTENSION_STATE_EXPORT));}}
    if(!send||!transport||transport()!=1)return;
    if(saveQueued&&editing&&editor.State()==EditState::editing){Save();return;}
    if(editor.State()==EditState::uncertain&&quoteToken!=layerwire::Token{}&&commitRetries<3){++commitRetries;Commit();return;}
    if(editing&&catalogueWanted){std::uint8_t bytes[12];layerwire::Put(bytes,pageRecord.unitSerial,8);layerwire::Put(bytes+8,catalogueCursor,4);Request(layerwire::catalogue,pageRecord.unitSerial,bytes,sizeof bytes);return;}
    if(wanted.empty())return;const auto uid=*wanted.begin();wanted.erase(wanted.begin());std::uint8_t bytes[8];layerwire::Put(bytes,uid,8);
    if(!Request(layerwire::get,uid,bytes,sizeof bytes)){wanted.insert(uid);retryAt=now+500;}
}
void Save(){
    if(observing)g_api->Log(g_mod,SDGO_LOG_INFO,"surface save armed=%u state=%u dirty=%u valid=%u pending=%04X transport=%d sequence=%llu",armed?1u:0u,unsigned(editor.State()),editor.Dirty()?1u:0u,layer::Valid(editor.Preview())?1u:0u,pendingCommand,transport?transport():-1,sequence);
    if(pendingUid||!transport||transport()!=1){saveQueued=true;notice=L"通信完成后继续核对…";return;}
    saveQueued=false;
    if(armed){layer::Design proposal;pendingRequest=++sequence;if(!editor.BeginSave(pendingRequest,proposal))return;commitRetries=0;Commit();if(!pendingUid)editor.Timeout();return;}
    layer::Design canonical;if(!layerwire::Canonical(editor.Preview(),canonical)||!editor.ReplaceCanonicalDraft(canonical)){notice=L"请先放置所有新增贴花";return;}
    std::vector<std::uint8_t> bytes;if(!layerwire::Encode(canonical,bytes))return;
    placing=false;RestoreInput();if(Request(layerwire::quote,canonical.unitSerial,bytes.data(),unsigned(bytes.size())))notice=L"正在核对消耗…";
}
bool PlaceAt(double x,double y){
    const auto* old=layer::Find(editor.Preview(),selected);
    if(!editing||!placing||armed||BlockingRequest()||editor.State()!=EditState::editing||!old)return false;
    const auto now=GetTickCount64();double nearest=std::numeric_limits<double>::infinity();Placement best;bool found=false;
    for(auto& item:models){auto& m=item.second;if(!SameStickers(editor.Preview(),m)||m.battle||m.room)continue;
        for(auto& n:m.nodes){if(now-n.viewed>150)continue;Ray ray;Hit hit;if(!ScreenRay(n.view,x,y,ray)||Pick(n.surface.mesh,ray,hit)!=Result::ok)continue;
            const auto length=std::hypot(ray.direction.x,ray.direction.y,ray.direction.z);
            const Vec3 point{ray.origin.x+ray.direction.x/length*hit.distance,ray.origin.y+ray.direction.y/length*hit.distance,ray.origin.z+ray.direction.z/length*hit.distance};
            Vec3 projected;if(!Transform(point,Multiply(Multiply(n.view.world,n.view.camera),n.view.projection),projected)||projected.z<0||projected.z>1||projected.z>=nearest)continue;
            Placement p;if(MakePlacement(n.surface.mesh,hit,n.surface.geometry,n.surface.index,{1,0,0},old->placed?old->placement.width:2,old->placed?old->placement.height:2,old->placed?old->placement.depth:.6,p)!=Result::ok)continue;
            if(old->placed)p.rotation=old->placement.rotation;Patch test;
            if((old->style.wrap?ProjectWrapped(n.surface.mesh,n.surface.geometry,n.surface.index,p,test):Project(n.surface.mesh,n.surface.geometry,n.surface.index,p,test))!=Result::ok)continue;
            best=p;nearest=projected.z;found=true;}}
    if(!found)return false;InvalidateQuote();notice=L"预览中，核对后保存";return editor.Place(selected,best);
}
void ApplyCommand(const ui::Command&);
void Action(ui::Action action){
    const auto* current=Authority(pageRecord.unitSerial);
    if(action==ui::Action::open){if(!current||(pendingUid&&pendingCommand!=layerwire::get)||legacyServer||editor.State()==EditState::uncertain||!editor.Open(*current))return;
        editing=true;placing=false;InvalidateQuote();inventorySettleUntil=GetTickCount64()+15000;selected=current->items.empty()?0:current->items.back().id;RefreshStock();page.Navigate(ui::Action::layers);notice=L"添加纹章，或选择图层调整";return;}
    if(!editing||editor.State()!=EditState::editing)return;
    if(page.Navigate(action))return;
    if(action==ui::Action::cancel&&armed){InvalidateQuote();notice=L"可继续编辑，尚未扣费";return;}
    if(action==ui::Action::cancel){Close();notice=L"已取消，未消耗任何纹章";return;}
    if(action==ui::Action::save){Save();return;}
    if(armed||BlockingRequest())return;
    if(action==ui::Action::add){page.Navigate(ui::Action::library);return;}
    const auto* item=layer::Find(editor.Preview(),selected);
    if(action==ui::Action::hideNative){editor.HideNative(editor.Preview().hiddenNative==0);notice=L"原生槽位显示已调整";return;}
    if(!item)return;
    if(action==ui::Action::place){placing=true;page.Navigate(ui::Action::properties);notice=L"左键放置，右键调整视角";return;}
    if(action==ui::Action::orbit){placing=false;dragging=mouseDown=false;notice=L"拖动调整视角";return;}
    if(action==ui::Action::duplicate){std::uint32_t added=0;if(editor.Preview().items.size()<policy.maxLayers&&editor.Duplicate(selected,added)){selected=added;page.Navigate(ui::Action::properties);notice=L"已复制，保存时核对消耗";}return;}
    if(action==ui::Action::remove){editor.Remove(selected);selected=editor.Preview().items.empty()?0:editor.Preview().items.back().id;notice=L"已删除图层，保存后生效";return;}
    if(action>=ui::Action::top&&action<=ui::Action::bottom){const auto& items=editor.Preview().items;const auto at=std::size_t(item-items.data()),last=items.size()-1;
        const auto to=action==ui::Action::top?last:action==ui::Action::bottom?0:action==ui::Action::raise?std::min(last,at+1):at?at-1:0;editor.Move(selected,to);notice=L"排序已调整，保存后同步";return;}
    if(action==ui::Action::visible){editor.Visible(selected,!item->visible);return;}
    if(action==ui::Action::wrap||action==ui::Action::pattern){auto style=item->style;if(action==ui::Action::wrap)style.wrap=!style.wrap;else style.seed+=0x9e3779b9u;editor.StyleValue(selected,style);return;}
}
void ApplyCommand(const ui::Command& command){
    if(command.action==ui::Action::none||!editing||armed||BlockingRequest()||editor.State()!=EditState::editing)return;
    if(command.action==ui::Action::choose){const auto i=std::find_if(stock.begin(),stock.end(),[&](const auto& x){return x.sticker==command.slot;});
        if(i==stock.end()||(policy.consume&&!i->remaining)||editor.Preview().items.size()>=policy.maxLayers)return;
        if(!textures.Get(command.slot)){notice=L"纹章素材未就绪，请更新客户端包";return;}
        std::uint32_t id=0;if(editor.Add(command.slot,id)){selected=id;placing=true;page.Navigate(ui::Action::properties);notice=L"在机体上点击放置新贴花";}return;}
    if(command.action==ui::Action::select){if(layer::Find(editor.Preview(),command.slot))selected=command.slot;return;}
    const auto* item=layer::Find(editor.Preview(),command.slot);if(!item)return;selected=command.slot;
    if(command.action==ui::Action::reorder)editor.Move(selected,unsigned(command.value));
    else if(command.action==ui::Action::wear){auto style=item->style;style.wear=std::uint16_t(std::lround(command.value*1000));editor.StyleValue(selected,style);}
    else if(item->placed){auto p=item->placement;if(command.action==ui::Action::rotation)p.rotation=(command.value-.5)*6.283185307179586;
        else if(command.action==ui::Action::size){const auto width=.2+7.8*command.value;p.height*=width/p.width;p.width=width;}else return;
        if(p.width<=30&&p.height<=30)editor.Place(selected,p);}
    notice=L"调整参数不重复扣费";
}
bool LogicalPoint(HWND window,LPARAM lparam,float& x,float& y){
    RECT rect{};if(!GetClientRect(window,&rect)||rect.right<=0||rect.bottom<=0)return false;
    x=GET_X_LPARAM(lparam)*1024.f/rect.right;y=GET_Y_LPARAM(lparam)*768.f/rect.bottom;return true;
}
LRESULT CALLBACK InputProc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(message==WM_NCDESTROY){pointerOwned=false;page.PointerUp();RemoveWindowSubclass(window,&InputProc,inputSubclass);inputWindow=nullptr;return DefSubclassProc(window,message,wp,lp);}
    if(message==WM_KILLFOCUS||message==WM_CANCELMODE||message==WM_CAPTURECHANGED){
        std::lock_guard<std::recursive_mutex> hold(gate);const bool owned=pointerOwned;page.PointerUp();pointerOwned=false;
        if(message!=WM_CAPTURECHANGED&&owned&&GetCapture()==window)ReleaseCapture();
    }
    if(message==WM_MOUSEWHEEL){std::lock_guard<std::recursive_mutex> hold(gate);POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(window,&point);RECT rect{};GetClientRect(window,&rect);
        if(rect.right>0&&rect.bottom>0&&page.Scroll(point.x*1024.f/rect.right,point.y*768.f/rect.bottom,GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA))return 0;}
    if(message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK||message==WM_MOUSEMOVE||message==WM_LBUTTONUP){
        std::lock_guard<std::recursive_mutex> hold(gate);float x=0,y=0;
        if(editing&&page.Visible()&&LogicalPoint(window,lp,x,y)){
            if(message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK){
                const auto command=page.PointerDown(x,y);ApplyCommand(command);
                if(page.Captured()){pointerOwned=true;SetCapture(window);return 0;}
            }else if(message==WM_MOUSEMOVE){const auto command=page.PointerMove(x,y);if(pointerOwned){ApplyCommand(command);return 0;}}
            else if(pointerOwned){page.PointerUp();pointerOwned=false;if(GetCapture()==window)ReleaseCapture();return 0;}
        }else if(pointerOwned){page.PointerUp();pointerOwned=false;if(GetCapture()==window)ReleaseCapture();return 0;}
    }
    return DefSubclassProc(window,message,wp,lp);
}
IDirect3DBaseTexture9* Thumbnail(unsigned sticker){
    std::lock_guard<std::recursive_mutex> hold(gate);if(auto* texture=textures.Get(sticker)){texture->AddRef();return texture;}return nullptr;
}
std::wstring StickerName(unsigned sticker){std::lock_guard<std::recursive_mutex> hold(gate);return textures.Name(sticker);}
void BindFactory(void* factory,const Appearance& record){
    auto* multi=page.Preview(factory);if(!multi)return;
    auto** first=Read<void**>(multi,0x21c);auto** end=Read<void**>(multi,0x220);
    if(!first||end<first||end-first>8)return;
    for(auto** p=first;p!=end;++p){if(!*p)continue;auto* model=Read<void*>(*p,0x34);auto found=models.find(model);if(found==models.end())continue;
        auto& m=found->second;if(m.uid!=record.unitSerial||m.type!=record.unitType){m.uid=record.unitSerial;m.type=record.unitType;m.projected={};wanted.insert(m.uid);}
    }
    if(editing&&!inputHeld){preview=multi;previewInput=Read<std::uint8_t>(multi,0x263);previewPitch=Read<std::uint8_t>(multi,0x264);
        Put<std::uint8_t>(multi,0x263,1);Put<std::uint8_t>(multi,0x264,1);inputHeld=true;}
}
void TickPage(void* service){
    auto* factory=Service(factoryTable);if(!factory)return;
    auto* root=Read<void*>(service,0xd0);if(!root||!Read<std::uint8_t>(service,0xac)||!Read<std::uint8_t>(root,0x162)){
        if(page.Owner()==service&&editing){Close();page.Render(false,false,selected,false,false,false,Authority(pageRecord.unitSerial),L"未保存的位置已取消");}return;
    }
    auto* source=page.Footer(factory,0);
    if(!page.Attach(service,source))return;
    page.SetThumbnailSource(&Thumbnail);page.SetNameSource(&StickerName);
    Appearance record;if(!Record(static_cast<std::uint8_t*>(service)+0xf8,record))return;
    if(pageRecord.unitSerial!=record.unitSerial||!SameContent(pageRecord,record)){
        Close();pageRecord=record;wanted.insert(record.unitSerial);notice=L"正在读取机体贴花位置…";
    }
    BindFactory(factory,record);
    const auto* current=Authority(record.unitSerial);
    // Native selection/replacement remains native. A draft never uses another
    // sticker's texture, nor can it silently become a save for the new unit.
    if(editing&&current){for(unsigned i=0;i<3;++i)if(record.slots[i].sticker!=current->equipped[i]){Close();wanted.insert(record.unitSerial);notice=L"请先完成原生贴纸保存";break;}}
    if(editing&&placing&&!pointerOwned){
        auto* application=app?app():nullptr;const HWND window=application?Read<HWND>(application,0x3c):nullptr;
        DWORD owner=0;GetWindowThreadProcessId(GetForegroundWindow(),&owner);
        if(window&&owner==GetCurrentProcessId()){
            POINT point{};GetCursorPos(&point);ScreenToClient(window,&point);
            const bool down=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
            const bool orbit=(GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0;
            if(orbit)dragging=false;
            else if(down&&!mouseDown){RECT client{};GetClientRect(window,&client);
                const float lx=client.right>0?point.x*1024.f/client.right:0,ly=client.bottom>0?point.y*768.f/client.bottom:0;
                if(!page.Contains(lx,ly))dragging=PlaceAt(point.x,point.y);
            }
            else if(down&&dragging)PlaceAt(point.x,point.y);
            if(!down)dragging=false;mouseDown=down;
        }else{dragging=false;mouseDown=false;}
    }
    const bool busy=saveQueued||(pendingUid&&pendingCommand!=layerwire::get)||editor.State()==EditState::uncertain;
    if(editing){
        if(footer.factory!=factory){
            RestoreFooter();footer.factory=factory;
            for(unsigned i=0;i<2;++i){footer.buttons[i]=page.Footer(factory,i);if(footer.buttons[i])footer.visible[i]=Read<std::uint8_t>(footer.buttons[i],0x162);}
        }
        for(auto* button:footer.buttons)if(button)Put<std::uint8_t>(button,0x162,0);
    }
    page.Render(editing,placing,selected,busy,editor.Dirty(),armed,editing?&editor.Preview():current,notice);
    inventoryBlocked=editing&&page.Visible();
    if(!inputWindow&&app&&app()){const HWND window=Read<HWND>(app(),0x3c);DWORD pid=0;const auto thread=GetWindowThreadProcessId(window,&pid);
        if(pid==GetCurrentProcessId()&&thread==GetCurrentThreadId()&&SetWindowSubclass(window,&InputProc,inputSubclass,0))inputWindow=window;}
}
void CALLBACK Timer(HWND,UINT,UINT_PTR,DWORD){
    try{std::lock_guard<std::recursive_mutex> hold(gate);Pump();
        if(pendingUid&&GetTickCount64()-pendingAt>15000){
            const auto uid=pendingUid;const auto command=pendingCommand;pendingUid=0;pendingCommand=0;pendingToken=++nextToken;
            if(command==layerwire::commit){editor.Timeout();notice=L"保存待确认，正在重新读取";}
            else notice=L"读取暂时超时，正在重试";
            wanted.insert(uid);retryAt=GetTickCount64()+500;
        }
    }catch(...){ }
}
void ArmTimer(){
    if(!timer&&g_api->size>=offsetof(SdgoApi,process_initial_thread_id)+sizeof(DWORD)&&g_api->process_initial_thread_id==GetCurrentThreadId())timer=SetTimer(nullptr,0,100,&Timer);
}
void __stdcall CaptureHuman(const std::uint8_t* frame){
    LastErrorGuard error;
    try{
        std::array<std::uint8_t,24> args{};if(!Copy(frame,args.data(),args.size()))return;
        const auto* source=Read<const std::uint8_t*>(args.data(),12);
        const auto* entry=Read<const std::uint8_t*>(args.data(),16);
        const unsigned deck=Read<unsigned>(args.data(),20)&255;
        std::uint32_t owner=0,sourceOwner=0;
        if(!Copy(entry,&owner,4)||!owner||(owner&0x80000000u))return;
        std::array<Appearance,4> records{};unsigned count=0;
        if(source&&Copy(source+0x429,&sourceOwner,4)&&sourceOwner==owner&&deck<=1){
            count=deck?4:1;
            for(unsigned i=0;i<count;++i)Record(source+(deck?0x19e+i*0x99:0x49),records[i]);
        }
        std::lock_guard<std::recursive_mutex> hold(gate);battleBindings.Replace(owner,records,count);
        for(unsigned i=0;i<count;++i)if(records[i].unitSerial){
            wanted.insert(records[i].unitSerial);
        }
        if(observing)g_api->Log(g_mod,SDGO_LOG_INFO,"surface native human identities count=%u",count);
    }catch(...){ }
}
void __stdcall CaptureBattleStyle(const std::uint8_t* frame){
    LastErrorGuard error;
    try{
        std::array<std::uint8_t,20> args{};if(!Copy(frame,args.data(),args.size()))return;
        const auto* record=Read<const std::uint8_t*>(args.data(),12);
        const auto* alternate=Read<const std::uint8_t*>(args.data(),16);
        const auto styleKey=reinterpret_cast<std::uintptr_t>(alternate?alternate:record)+(alternate?0x10:0x54);
        {std::lock_guard<std::recursive_mutex> hold(gate);battleBindings.ForgetStyle(styleKey);}
        std::array<std::uint8_t,0x344> bytes{};if(!Copy(record,bytes.data(),bytes.size()))return;
        unsigned slot=0;
        if(alternate){
            const auto start=reinterpret_cast<std::uintptr_t>(record)+0x44;
            const auto at=reinterpret_cast<std::uintptr_t>(alternate);
            if(at<start||(at-start)%0xb4||(at-start)/0xb4>=4)return;
            slot=static_cast<unsigned>((at-start)/0xb4);
        }
        const auto offset=0x44+slot*0xb4;
        const auto type=Read<unsigned>(bytes.data(),offset);
        const auto ids=Read<std::array<std::uint32_t,3>>(bytes.data(),offset+0x24);
        const auto colors=reinterpret_cast<std::uintptr_t>(record)+offset+0x10;
        std::lock_guard<std::recursive_mutex> hold(gate);
        const bool bound=battleBindings.Prepare(Read<unsigned>(bytes.data()),slot,type,ids,colors);
        if(observing)g_api->Log(g_mod,SDGO_LOG_INFO,"surface native battle identity slot=%u bound=%u",slot,bound?1u:0u);
    }catch(...){ }
}
#define SURFACE_CONTEXT_SITE(Name, Observe, Original) \
__declspec(naked) void Name(){ \
    __asm { pushfd } __asm { pushad } __asm { mov eax,esp } \
    __asm { sub esp,210h } __asm { and esp,0FFFFFFF0h } \
    __asm { mov [esp+200h],eax } __asm { fxsave [esp] } \
    __asm { fninit } __asm { ldmxcsr dword ptr [defaultMxcsr] } __asm { cld } \
    __asm { push ebp } __asm { call Observe } __asm { fxrstor [esp] } \
    __asm { mov esp,[esp+200h] } __asm { popad } __asm { popfd } \
    __asm { jmp dword ptr [Original] } \
}
SURFACE_CONTEXT_SITE(HumanSite,CaptureHuman,humanSiteOriginal)
SURFACE_CONTEXT_SITE(BattleStyleSite,CaptureBattleStyle,battleStyleOriginal)
#undef SURFACE_CONTEXT_SITE
// Native CUIMulti3DRender has already latched its mouse buttons here, before
// updating the model transform under its native lock. Reuse that operation,
// including pitch, instead of writing model transforms.
void __stdcall PreviewInput(void* control){
    LastErrorGuard error;
    try{std::lock_guard<std::recursive_mutex> hold(gate);
        if(!editing||!inputHeld||control!=preview)return;
        if(pointerOwned){Put<std::uint8_t>(control,0x261,0);Put<std::uint8_t>(control,0x262,0);return;}
        const bool right=Read<std::uint8_t>(control,0x261)!=0;
        const bool left=Read<std::uint8_t>(control,0x262)!=0;
        Put<std::uint8_t>(control,0x262,(right||(!placing&&left))?1:0);
    }catch(...){ }
}
__declspec(naked) void PreviewInputSite(){
    __asm { pushfd } __asm { pushad } __asm { mov eax,esp }
    __asm { sub esp,210h } __asm { and esp,0FFFFFFF0h }
    __asm { mov [esp+200h],eax } __asm { fxsave [esp] }
    __asm { fninit } __asm { ldmxcsr dword ptr [defaultMxcsr] } __asm { cld }
    __asm { push esi } __asm { call PreviewInput } __asm { fxrstor [esp] }
    __asm { mov esp,[esp+200h] } __asm { popad } __asm { popfd }
    __asm { jmp dword ptr [previewInputOriginal] }
}
int __stdcall OwnPreviewInput(void* control){
    LastErrorGuard error;
    try{std::lock_guard<std::recursive_mutex> hold(gate);return editing&&inputHeld&&control==preview;}catch(...){return 0;}
}
// This generic widget's native wheel branch clamps model scale to 5.5..9.5
// on every hovered frame, even with no wheel movement. Factory previews use
// their own larger framing. Enable native orbit without that unrelated clamp.
__declspec(naked) void PreviewZoomSite(){
    __asm { pushfd } __asm { pushad } __asm { mov eax,esp }
    __asm { sub esp,210h } __asm { and esp,0FFFFFFF0h }
    __asm { mov [esp+200h],eax } __asm { fxsave [esp] }
    __asm { fninit } __asm { ldmxcsr dword ptr [defaultMxcsr] } __asm { cld }
    __asm { push esi } __asm { call OwnPreviewInput } __asm { test eax,eax } __asm { jz nativeZoom }
    __asm { fxrstor [esp] } __asm { mov esp,[esp+200h] } __asm { popad } __asm { popfd }
    __asm { jmp dword ptr [previewZoomEnd] }
    __asm { nativeZoom: }
    __asm { fxrstor [esp] } __asm { mov esp,[esp+200h] } __asm { popad } __asm { popfd }
    __asm { jmp dword ptr [previewZoomOriginal] }
}
LRESULT CALLBACK GameThreadMessage(int code,WPARAM wparam,LPARAM lparam){
    const auto last=GetLastError();
    // A thread-local message hook establishes the timer without taking over
    // another Mod's game.app detour. Remove it once the UI timer is running.
    if(code>=0){ArmTimer();if(timer&&messageHook){auto hook=messageHook;messageHook=nullptr;UnhookWindowsHookEx(hook);}}
    SetLastError(last);return CallNextHookEx(nullptr,code,wparam,lparam);
}
bool PackedStyleCaller(std::uintptr_t caller){
    // These native previews pass a complete packed 0x99-byte owned-unit
    // record. Battle actors use a different appearance layout and must never
    // infer a serial from bytes preceding their colour array.
    switch(caller){
    case 0x1f64ad:
    // Ctor 0x61b7a0 copies all 0x99 bytes to this+0x68; its model
    // renderer passes colours at +0x96, not a condensed battle record.
    case 0x21c55f:
    case 0x369a6e:case 0x36b7d4:case 0x36c22c:
    case 0x3c896e:case 0x3c90a6:case 0x3c95eb:
    // The shared preview job copies 0x99 bytes to job+0x10. The unit
    // inventory renderer obtains the same record through CUnitInfo::GetData.
    case 0x3f5650:case 0x56ebb4:
    case 0x445054:case 0x4457c6:case 0x445d0b:
    case 0x461b53:case 0x472760:
    case 0x4b32b8:case 0x4b5b82:
    case 0x53aae6:case 0x53b02b:case 0x53bdf4:
    case 0x614dd6:case 0x61cb47:
        return true;
    default:return false;
    }
}
void __cdecl StyleHook(void* model,const std::uint16_t* colors,std::uint16_t shine,const std::uint32_t* decals){
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-reinterpret_cast<std::uintptr_t>(g_api->host_base);
    styleOriginal(model,colors,shine,decals);const auto last=GetLastError();
    try{
        std::vector<native::Node> nodes;const auto result=geometry.Read(model,nodes);
        std::array<std::uint32_t,3> ids{};if(!Copy(decals,ids.data(),sizeof ids)){SetLastError(last);return;}
        Appearance record;bool bound=false,battle=false,room=false;
        if(PackedStyleCaller(caller)&&reinterpret_cast<const std::uint8_t*>(decals)==reinterpret_cast<const std::uint8_t*>(colors)+14)
            bound=Record(reinterpret_cast<const std::uint8_t*>(colors)-0x2e,record);
        std::lock_guard<std::recursive_mutex> hold(gate);
        if(caller==0x326104&&reinterpret_cast<const std::uint8_t*>(decals)==reinterpret_cast<const std::uint8_t*>(colors)+16)
            room=bound=RoomRecord(model,colors,ids,record);
        if((caller==0x6c9d31||caller==0x6c9d4a||caller==0x103af6||caller==0x103b13)&&
           reinterpret_cast<const std::uint8_t*>(decals)==reinterpret_cast<const std::uint8_t*>(colors)+20)
            battle=bound=battleBindings.Resolve(reinterpret_cast<std::uintptr_t>(colors),record);
        if(observing)g_api->Log(g_mod,SDGO_LOG_INFO,"surface style caller=%08X bound=%u battle=%u room=%u geometry=%u",static_cast<unsigned>(caller),bound?1u:0u,battle?1u:0u,room?1u:0u,static_cast<unsigned>(result));
        // Styling can run before animation/parent binding, including previews.
        // Refresh geometry and accumulated poses in the first live draw scope.
        Model value;value.stickers=ids;value.geometryPending=true;
        value.battle=battle;value.room=room;
        if(bound){value.uid=record.unitSerial;value.type=record.unitType;wanted.insert(value.uid);}
        if(result==native::ReadResult::ok)for(auto& n:nodes)value.nodes.push_back({std::move(n),{},0});
        if(models.size()>=256&&models.find(model)==models.end())models.erase(models.begin());
        models[model]=std::move(value);ArmTimer();
    }catch(...){ }
    SetLastError(last);
}
void __fastcall ModelDestroyHook(void* self,void*){{std::lock_guard<std::recursive_mutex> hold(gate);models.erase(self);}modelDestroyOriginal(self);}
bool BattleSurfaceReady(void* owner,const Model& model,unsigned index,const Patch& patch){
    if(!model.battle)return true;
    auto** first=Read<void**>(owner,0x124);auto** end=Read<void**>(owner,0x128);
    if(!first||end<first||end-first>4096||index>=static_cast<unsigned>(end-first))return false;
    auto* node=first[index];
    if(!node||!render::OpaqueNode(Read<std::uint8_t>(node,0x108)!=0,Read<unsigned>(node,0x204)))return false;
    auto* common=Read<void*>(node,0x11c);if(!common)return false;
    const auto count=Read<unsigned>(common,0x194);const auto* groups=Read<std::uint8_t*>(common,0x198);
    if(!groups||count>256)return false;
    const auto surface=std::find_if(model.nodes.begin(),model.nodes.end(),[&](const Node& n){return n.surface.index==index;});
    if(surface==model.nodes.end())return false;
    return render::OpaqueCoverage(patch,surface->surface.triangleGroups,count,
        [&](unsigned group,float& alpha){
            const auto id=Read<unsigned>(groups,group*12+8);if(id>=7&&id<=9)return false;
            auto* m=material(owner,id,0,1);if(!m)return false;
            alpha=Read<float>(m,0x10);return true;
        },0.f);
}
bool BattleSurfaceReady(void* owner,const Model& model,const layer::Layer& item,const Patch& patch){return BattleSurfaceReady(owner,model,item.placement.anchor.node,patch);}
void RefreshFixedBindings(void* owner,Model& model,std::uint64_t now){
    if(now-model.bindingsAt<1000)return;model.bindingsAt=now;
    std::vector<native::FixedBinding> bindings;
    const bool ready=geometry.ReadFixedBindings(owner,bindings)==native::ReadResult::ok;
    bool changed=false;for(const auto& n:model.nodes){const auto group=ready&&n.surface.index<bindings.size()?bindings[n.surface.index].group:0;changed=changed||group!=n.surface.fixedGroup;}
    if(!changed)return;
    std::vector<native::Node> fresh;
    if(ready&&geometry.Read(owner,fresh)==native::ReadResult::ok){
        std::vector<Node> next;next.reserve(fresh.size());
        for(auto& n:fresh){Node value;const auto old=std::find_if(model.nodes.begin(),model.nodes.end(),[&](const auto& prior){return prior.surface.index==n.index;});
            if(old!=model.nodes.end()){value.view=old->view;value.viewed=old->viewed;}value.surface=std::move(n);next.push_back(std::move(value));}
        model.nodes=std::move(next);
    }else for(auto& n:model.nodes)n.surface.fixedGroup=0;
    model.projected={};model.patches.clear();model.fixedPatches.clear();model.fixedDraws.clear();
}
void RenderNode(void* self,void* entry,unsigned,bool before,bool& suppress){
    // The callback's texture bit is a state-change optimization, not visibility.
    if(Read<std::uint8_t>(self,0x108))return;
    auto* owner=Read<void*>(self,0x118);auto found=models.find(owner);if(found==models.end())return;
    auto& m=found->second;m.seenAt=GetTickCount64();const auto* appearance=AppearanceFor(m);if(!appearance)return;
    // Battle and waiting-room styling can precede asynchronous model loading.
    // Resolve geometry on the native rendering thread once the model actually
    // draws; never freeze an empty projection from its construction callback.
    const auto now=GetTickCount64();
    if(!before&&m.geometryPending&&now>=m.geometryRetry){
        m.geometryRetry=now+250;std::vector<native::Node> nodes;
        if(geometry.Read(owner,nodes)==native::ReadResult::ok){
            m.nodes.clear();for(auto& n:nodes)m.nodes.push_back({std::move(n),{},0});
            m.geometryPending=false;m.projected={};m.patches={};m.fixedPatches.clear();m.fixedDraws.clear();
        }
    }
    if(!m.geometryPending)RefreshFixedBindings(owner,m,now);
    Rebuild(owner,m,*appearance);
    if(!SameStickers(*appearance,m))return;
    auto* common=Read<void*>(self,0x11c);const auto group=Read<std::uint8_t>(entry,0x14);
    if(group>=Read<unsigned>(common,0x194))return;
    const auto* groups=Read<std::uint8_t*>(common,0x198);const auto mat=Read<unsigned>(groups,group*12+8);
    if(before){
        if(mat>=7&&mat<=9&&(appearance->hiddenNative&(1u<<(mat-7)))){
            const auto source=std::find_if(appearance->items.begin(),appearance->items.end(),[&](const auto& l){return l.source==mat-7;});
            if(source==appearance->items.end()||!source->visible)suppress=true;
            else{const auto patch=m.patches.find(source->id);auto* device=renderer&&*renderer?Read<IDirect3DDevice9*>(*renderer,8):nullptr;
                if(patch!=m.patches.end()&&!patch->second.vertices.empty()&&textures.Get(source->sticker)&&render::CanDraw(device,source->style)&&BattleSurfaceReady(owner,m,*source,patch->second))suppress=true;}
        }return;
    }
    auto** first=Read<void**>(owner,0x124);auto** end=Read<void**>(owner,0x128);if(!first||end<first||end-first>4096)return;
    unsigned index=0;while(first+index!=end&&first[index]!=self)++index;
    auto n=std::find_if(m.nodes.begin(),m.nodes.end(),[&](const Node& node){return node.surface.index==index;});if(n==m.nodes.end())return;
    auto* device=renderer&&*renderer?Read<IDirect3DDevice9*>(*renderer,8):nullptr;
    if(!render::Capture(device,Read<std::array<float,16>>(self,0x130),n->view))return;
    n->viewed=GetTickCount64();
    // One fresh ownership/animation snapshot is shared only by this native
    // node/material draw callback, never by a later frame or another actor.
    std::vector<native::FixedBinding> currentBindings;
    const bool derivedHere=std::any_of(m.fixedPatches.begin(),m.fixedPatches.end(),[&](const auto& p){return p.first.second==index;});
    const bool bindingsReady=derivedHere&&geometry.ReadFixedBindings(owner,currentBindings)==native::ReadResult::ok;
    Matrix targetPose{};const bool targetPoseReady=derivedHere&&CurrentModelPose(owner,index,targetPose);
    std::map<unsigned,Matrix> relativePoses;std::set<unsigned> invalidPoses;
    for(const auto& item:appearance->items){
        if(!item.visible||!item.placed)continue;
        const Patch* projected=nullptr;bool derived=false;
        if(item.placement.anchor.node==index){const auto p=m.patches.find(item.id);if(p!=m.patches.end())projected=&p->second;}
        else{const auto p=m.fixedPatches.find({item.id,index});
            if(p!=m.fixedPatches.end()&&p->second.geometry==n->surface.geometry&&bindingsReady&&index<currentBindings.size()&&item.placement.anchor.node<currentBindings.size()){
                const auto expected=p->second.group;const auto& target=currentBindings[index];const auto& anchor=currentBindings[item.placement.anchor.node];
                if(expected&&target.valid&&anchor.valid&&target.group==expected&&anchor.group==expected&&targetPoseReady){
                    const auto anchorIndex=item.placement.anchor.node;if(!relativePoses.count(anchorIndex)&&!invalidPoses.count(anchorIndex)){
                        Matrix pose{},inverse{};if(CurrentModelPose(owner,anchorIndex,pose)&&InvertModelPose(pose,inverse))relativePoses[anchorIndex]=Multiply(targetPose,inverse);else invalidPoses.insert(anchorIndex);}
                    const auto pose=relativePoses.find(anchorIndex);if(pose!=relativePoses.end()&&StableFixedPose(p->second,pose->second,item.placement)){projected=&p->second.patch;derived=true;}}}
        }
        if(!projected||projected->vertices.empty())continue;
        float opacity=1;
        if(!BattleSurfaceReady(owner,m,index,*projected))continue;
        if(m.battle){
            if(!render::OpaqueNode(Read<std::uint8_t>(self,0x108)!=0,Read<unsigned>(self,0x204)))continue;
            auto* nativeMaterial=material(owner,mat,0,1);if(!nativeMaterial)continue;opacity=Read<float>(nativeMaterial,0x10);
            if(!std::isfinite(opacity)||opacity<=0||opacity>1)continue;
        }
        Patch part;for(const auto& vertex:projected->vertices)
            if(vertex.triangle<n->surface.triangleGroups.size()&&n->surface.triangleGroups[vertex.triangle]==group)part.vertices.push_back(vertex);
        if(!part.vertices.empty())if(auto* texture=textures.Get(item.sticker))if(render::Draw(device,texture,part,n->view,item.style,opacity)){++drawCount;++m.draws;++m.layerDraws[item.id];if(derived)++m.fixedDraws[{item.id,index}];if(m.battle)++battleDrawCount;}
    }
}
bool __fastcall IndexedHook(void* self,void*,unsigned primitive,int base,unsigned minimum,unsigned vertices,unsigned first,unsigned count){
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-reinterpret_cast<std::uintptr_t>(g_api->host_base);
    if(suppressNative&&caller==0x817e8a)return true;
    return indexedOriginal(self,primitive,base,minimum,vertices,first,count);
}
void __fastcall CallbackHook(void* self,void*,void* entry,unsigned flags){
    bool suppress=false;
    try{std::lock_guard<std::recursive_mutex> hold(gate);ArmTimer();RenderNode(self,entry,flags,true,suppress);}catch(...){suppress=false;}
    const bool previous=suppressNative;suppressNative=suppress;
    callbackOriginal(self,entry,flags);suppressNative=previous;
    const auto last=GetLastError();
    try{std::lock_guard<std::recursive_mutex> hold(gate);RenderNode(self,entry,flags,false,suppress);}catch(...){ }
    SetLastError(last);
}
void __fastcall UpdateHook(void* self,void*){
    pageUpdateOriginal(self);const auto last=GetLastError();
    if(!page.Ready(self)){
        std::lock_guard<std::recursive_mutex> hold(gate);
        if(page.Owner()==self)Close();
        SetLastError(last);return;
    }
    // Model loading/rendering holds this native lock before entering our
    // styling callback. Keep the same lock order while reading its vector.
    auto* factory=Service(factoryTable);auto* multi=page.Preview(factory);
    if(!multi){SetLastError(last);return;}
    std::array<void*,8> locked{};unsigned count=0;
    if(multi){auto** first=Read<void**>(multi,0x21c);auto** end=Read<void**>(multi,0x220);
        if(first&&end>=first&&end-first<=8)for(auto** p=first;p!=end;++p)if(*p){previewLock(*p);locked[count++]=*p;}
    }
    try{std::lock_guard<std::recursive_mutex> hold(gate);ArmTimer();TickPage(self);}catch(...){Close();page.Detach();}
    while(count)previewUnlock(locked[--count]);SetLastError(last);
}
void* __fastcall PageDestroyHook(void* self,void*,unsigned flags){{std::lock_guard<std::recursive_mutex> hold(gate);if(page.Owner()==self){Close();page.Detach();pageRecord={};}}return pageDestroyOriginal(self,flags);}
void __stdcall EventHook(void* service,unsigned event,std::uintptr_t second,void* control){
    {std::lock_guard<std::recursive_mutex> hold(gate);if(page.Owner()==service){const auto action=page.Click(control);if(action!=ui::Action::none){if(event==0x101||event==0x803){if(action==ui::Action::select)ApplyCommand(page.Choose(control));else Action(action);}return;}}}
    eventOriginal(service,event,second,control);
}
}
namespace {
bool NativeTest(){
    wchar_t run[64]{};
    return g_api&&g_api->size>=offsetof(SdgoApi,process_initial_thread_id)+sizeof(DWORD)&&
        g_api->process_initial_thread_id==GetCurrentThreadId()&&GetModuleHandleW(L"game_test_client.dll")&&
        GetEnvironmentVariableW(L"SDGO_GAME_TEST_RUN",run,64)>0;
}
}
extern "C" __declspec(dllexport) int __cdecl SdgoSurfaceDecalTestObserveV1(test::Snapshot* output){
    if(!NativeTest()||!output||output->size!=sizeof(*output)||output->version!=1)return 0;
    std::lock_guard<std::recursive_mutex> hold(gate);test::Snapshot s;
    s.editing=editing;s.placing=placing;s.state=static_cast<unsigned>(editor.State());s.pending=pendingCommand;s.selected=selected;s.draws=drawCount;
    s.battleDraws=battleDrawCount;
    if(const auto* a=Authority(pageRecord.unitSerial))decalwire::Encode(layer::ToLegacy(*a),s.saved);
    if(editing)decalwire::Encode(layer::ToLegacy(editor.Preview()),s.preview);
    const auto now=GetTickCount64();for(const auto& item:models)if(item.second.uid==pageRecord.unitSerial){
        for(const auto& n:item.second.nodes)if(now-n.viewed<150)++s.freshViews;
        for(const auto& patch:item.second.patches)s.triangles+=static_cast<unsigned>(patch.second.vertices.size()/3);
    }
    for(const auto& item:models)if(item.second.uid){
        ++s.boundModels;
        if(item.second.battle)++s.battleModels;
        const auto* a=Authority(item.second.uid);
        if(a&&SameStickers(*a,item.second))for(const auto& layer:a->items)if(layer.placed){++s.positionedModels;if(item.second.battle)++s.battlePositioned;}
    }
    *output=s;return 1;
}
extern "C" __declspec(dllexport) int __cdecl SdgoSurfaceDecalTestPlaceV1(double x,double y){
    if(!NativeTest())return 0;std::lock_guard<std::recursive_mutex> hold(gate);return PlaceAt(x,y)?1:0;
}
extern "C" __declspec(dllexport) int __cdecl SdgoSurfaceDecalTestLayersV2(std::uint64_t uid,test::LayerSnapshot* out){
    if(!NativeTest()||!out||out->size!=sizeof(*out)||out->version!=2)return 0;
    std::lock_guard<std::recursive_mutex> hold(gate);test::LayerSnapshot value;
    if(!uid)uid=pageRecord.unitSerial;const auto* a=Authority(uid);std::vector<std::uint8_t> bytes;
    if(a&&layerwire::Encode(*a,bytes)){value.savedBytes=unsigned(bytes.size());std::copy(bytes.begin(),bytes.end(),value.saved.begin());}
    if(editing&&editor.Preview().unitSerial==uid){value.editing=1;value.placing=placing;value.state=unsigned(editor.State());value.pending=pendingCommand;value.selected=selected;value.armed=armed;value.stockTypes=unsigned(stock.size());
        value.previewLayers=unsigned(editor.Preview().items.size());for(const auto& c:quotedCosts)value.quotedItems+=c.required;
        if(layerwire::Encode(editor.Preview(),bytes)){value.previewBytes=unsigned(bytes.size());std::copy(bytes.begin(),bytes.end(),value.preview.begin());}}
    const auto* design=editing&&editor.Preview().unitSerial==uid?&editor.Preview():a;
    if(design)for(std::size_t i=0;i<design->items.size();++i)value.layerIDs[i]=design->items[i].id;
    for(const auto& entry:models){const auto& m=entry.second;if(m.uid!=uid)continue;++value.models;value.draws+=m.draws;if(m.room)value.roomDraws+=m.draws;if(m.battle)value.battleDraws+=m.draws;
        if(observing&&m.battle&&design&&m.draws==0){
            static std::map<std::uint64_t,std::uint64_t> logged;const auto now=GetTickCount64();
            if(now-logged[uid]>5000){logged[uid]=now;auto** first=Read<void**>(entry.first,0x124);auto** end=Read<void**>(entry.first,0x128);
                for(const auto& item:design->items){if(!item.placed||!item.visible)continue;const auto patch=m.patches.find(item.id);const auto index=item.placement.anchor.node;
                    if(patch==m.patches.end()||!first||end<first||end-first>4096||index>=unsigned(end-first)||!first[index])continue;auto* node=first[index];auto* common=Read<void*>(node,0x11c);
                    const auto groupCount=common?Read<unsigned>(common,0x194):0;const auto* groups=common?Read<std::uint8_t*>(common,0x198):nullptr;
                    float alpha=1;unsigned materialID=UINT32_MAX;const auto geometryNode=std::find_if(m.nodes.begin(),m.nodes.end(),[&](const auto& n){return n.surface.index==index;});
                    if(geometryNode!=m.nodes.end()&&!patch->second.vertices.empty()&&groups){const auto triangle=patch->second.vertices.front().triangle;
                        if(triangle<geometryNode->surface.triangleGroups.size()){const auto group=geometryNode->surface.triangleGroups[triangle];if(group<groupCount){materialID=Read<unsigned>(groups,group*12+8);if(auto* mat=material(entry.first,materialID,0,1))alpha=Read<float>(mat,0x10);}}}
                    g_api->Log(g_mod,SDGO_LOG_INFO,"surface undrawn uid=%llu node=%u hidden=%u flags=%08X ready=%u material=%u alpha=%.4f age=%llu",uid,index,Read<std::uint8_t>(node,0x108),Read<unsigned>(node,0x204),BattleSurfaceReady(entry.first,m,item,patch->second)?1u:0u,materialID,alpha,now-m.seenAt);break;}
            }}
        for(const auto& patch:m.patches)value.triangles+=unsigned(patch.second.vertices.size()/3);
        if(design&&layer::Same(m.projected,*design)&&GetTickCount64()-m.seenAt<1500){
            std::set<std::uint64_t> groups;for(const auto& node:m.nodes)if(node.surface.fixedGroup){++value.fixedNodes;groups.insert(node.surface.fixedGroup);}value.fixedGroups+=unsigned(groups.size());
            for(const auto& p:m.fixedPatches){const auto* item=layer::Find(*design,p.first.first);if(!item||!item->visible||!item->placed)continue;
                ++value.fixedParts;value.triangles+=unsigned(p.second.patch.vertices.size()/3);const auto draws=m.fixedDraws.find(p.first);if(draws!=m.fixedDraws.end())value.fixedPartDraws+=draws->second;}
        }
        if(design)for(std::size_t i=0;i<design->items.size();++i){const auto& item=design->items[i];const auto draw=m.layerDraws.find(item.id);if(draw!=m.layerDraws.end()){value.layerDraws[i]+=draw->second;if(m.room)value.roomLayerDraws[i]+=draw->second;if(m.battle)value.battleLayerDraws[i]+=draw->second;}
            if(!item.visible||!item.placed||!item.style.wrap)continue;const auto patch=m.patches.find(item.id);if(patch==m.patches.end())continue;
            const auto node=std::find_if(m.nodes.begin(),m.nodes.end(),[&](const auto& n){return n.surface.index==item.placement.anchor.node;});if(node==m.nodes.end())continue;
            const auto& mesh=node->surface.mesh;
            const auto normal=[&](unsigned triangle){Vec3 out;if(triangle>=mesh.triangles.size())return out;const auto ids=mesh.triangles[triangle].vertex;
                const auto a=mesh.vertices[ids[0]],b=mesh.vertices[ids[1]],c=mesh.vertices[ids[2]];
                out={(b.y-a.y)*(c.z-a.z)-(b.z-a.z)*(c.y-a.y),(b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z),(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)};
                const auto length=std::hypot(out.x,out.y,out.z);return length>1e-12?Vec3{out.x/length,out.y/length,out.z/length}:Vec3{};};
            const auto anchor=normal(item.placement.anchor.triangle);std::set<unsigned> counted;
            for(const auto& vertex:patch->second.vertices)if(counted.insert(vertex.triangle).second){const auto face=normal(vertex.triangle);if(anchor.x*face.x+anchor.y*face.y+anchor.z*face.z<.25)++value.foldedTriangles;}
        }}
    if(auto* stockPage=Service(inventoryTable))if(Read<unsigned>(stockPage,0x178)==5&&Read<std::uint16_t>(stockPage,0x355)==0x09f2){
        value.nativeStockKnown=1;value.nativeStockPage=Read<unsigned>(stockPage,0x180);value.nativeStockPending=Read<unsigned>(stockPage,0x184);value.nativeStockTotal=Read<unsigned>(stockPage,0x188);Copy(static_cast<const std::uint8_t*>(stockPage)+0x355,value.nativeStock.data(),value.nativeStock.size());}
    value.lastCommit=lastCommit;*out=value;return 1;
}
extern "C" __declspec(dllexport) int __cdecl SdgoSurfaceDecalTestDumpBindingsV1(std::uint64_t uid,const wchar_t* path){
    if(!NativeTest()||!path)return 0;
    wchar_t executable[MAX_PATH]{};if(!GetModuleFileNameW(nullptr,executable,MAX_PATH))return 0;
    auto* slash=std::wcsrchr(executable,L'\\');if(!slash)return 0;*slash=0;
    const auto prefix=std::wstring(executable)+L"\\sdgo\\gametest\\";
    if(_wcsnicmp(path,prefix.c_str(),prefix.size())||std::wcsstr(path,L"..")||std::wcsstr(path,L"/"))return 0;
    std::lock_guard<std::recursive_mutex> hold(gate);if(!uid)uid=pageRecord.unitSerial;
    FILE* file=nullptr;if(_wfopen_s(&file,path,L"wb")||!file)return 0;
    std::fprintf(file,"{\"schema\":1,\"unit_uid\":%llu,\"models\":[",static_cast<unsigned long long>(uid));
    unsigned count=0,total=0,meshVertices=0;
    const bool includeMesh=GetEnvironmentVariableW(L"SDGO_APPEARANCE_MESH_PROBE",nullptr,0)>0;
    for(const auto& model:models){const auto& m=model.second;if(m.uid!=uid||GetTickCount64()-m.seenAt>1500||count>=8)continue;
        std::vector<native::FixedBinding> bindings;if(geometry.ReadFixedBindings(model.first,bindings)!=native::ReadResult::ok||total+bindings.size()>4096)continue;
        total+=unsigned(bindings.size());std::fprintf(file,"%s{\"model\":%u,\"type\":%u,\"battle\":%u,\"room\":%u,\"nodes\":[",count++?",":"",unsigned(reinterpret_cast<std::uintptr_t>(model.first)),m.type,m.battle?1:0,m.room?1:0);
        bool comma=false;for(const auto& binding:bindings){
            const auto surface=std::find_if(m.nodes.begin(),m.nodes.end(),[&](const auto& n){return n.surface.index==binding.index;});
            std::fprintf(file,"%s{\"index\":%u,\"vtable_rva\":%u,\"common\":%u,\"parent\":%d,\"runtime_parent\":%d,\"track_begin\":%u,\"track_end\":%u,\"track_capacity\":%u,\"modifier\":%u,\"valid\":%u,\"group\":\"%016llX\",\"vertices\":%u,\"triangles\":%u,\"active_slots\":[",comma?",":"",binding.index,unsigned(binding.vtable-reinterpret_cast<std::uintptr_t>(g_api->host_base)),unsigned(binding.common),binding.parentIndex,binding.runtimeParentIndex,unsigned(binding.trackBegin),unsigned(binding.trackEnd),unsigned(binding.trackCapacity),unsigned(binding.dynamicModifier),binding.valid?1:0,static_cast<unsigned long long>(binding.group),surface==m.nodes.end()?0:unsigned(surface->surface.mesh.vertices.size()),surface==m.nodes.end()?0:unsigned(surface->surface.mesh.triangles.size()));
            bool slotComma=false;for(auto slot:binding.nonNullTrackSlots){std::fprintf(file,"%s%u",slotComma?",":"",slot);slotComma=true;}
            std::fprintf(file,"],\"constant_slots\":[");slotComma=false;for(auto slot:binding.constantTrackSlots){std::fprintf(file,"%s%u",slotComma?",":"",slot);slotComma=true;}
            std::fprintf(file,"],\"unknown_slots\":[");slotComma=false;for(auto slot:binding.unknownTrackSlots){std::fprintf(file,"%s%u",slotComma?",":"",slot);slotComma=true;}
            std::fprintf(file,"],\"track_diagnostics\":[");slotComma=false;
            const auto values=[&](const char* name,const float* data,unsigned size){bool finite=true;for(unsigned i=0;i<size;++i)finite=finite&&std::isfinite(data[i]);
                std::fprintf(file,",\"%s\":",name);if(!finite){std::fprintf(file,"null");return;}std::fprintf(file,"[");for(unsigned i=0;i<size;++i)std::fprintf(file,"%s%.9g",i?",":"",data[i]);std::fprintf(file,"]");};
            for(const auto& diagnostic:binding.trackDiagnostics){const auto& consumer=diagnostic.consumer;
                std::fprintf(file,"%s{\"slot\":%u,\"state\":\"%s\",\"reason\":\"%s\",\"wrapper\":%u,\"record\":%u,\"key_counts\":[%u,%u,%u],\"decoded_keys\":%u",slotComma?",":"",diagnostic.slot,native::TrackDiagnosticStateName(diagnostic.state),native::TrackDiagnosticReasonName(diagnostic.reason),unsigned(consumer.wrapper),unsigned(consumer.record),consumer.positionKeys.count,consumer.quaternionKeys.count,consumer.scaleKeys.count,consumer.decodedKeyCount);
                values("record_matrix",consumer.defaultMatrix.data(),16);values("record_scale",consumer.defaultScale.data(),3);values("record_quaternion",consumer.defaultQuaternion.data(),4);values("record_position",consumer.defaultPosition.data(),3);std::fprintf(file,"}");slotComma=true;
            }
            const auto floats=[&](const char* name,std::uintptr_t address,unsigned values){
                std::array<float,16> data{};bool readable=values<=data.size()&&Copy(reinterpret_cast<const void*>(address),data.data(),values*4);
                for(unsigned i=0;i<values&&readable;++i)readable=std::isfinite(data[i]);
                std::fprintf(file,",\"%s\":",name);if(!readable){std::fprintf(file,"null");return;}
                std::fprintf(file,"[");for(unsigned i=0;i<values;++i)std::fprintf(file,"%s%.9g",i?",":"",data[i]);std::fprintf(file,"]");
            };
            std::fprintf(file,"]");
            floats("common_matrix",binding.common+0x12c,16);floats("common_scale",binding.common+0x16c,3);floats("common_quaternion",binding.common+0x178,4);floats("common_position",binding.common+0x188,3);
            std::fprintf(file,",\"model_pose\":");if(surface==m.nodes.end()||!surface->surface.modelPoseValid)std::fprintf(file,"null");
            else{std::fprintf(file,"[");for(unsigned i=0;i<16;++i)std::fprintf(file,"%s%.9g",i?",":"",surface->surface.modelPose[i]);std::fprintf(file,"]");}
            if(includeMesh&&surface!=m.nodes.end()&&meshVertices+surface->surface.mesh.vertices.size()<=16384){
                meshVertices+=unsigned(surface->surface.mesh.vertices.size());const auto& mesh=surface->surface.mesh;
                std::fprintf(file,",\"mesh\":{\"vertices\":[");bool element=false;
                for(const auto& v:mesh.vertices){std::fprintf(file,"%s[%.17g,%.17g,%.17g]",element?",":"",v.x,v.y,v.z);element=true;}
                std::fprintf(file,"],\"triangles\":[");element=false;
                for(const auto& t:mesh.triangles){std::fprintf(file,"%s[%u,%u,%u]",element?",":"",t.vertex[0],t.vertex[1],t.vertex[2]);element=true;}
                std::fprintf(file,"]}");
            }
            std::fprintf(file,"}");comma=true;
        }std::fprintf(file,"]}");
    }
    std::fprintf(file,"]}\n");const bool ok=std::ferror(file)==0;std::fclose(file);return ok&&count?1:0;
}
extern "C" __declspec(dllexport) int __cdecl SdgoSurfaceDecalTestTargetV1(std::uint64_t uid,test::TargetSnapshot* out){
    if(!NativeTest()||!out||out->size!=sizeof(*out)||out->version!=1)return 0;
    std::lock_guard<std::recursive_mutex> hold(gate);test::TargetSnapshot value;
    const auto* a=Authority(uid);if(a)decalwire::Encode(layer::ToLegacy(*a),value.appearance);
    for(const auto& item:models)if(item.second.battle&&item.second.uid==uid){
        ++value.battleModels;value.draws+=item.second.draws;
        value.nodes+=static_cast<unsigned>(item.second.nodes.size());value.pendingGeometry+=item.second.geometryPending?1u:0u;
        value.matchingStickers+=a&&SameStickers(*a,item.second)?1u:0u;
        for(const auto& p:item.second.patches)value.triangles+=static_cast<unsigned>(p.second.vertices.size()/3);
    }
    *out=value;return 1;
}
extern "C" __declspec(dllexport) int __cdecl SdgoSurfaceDecalTestRoomV1(std::uint64_t uid,test::RoomSnapshot* out){
    if(!NativeTest()||!out||out->size!=sizeof(*out)||out->version!=1)return 0;
    std::lock_guard<std::recursive_mutex> hold(gate);test::RoomSnapshot value;
    const auto* a=Authority(uid);if(a)decalwire::Encode(layer::ToLegacy(*a),value.appearance);
    for(const auto& item:models)if(item.second.room&&item.second.uid==uid){
        ++value.models;value.draws+=item.second.draws;
        value.nodes+=static_cast<unsigned>(item.second.nodes.size());value.pendingGeometry+=item.second.geometryPending?1u:0u;
        value.matchingStickers+=a&&SameStickers(*a,item.second)?1u:0u;
        for(const auto& p:item.second.patches)value.triangles+=static_cast<unsigned>(p.second.vertices.size()/3);
    }
    *out=value;return 1;
}
int LoadDecals(const SdgoApi* api, SdgoMod* mod) {
    if(!api||!mod||!api->Symbol||!api->HookCode||!api->SettingInt||!api->host_key||std::strcmp(api->host_key,"GOnline-54BE1568-00C97000")||!api->SettingInt(mod,"DecalsEnabled",1))return 0;
    g_api=api;g_mod=mod;
    wchar_t run[64]{};observing=GetEnvironmentVariableW(L"SDGO_GAME_TEST_RUN",run,64)>0;
    if(!geometry.Bind(api,mod)||!page.Bind(api,mod)||!textures.Bind(api,mod))return 0;
    renderer=static_cast<void**>(api->Symbol(mod,"surface_decal.renderer"));services=static_cast<void**>(api->Symbol(mod,"flow.service_managed_pointer"));
    stickerTable=api->Symbol(mod,"surface_decal.sticker_vtable");factoryTable=api->Symbol(mod,"surface_decal.factory_vtable");
    app=reinterpret_cast<App>(api->Symbol(mod,"game.app"));material=reinterpret_cast<Material>(api->Symbol(mod,"surface_decal.material"));
    inventoryTable=api->Symbol(mod,"rgb_paint.inventory_vtable");inventoryPage=reinterpret_cast<InventoryPage>(api->Symbol(mod,"surface_decal.inventory_page"));
    inventoryTick=reinterpret_cast<InventoryTick>(api->Symbol(mod,"surface_decal.inventory_page_tick"));inventoryAction=reinterpret_cast<InventoryAction>(api->Symbol(mod,"surface_decal.inventory_action"));
    const auto roomSymbol=[&](const char* name){return reinterpret_cast<std::uint32_t>(api->Symbol(mod,name));};
    roomBindings={roomSymbol("surface_decal.room_service"),roomSymbol("surface_decal.room_update"),roomSymbol("surface_decal.room_info"),
        roomSymbol("surface_decal.room_normal_owner"),roomSymbol("surface_decal.room_mission_owner"),
        roomSymbol("surface_decal.room_normal_unit"),roomSymbol("surface_decal.room_mission_unit"),
        roomSymbol("surface_decal.room_object"),roomSymbol("surface_decal.room_data")};
    if(!roomBindings.Valid())return 0;
    session=reinterpret_cast<Session>(api->Symbol(mod,"storage.session"));
    previewLock=reinterpret_cast<Simple>(api->Symbol(mod,"surface_decal.preview_lock"));
    previewUnlock=reinterpret_cast<Simple>(api->Symbol(mod,"surface_decal.preview_unlock"));
    previewZoomEnd=api->Symbol(mod,"surface_decal.preview_zoom_end");
    if(!renderer||!services||!stickerTable||!factoryTable||!app||!material||!session||!previewLock||!previewUnlock||!previewZoomEnd||!inventoryTable||!inventoryPage||!inventoryTick||!inventoryAction)return 0;
    if(api->size<offsetof(SdgoApi,process_initial_thread_id)+sizeof(DWORD)||!api->process_initial_thread_id||!api->HookSite)return 0;
    const bool hooks=api->HookCode(mod,"surface_decal.unit_style",reinterpret_cast<void*>(&StyleHook),reinterpret_cast<void**>(&styleOriginal))&&
        api->HookCode(mod,"surface_decal.model_destroy",reinterpret_cast<void*>(&ModelDestroyHook),reinterpret_cast<void**>(&modelDestroyOriginal))&&
        api->HookCode(mod,"surface_decal.render_callback",reinterpret_cast<void*>(&CallbackHook),reinterpret_cast<void**>(&callbackOriginal))&&
        api->HookCode(mod,"surface_decal.draw_indexed",reinterpret_cast<void*>(&IndexedHook),reinterpret_cast<void**>(&indexedOriginal))&&
        api->HookCode(mod,"surface_decal.sticker_update",reinterpret_cast<void*>(&UpdateHook),reinterpret_cast<void**>(&pageUpdateOriginal))&&
        api->HookCode(mod,"surface_decal.sticker_event",reinterpret_cast<void*>(&EventHook),reinterpret_cast<void**>(&eventOriginal))&&
        api->HookCode(mod,"surface_decal.sticker_destroy",reinterpret_cast<void*>(&PageDestroyHook),reinterpret_cast<void**>(&pageDestroyOriginal))&&
        api->HookSite(mod,"surface_decal.human_identity",reinterpret_cast<void*>(&HumanSite),&humanSiteOriginal)&&
        api->HookSite(mod,"surface_decal.battle_identity",reinterpret_cast<void*>(&BattleStyleSite),&battleStyleOriginal)&&
        api->HookSite(mod,"surface_decal.preview_input",reinterpret_cast<void*>(&PreviewInputSite),&previewInputOriginal)&&
        api->HookSite(mod,"surface_decal.preview_zoom",reinterpret_cast<void*>(&PreviewZoomSite),&previewZoomOriginal);
    if(!hooks)return 0;
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&GameThreadMessage),&module))return 0;
    messageHook=SetWindowsHookExW(WH_GETMESSAGE,&GameThreadMessage,module,api->process_initial_thread_id);
    return messageHook!=nullptr;
}

bool SurfaceDecalInputBlocked(){return inventoryBlocked.load();}
void AbortDecalsLoad(){if(messageHook){UnhookWindowsHookEx(messageHook);messageHook=nullptr;}if(timer){KillTimer(nullptr,timer);timer=0;}if(inputWindow){RemoveWindowSubclass(inputWindow,&InputProc,inputSubclass);inputWindow=nullptr;}inventoryBlocked=false;}
