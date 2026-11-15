#include "layers_server.h"
#include "sdgo/layered_decal_wire.h"
#include "sdgo/surface_decal_sql.h"
#include "../../oc_custom_server/oc_session.h"
#include <objbase.h>
#include <fstream>
#include <mutex>
#include <algorithm>

namespace {
namespace layer=sdgo::surface_decal::layers;
namespace wire=layer::wire;
namespace oldwire=sdgo::surface_decal::wire;
namespace sql=sdgo::surface_decal::sql;
using namespace sdgo::oc_custom::server;
using sdgo::friend_unit_gift::WireStatus;
using Handler=sdgo::friend_unit_gift::ExtensionHandlerFn;
Handler legacy=nullptr;
layer::Policy policy;
std::wstring schema;
std::mutex schemaMutex,quoteMutex;
bool ready=false;
struct Quote {
    std::uint32_t actor=0;std::uint64_t uid=0,at=0;
    bool existing=false;
    layer::Design saved,proposal;layer::Policy policy;
    std::vector<std::uint8_t> preimage;
    std::vector<layer::Cost> costs;
};
std::map<wire::Token,Quote> quotes;
std::wstring N(std::uint64_t value){return std::to_wstring(value);}
std::wstring Hex(const std::vector<std::uint8_t>& bytes){return sql::Hex(bytes.data(),bytes.size());}
bool Unhex(const std::wstring& value,std::vector<std::uint8_t>& bytes,std::size_t limit){
    if(value.size()%2||value.size()/2>limit)return false;std::vector<std::uint8_t> b(value.size()/2);
    if(!sql::Unhex(value,b.data(),b.size()))return false;bytes=std::move(b);return true;
}
bool Initialize(Session& db){std::lock_guard<std::mutex> lock(schemaMutex);if(!ready)ready=db.Execute(schema);return ready;}
bool Query(Session& db,const std::wstring& text,TextRows& rows){ResultSets sets;if(!db.Query(L"SET NOCOUNT ON; "+text,&sets)||sets.size()!=1)return false;rows=std::move(sets[0]);return true;}
bool LoadLegacy(std::uint32_t actor,std::uint64_t uid,void* database,sdgo::surface_decal::Appearance& a){
    std::uint8_t request[12];oldwire::Put(request,uid,8);oldwire::Put(request+8,2,4);
    std::array<std::uint8_t,oldwire::appearanceBytes+4> response{};std::uint32_t size=0;
    const auto status=legacy(nullptr,actor,oldwire::get,request,sizeof request,nullptr,database,response.data(),static_cast<std::uint32_t>(response.size()),&size);
    return status==0&&size==response.size()&&oldwire::Get(response.data(),4)==0&&oldwire::Decode(response.data()+4,size-4,a);
}
bool Load(Session& db,std::uint32_t actor,std::uint64_t uid,void* database,layer::Design& out,bool& existing,std::vector<std::uint8_t>& preimage){
    sdgo::surface_decal::Appearance original;if(!LoadLegacy(actor,uid,database,original))return false;
    TextRows rows;if(!Query(db,L"SELECT Revision,CONVERT(varchar(2960),Payload,2) FROM dbo.sdgo_mod_surface_layers WHERE UserUnitNumber="+N(uid),rows)||rows.size()>1)return false;
    existing=!rows.empty();
    if(!existing){preimage.clear();return wire::Canonical(layer::FromLegacy(original),out);}
    if(rows[0].size()!=2||!Unhex(rows[0][1],preimage,wire::maxBytes)||!wire::Decode(preimage.data(),preimage.size(),out)||out.unitSerial!=uid||out.revision!=static_cast<std::uint64_t>(Int64Or(rows[0],0,-1)))return false;
    if(original.revision<out.revision)return false;
    if(original.revision>out.revision||out.unitType!=original.unitType){
        // A normal native item replacement or a legacy-only edit changed the
        // baseline. Keep independent layers; only synchronize linked slots.
        auto next=out;next.unitType=original.unitType;next.revision=original.revision;
        for(unsigned i=0;i<3;++i)next.equipped[i]=original.slots[i].sticker;
        next.items.erase(std::remove_if(next.items.begin(),next.items.end(),[](const auto& l){return l.source<3;}),next.items.end());
        const auto migrated=layer::FromLegacy(original);
        for(const auto& l:migrated.items)if(next.items.size()<layer::maximum)next.items.push_back(l);
        std::vector<std::uint8_t> payload;if(!wire::Canonical(next,next)||!wire::Encode(next,payload))return false;
        TextRows changed;
        const auto batch=L"UPDATE dbo.sdgo_mod_surface_layers SET Revision="+N(next.revision)+L",Payload="+Hex(payload)+L",UpdatedAt=SYSUTCDATETIME() WHERE UserUnitNumber="+N(uid)+L" AND Revision="+N(out.revision)+L" AND Payload="+Hex(preimage)+L" AND EXISTS(SELECT 1 FROM dbo.sdgo_mod_surface_decal WHERE UserUnitNumber="+N(uid)+L" AND Revision="+N(original.revision)+L"); SELECT @@ROWCOUNT;";
        if(!Query(db,batch,changed)||changed.size()!=1||Int64Or(changed[0],0)!=1)return false;out=std::move(next);preimage=std::move(payload);
    }
    for(unsigned i=0;i<3;++i)if(out.equipped[i]!=original.slots[i].sticker)return false;
    return true;
}
std::wstring Allowed(){
    return L"s.StickerType<>2 AND s.UserLevel<=p.UserLevelNumber AND (ISNULL(s.AllowUnitID,0)=0 OR s.AllowUnitID=u.UnitNumber) AND "
        L"(s.AllowGroupInfluence=1 OR s.AllowInfluence=0 OR s.AllowInfluence IN(u.Influence1,u.Influence2) OR EXISTS(SELECT 1 FROM dbo.tbl_InfluenceGroup g WHERE g.InfluenceGroupNumber=s.AllowGroupInfluence AND g.InfluenceNumber IN(u.Influence1,u.Influence2)))";
}
std::wstring StockQuery(std::uint32_t actor,std::uint64_t uid,std::uint32_t cursor=0){
    return L"SELECT TOP 64 s.StickerID,CAST(ISNULL(x.Amount,0)AS bigint),CASE WHEN z.StickerID IS NOT NULL OR EXISTS(SELECT 1 FROM dbo.tbl_UserUnits n WHERE n.AccountNumber="+N(actor)+L" AND s.StickerID IN(n.UnitDecalNumber_1,n.UnitDecalNumber_2,n.UnitDecalNumber_3)) THEN 1 ELSE 0 END "
        L"FROM dbo.tbl_StickerItem s JOIN dbo.tbl_Users p ON p.AccountNumber="+N(actor)+L" JOIN dbo.tbl_UserUnits v ON v.UserUnitNumber="+N(uid)+L" AND v.AccountNumber=p.AccountNumber JOIN dbo.tbl_Units u ON u.UnitNumber=v.UnitNumber "
        L"LEFT JOIN(SELECT StickerID,SUM(CAST(RemainCount AS bigint)) Amount FROM dbo.tbl_UserStickerItems WHERE AccountNumber="+N(actor)+L" AND RemainCount>0 GROUP BY StickerID)x ON x.StickerID=s.StickerID "
        L"LEFT JOIN dbo.sdgo_mod_sticker_unlocks z ON z.AccountNumber=p.AccountNumber AND z.StickerID=s.StickerID WHERE s.StickerID>"+N(cursor)+L" AND "+Allowed()+
        L" AND (ISNULL(x.Amount,0)>0 OR z.StickerID IS NOT NULL OR EXISTS(SELECT 1 FROM dbo.tbl_UserUnits n WHERE n.AccountNumber=p.AccountNumber AND s.StickerID IN(n.UnitDecalNumber_1,n.UnitDecalNumber_2,n.UnitDecalNumber_3))) ORDER BY s.StickerID;";
}
bool Inventory(Session& db,std::uint32_t actor,std::uint64_t uid,std::map<std::uint32_t,layer::Stock>& out){
    std::uint32_t cursor=0;for(unsigned page=0;page<32;++page){TextRows rows;if(!Query(db,StockQuery(actor,uid,cursor),rows))return false;
        for(const auto& r:rows){const auto sid=Int64Or(r,0,-1),count=Int64Or(r,1,-1);if(r.size()!=3||sid<=cursor||sid>INT32_MAX||count<0)return false;
            cursor=static_cast<std::uint32_t>(sid);out[cursor]={cursor,static_cast<std::uint32_t>(std::min<std::int64_t>(count,INT32_MAX)),Int64Or(r,2)!=0};}
        if(rows.size()<64)return true;
    }return false;
}
int Send(const wire::Response& r,std::uint8_t* out,std::uint32_t capacity,std::uint32_t* size){
    std::vector<std::uint8_t> b;if(!wire::EncodeResponse(r,b)||b.size()>capacity)return static_cast<int>(WireStatus::database_unavailable);
    std::memcpy(out,b.data(),b.size());*size=static_cast<std::uint32_t>(b.size());return 0;
}
bool Owner(Session& db,std::uint32_t actor,std::uint64_t uid){TextRows rows;return Query(db,L"SELECT AccountNumber FROM dbo.tbl_UserUnits WHERE UserUnitNumber="+N(uid),rows)&&rows.size()==1&&Int64Or(rows[0],0)==actor;}
std::wstring CommitBatch(const wire::Token& token,const Quote& quote,const std::vector<std::uint8_t>& payload,const std::vector<std::uint8_t>& reply){
    const auto actor=N(quote.actor),uid=N(quote.uid),tx=sql::Hex(token.data(),token.size());
    const auto legacyAppearance=layer::ToLegacy(quote.proposal);oldwire::Bytes old;if(!oldwire::Encode(legacyAppearance,old))return {};
    std::wstring s=L"SET NOCOUNT ON;SET XACT_ABORT ON;BEGIN TRY BEGIN TRANSACTION;DECLARE @lock int,@native int,@owner int;"
        L"EXEC @lock=sys.sp_getapplock @Resource=N'SDGO:surface-items:"+actor+L"',@LockMode='Exclusive',@LockOwner='Transaction',@LockTimeout=5000;"
        L"IF @lock<0 BEGIN ROLLBACK;SELECT 12;RETURN;END;"
        L"IF EXISTS(SELECT 1 FROM dbo.sdgo_mod_surface_layer_receipts WITH(UPDLOCK,HOLDLOCK) WHERE Token="+tx+L") BEGIN SELECT 0,CONVERT(varchar(3796),Reply,2) FROM dbo.sdgo_mod_surface_layer_receipts WHERE Token="+tx+L" AND AccountNumber="+actor+L" AND UserUnitNumber="+uid+L";COMMIT;RETURN;END;"
        // Match native equipment operations: acquire the account's native
        // inventory lock before locking an owned-unit row.
        L"EXEC dbo.sp_LockUserUnits "+actor+L",@native OUTPUT;IF @native<>0 BEGIN ROLLBACK;SELECT 12;RETURN;END;"
        L"SELECT @owner=AccountNumber FROM dbo.tbl_UserUnits WITH(UPDLOCK,HOLDLOCK) WHERE UserUnitNumber="+uid+L";IF @owner IS NULL OR @owner<>"+actor+L" BEGIN ROLLBACK;SELECT 9;RETURN;END;"
        L"IF NOT EXISTS(SELECT 1 FROM dbo.sdgo_mod_surface_decal WITH(UPDLOCK,HOLDLOCK) WHERE UserUnitNumber="+uid+L" AND Revision="+N(quote.saved.revision)+L") BEGIN ROLLBACK;SELECT 12;RETURN;END;";
    s+=L"IF NOT EXISTS(SELECT 1 FROM dbo.tbl_UserUnits WHERE UserUnitNumber="+uid+L" AND AccountNumber="+actor+L" AND UnitNumber="+N(quote.saved.unitType);
    for(unsigned i=0;i<3;++i)s+=L" AND ISNULL(UnitDecalNumber_"+N(i+1)+L",0)="+N(quote.saved.equipped[i]);
    s+=L") BEGIN ROLLBACK;SELECT 12;RETURN;END;";
    if(quote.existing)s+=L"IF NOT EXISTS(SELECT 1 FROM dbo.sdgo_mod_surface_layers WITH(UPDLOCK,HOLDLOCK) WHERE UserUnitNumber="+uid+L" AND Revision="+N(quote.saved.revision)+L" AND Payload="+Hex(quote.preimage)+L") BEGIN ROLLBACK;SELECT 12;RETURN;END;";
    else s+=L"IF EXISTS(SELECT 1 FROM dbo.sdgo_mod_surface_layers WITH(UPDLOCK,HOLDLOCK) WHERE UserUnitNumber="+uid+L") BEGIN ROLLBACK;SELECT 12;RETURN;END;";
    for(const auto& cost:quote.costs){
        const auto sid=N(cost.sticker);s+=L"IF NOT EXISTS(SELECT 1 FROM dbo.tbl_StickerItem s JOIN dbo.tbl_Users p ON p.AccountNumber="+actor+L" JOIN dbo.tbl_UserUnits v ON v.UserUnitNumber="+uid+L" JOIN dbo.tbl_Units u ON u.UnitNumber=v.UnitNumber WHERE s.StickerID="+sid+L" AND "+Allowed()+L") BEGIN ROLLBACK;SELECT 9;RETURN;END;";
        if(quote.policy.consume){s+=L"DECLARE @need"+sid+L" int="+N(cost.required)+L",@stack"+sid+L" bigint,@amount"+sid+L" int,@take"+sid+L" int;"
            L"WHILE @need"+sid+L">0 BEGIN SET @stack"+sid+L"=NULL;SELECT TOP 1 @stack"+sid+L"=UserStickerItemNumber,@amount"+sid+L"=RemainCount FROM dbo.tbl_UserStickerItems WITH(UPDLOCK,HOLDLOCK) WHERE AccountNumber="+actor+L" AND StickerID="+sid+L" AND RemainCount>0 ORDER BY UserStickerItemNumber;"
            L"IF @stack"+sid+L" IS NULL BEGIN ROLLBACK;SELECT 18;RETURN;END;SET @take"+sid+L"=CASE WHEN @amount"+sid+L"<@need"+sid+L" THEN @amount"+sid+L" ELSE @need"+sid+L" END;"
            L"UPDATE dbo.tbl_UserStickerItems SET RemainCount=RemainCount-@take"+sid+L" WHERE UserStickerItemNumber=@stack"+sid+L" AND AccountNumber="+actor+L";"
            L"DELETE dbo.tbl_UserStickerItems WHERE UserStickerItemNumber=@stack"+sid+L" AND AccountNumber="+actor+L" AND RemainCount=0;SET @need"+sid+L"=@need"+sid+L"-@take"+sid+L";END;";
        }else s+=L"IF NOT EXISTS(SELECT 1 FROM dbo.tbl_UserStickerItems WITH(UPDLOCK,HOLDLOCK) WHERE AccountNumber="+actor+L" AND StickerID="+sid+L" AND RemainCount>0) AND NOT EXISTS(SELECT 1 FROM dbo.sdgo_mod_sticker_unlocks WHERE AccountNumber="+actor+L" AND StickerID="+sid+L") AND NOT EXISTS(SELECT 1 FROM dbo.tbl_UserUnits WHERE AccountNumber="+actor+L" AND "+sid+L" IN(UnitDecalNumber_1,UnitDecalNumber_2,UnitDecalNumber_3)) BEGIN ROLLBACK;SELECT 18;RETURN;END;";
        s+=L"IF NOT EXISTS(SELECT 1 FROM dbo.sdgo_mod_sticker_unlocks WITH(UPDLOCK,HOLDLOCK) WHERE AccountNumber="+actor+L" AND StickerID="+sid+L") INSERT dbo.sdgo_mod_sticker_unlocks(AccountNumber,StickerID) VALUES("+actor+L","+sid+L");";
    }
    if(quote.existing)s+=L"UPDATE dbo.sdgo_mod_surface_layers SET Revision="+N(quote.proposal.revision)+L",Payload="+Hex(payload)+L",UpdatedAt=SYSUTCDATETIME() WHERE UserUnitNumber="+uid+L";";
    else s+=L"INSERT dbo.sdgo_mod_surface_layers(UserUnitNumber,Revision,Payload) VALUES("+uid+L","+N(quote.proposal.revision)+L","+Hex(payload)+L");";
    s+=L"UPDATE dbo.sdgo_mod_surface_decal SET Revision="+N(quote.proposal.revision);
    for(unsigned i=0;i<3;++i)s+=L",Placement"+N(i+1)+L"="+sql::Hex(old.data()+oldwire::headerBytes+i*oldwire::slotBytes,oldwire::slotBytes);
    s+=L",StyleData="+sql::Hex(old.data()+oldwire::legacyBytes,oldwire::styleBytes)+L" WHERE UserUnitNumber="+uid+L";"
        L"INSERT dbo.sdgo_mod_surface_layer_receipts(Token,AccountNumber,UserUnitNumber,Reply) VALUES("+tx+L","+actor+L","+uid+L","+Hex(reply)+L");"
        L"EXEC dbo.sp_UnlockUserUnits "+actor+L";COMMIT;SELECT 0,CONVERT(varchar(3796),"+Hex(reply)+L",2);END TRY BEGIN CATCH IF @@TRANCOUNT>0 ROLLBACK;THROW;END CATCH;";
    return s;
}
int __cdecl Handle(void* context,std::uint32_t actor,std::uint16_t command,const std::uint8_t* data,std::uint32_t size,void* members,void* database,std::uint8_t* output,std::uint32_t capacity,std::uint32_t* produced){
    if(produced)*produced=0;
    if(command==oldwire::get||command==oldwire::save)return legacy(context,actor,command,data,size,members,database,output,capacity,produced);
    if(!actor||actor>INT32_MAX||!data||!output||!produced)return static_cast<int>(WireStatus::invalid_argument);
    try{
        Session db(5,true);if(!db.Open(database)||!Initialize(db))return static_cast<int>(WireStatus::database_unavailable);
        layer::Design proposal;
        if(command==wire::quote&&!wire::Decode(data,size,proposal))return static_cast<int>(WireStatus::invalid_argument);
        const auto uid=command==wire::quote?proposal.unitSerial:size>=8?wire::Get(data,8):0;
        if(!uid||uid>INT64_MAX)return static_cast<int>(WireStatus::invalid_argument);
        if(command==wire::catalogue){
            if(size!=12||!Owner(db,actor,uid))return static_cast<int>(WireStatus::not_authorized);
            TextRows rows;if(!Query(db,StockQuery(actor,uid,static_cast<std::uint32_t>(wire::Get(data+8,4))),rows)||rows.size()>64||capacity<20+rows.size()*12)return static_cast<int>(WireStatus::database_unavailable);
            std::memset(output,0,20);wire::Put(output+4,policy.consume?1:0,4);wire::Put(output+8,policy.maxLayers,4);wire::Put(output+12,rows.size(),4);
            for(std::size_t i=0;i<rows.size();++i){auto* p=output+20+i*12;wire::Put(p,Int64Or(rows[i],0),4);wire::Put(p+4,std::min<std::int64_t>(INT32_MAX,Int64Or(rows[i],1)),4);wire::Put(p+8,Int64Or(rows[i],2)!=0?1:0,4);}
            if(rows.size()==64)wire::Put(output+16,Int64Or(rows.back(),0),4);*produced=static_cast<std::uint32_t>(20+rows.size()*12);return 0;
        }
        wire::Token token{};
        if(command==wire::commit){
            if(size!=24||!Owner(db,actor,uid))return static_cast<int>(WireStatus::not_authorized);std::copy(data+8,data+24,token.begin());
            TextRows receipt;if(!Query(db,L"SELECT CONVERT(varchar(3796),Reply,2) FROM dbo.sdgo_mod_surface_layer_receipts WHERE Token="+sql::Hex(token.data(),16)+L" AND AccountNumber="+N(actor)+L" AND UserUnitNumber="+N(uid),receipt))return static_cast<int>(WireStatus::database_unavailable);
            if(!receipt.empty()){std::vector<std::uint8_t> bytes;if(receipt.size()!=1||!Unhex(receipt[0][0],bytes,capacity))return static_cast<int>(WireStatus::database_unavailable);std::memcpy(output,bytes.data(),bytes.size());*produced=static_cast<std::uint32_t>(bytes.size());return 0;}
        }
        wire::Response response;response.policy=policy;bool existing=false;std::vector<std::uint8_t> preimage;
        if(!Load(db,actor,uid,database,response.authority,existing,preimage))return static_cast<int>(WireStatus::unit_unavailable);
        if(command==wire::get){if(size!=8)return static_cast<int>(WireStatus::invalid_argument);return Send(response,output,capacity,produced);}
        if(command==wire::quote){
            if(!Owner(db,actor,uid))return static_cast<int>(WireStatus::not_authorized);
            if(proposal.revision!=response.authority.revision||proposal.unitType!=response.authority.unitType||proposal.equipped!=response.authority.equipped||proposal.revision>=INT64_MAX){response.result=wire::Result::conflict;return Send(response,output,capacity,produced);}
            if(proposal.items.size()>policy.maxLayers&&proposal.items.size()>response.authority.items.size()){response.result=wire::Result::limit;return Send(response,output,capacity,produced);}
            std::map<std::uint32_t,layer::Stock> stock;if(!Inventory(db,actor,uid,stock))return static_cast<int>(WireStatus::database_unavailable);
            for(const auto& need:layer::RequiredItems(response.authority,proposal)){const auto found=stock.find(need.first);const auto have=found==stock.end()?0:found->second.remaining;
                response.costs.push_back({need.first,policy.consume?need.second:0,have});
                if(found==stock.end()||(policy.consume&&have<need.second)||(!policy.consume&&!have&&!found->second.unlocked))response.result=wire::Result::insufficient;
            }
            if(response.result!=wire::Result::ok)return Send(response,output,capacity,produced);
            GUID guid{};if(FAILED(CoCreateGuid(&guid)))return static_cast<int>(WireStatus::database_unavailable);std::memcpy(response.token.data(),&guid,16);
            Quote q;q.actor=actor;q.uid=uid;q.at=GetTickCount64();q.saved=response.authority;q.proposal=proposal;q.policy=policy;q.existing=existing;q.preimage=preimage;q.costs=response.costs;
            // Keep required quantities even in reusable mode for the final
            // ownership check; only the advertised charge becomes zero.
            const auto needed=layer::RequiredItems(q.saved,q.proposal);for(auto& c:q.costs)c.required=needed.at(c.sticker);
            std::lock_guard<std::mutex> lock(quoteMutex);for(auto i=quotes.begin();i!=quotes.end();)if(q.at-i->second.at>60000)i=quotes.erase(i);else ++i;
            if(quotes.size()>=256)quotes.erase(quotes.begin());quotes[response.token]=std::move(q);return Send(response,output,capacity,produced);
        }
        if(command==wire::commit){
            Quote q;{std::lock_guard<std::mutex> lock(quoteMutex);const auto i=quotes.find(token);if(i==quotes.end()||i->second.actor!=actor||i->second.uid!=uid||GetTickCount64()-i->second.at>60000){response.result=wire::Result::expired;return Send(response,output,capacity,produced);}q=i->second;}
            if(q.saved.revision!=response.authority.revision||!layer::Same(q.saved,response.authority)){response.result=wire::Result::conflict;return Send(response,output,capacity,produced);}
            ++q.proposal.revision;response.authority=q.proposal;response.token=token;response.costs=q.costs;if(!policy.consume)for(auto& c:response.costs)c.required=0;
            std::vector<std::uint8_t> payload,reply;if(!wire::Encode(q.proposal,payload)||!wire::EncodeResponse(response,reply))return static_cast<int>(WireStatus::invalid_argument);
            TextRows done;const auto batch=CommitBatch(token,q,payload,reply);if(batch.empty()||!Query(db,batch,done)||done.size()!=1)return static_cast<int>(WireStatus::database_unavailable);
            if(done[0].size()==2&&Int64Or(done[0],0,-1)==0){std::vector<std::uint8_t> bytes;if(!Unhex(done[0][1],bytes,capacity))return static_cast<int>(WireStatus::database_unavailable);std::memcpy(output,bytes.data(),bytes.size());*produced=static_cast<std::uint32_t>(bytes.size());return 0;}
            const auto result=Int64Or(done[0],0,12);response.result=result==18?wire::Result::insufficient:result==9?wire::Result::denied:wire::Result::conflict;
            if(!Load(db,actor,uid,database,response.authority,existing,preimage))return static_cast<int>(WireStatus::database_unavailable);return Send(response,output,capacity,produced);
        }
        return static_cast<int>(WireStatus::invalid_argument);
    }catch(...){return static_cast<int>(WireStatus::database_unavailable);}
}
}
Handler PrepareLayeredDecals(const SdgoApi* api,SdgoMod* mod,Handler old){
    if(!api||!mod||!old||!api->SettingInt)return nullptr;legacy=old;
    policy.consume=api->SettingInt(mod,"ConsumeStickers",1)!=0;const auto maximum=api->SettingInt(mod,"MaxLayers",20);if(maximum<1||maximum>20)return nullptr;policy.maxLayers=static_cast<unsigned>(maximum);
    std::ifstream file(std::wstring(api->ModDirectory(mod))+L"\\layers_schema.sql",std::ios::binary);const std::string bytes((std::istreambuf_iterator<char>(file)),{});
    if(bytes.empty()||bytes.size()>65536)return nullptr;for(unsigned char c:bytes){if(c>127)return nullptr;schema+=wchar_t(c);}return &Handle;
}
