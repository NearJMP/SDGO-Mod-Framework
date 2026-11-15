#include "../mods/unit_appearance_client/rgb/sync.cpp"
#include <cstdlib>
#include <iostream>
namespace rgb=sdgo::rgb_paint;namespace sync=rgb::sync;
namespace {
std::array<std::uint8_t,32> fakeSession{};
int carrierState=2,sends=0,replies=0;bool accepted=false;
sdgo::friend_unit_gift::ClientExtensionCallbackFn completed=nullptr;void* cookie=nullptr;
rgb::Record sentRecord;
void* __cdecl NativeSession(){return fakeSession.data();}
int __cdecl Carrier(){return carrierState;}
int __cdecl Send(std::uint16_t command,const std::uint8_t* data,std::uint32_t bytes,sdgo::friend_unit_gift::ClientExtensionCallbackFn callback,void* context){
    if(command!=rgb::wire::save||carrierState!=1||!rgb::wire::Decode(data,bytes,sentRecord))return 0;
    ++sends;completed=callback;cookie=context;return 1;
}
void Saved(std::uint64_t,std::uint64_t,bool ok,const rgb::Record* record){++replies;accepted=ok&&record&&record->state.revision==5;}
void Check(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
}
int main(){
    int connection=7;std::memcpy(fakeSession.data()+0x14,&connection,4);
    sync::nativeSession=&NativeSession;sync::transport=&Carrier;sync::send=&Send;
    sync::sessionKey=(std::uint64_t(reinterpret_cast<std::uintptr_t>(fakeSession.data()))<<32)|7;
    rgb::Record proposal;proposal.type=100;proposal.state.identity={sync::sessionKey,700};proposal.state.revision=4;proposal.state.colors[0]=rgb::Rgb{17,34,51};
    Check(sync::SaveRecord(proposal,&Saved),"another component's outstanding read must not reject a confirmed RGB save");
    Check(!sync::SaveRecord(proposal,&Saved),"a queued save cannot be replaced by another draft");
    sync::Pump();Check(sends==0&&replies==0,"busy carrier preserves the queued draft without claiming a save");
    carrierState=1;sync::Pump();Check(sends==1&&sentRecord.state.colors==proposal.state.colors,"idle carrier sends the exact confirmed colors");
    Check(!sync::SaveRecord(proposal,&Saved),"in-flight save keeps one completion owner");
    auto authority=sentRecord;++authority.state.revision;rgb::wire::Bytes bytes;Check(rgb::wire::Encode(authority,bytes),"valid authoritative response");
    std::array<std::uint8_t,rgb::wire::bytes+4> reply{};std::copy(bytes.begin(),bytes.end(),reply.begin()+4);
    completed(cookie,rgb::wire::save,0,reply.data(),unsigned(reply.size()));Check(replies==1&&accepted,"the native completion path confirms the server revision exactly once");
    Check(sync::SaveRecord(proposal,&Saved),"a later confirmed save can queue");
    ++connection;std::memcpy(fakeSession.data()+0x14,&connection,4);sync::Pump();
    Check(!sync::queued&&sends==1,"session replacement discards an unsent previous-account draft");
    std::cout<<"RGB shared-carrier queue and authoritative completion passed\n";
}
