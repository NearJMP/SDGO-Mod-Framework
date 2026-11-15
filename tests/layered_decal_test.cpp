#include "sdgo/layered_decal_wire.h"
#include <cstdlib>
#include <iostream>
namespace l=sdgo::surface_decal::layers;
namespace w=l::wire;
namespace gift=sdgo::friend_unit_gift;
using sdgo::surface_decal::Reply;
unsigned checks=0;
void Check(bool v,const char* why){++checks;if(!v){std::cerr<<why<<'\n';std::exit(1);}}
int main(){
    l::Design root;root.unitSerial=100;root.unitType=10002;root.revision=1;root.equipped={{10,11,12}};
    l::Editor edit;Check(edit.Open(root),"open empty independent design");
    sdgo::surface_decal::Placement p;p.anchor.geometry=7;p.anchor.barycentric={{.2,.3,.5}};
    p.anchor.tangent={1,0,0};p.width=p.height=2;p.depth=.6;
    std::uint32_t first=0;
    for(unsigned i=0;i<20;++i){std::uint32_t id=0;Check(edit.Add(101+i,id)&&edit.Place(id,p),"add and place twenty independent types");if(!i)first=id;}
    std::uint32_t extra=0;Check(!edit.Add(999,extra),"twenty-first layer refused");
    auto costs=l::RequiredItems(root,edit.Preview());Check(costs.size()==20&&costs[101]==1,"each new layer consumes once");
    Check(edit.Move(first,19)&&edit.StyleValue(first,{350,33,true}),"stable layer moves above other nineteen");
    Check(l::RequiredItems(root,edit.Preview())==costs,"movement and wear do not increase cost");
    l::Design canonical;Check(w::Canonical(edit.Preview(),canonical)&&edit.ReplaceCanonicalDraft(canonical),"native float values canonicalize");
    std::vector<std::uint8_t> bytes;Check(w::Encode(canonical,bytes)&&bytes.size()==1480,"actual full twenty-layer payload");
    l::Design decoded;Check(w::Decode(bytes.data(),bytes.size(),decoded)&&l::Same(canonical,decoded),"full design roundtrip");
    auto corrupt=bytes;corrupt[w::headerBytes+13]=1;Check(!w::Decode(corrupt.data(),corrupt.size(),decoded),"reserved bytes rejected");
    corrupt=bytes;corrupt[37]=21;Check(!w::Decode(corrupt.data(),corrupt.size(),decoded),"over-cap packet rejected");
    auto forged=canonical;forged.items[0].source=0;Check(!l::Valid(forged),"independent ID cannot impersonate an equipped slot");
    w::Response response;response.authority=canonical;response.token[0]=1;
    for(const auto& c:costs)response.costs.push_back({c.first,c.second,100});
    Check(w::EncodeResponse(response,bytes)&&bytes.size()==1768,"full design and twenty item costs fit real carrier");
    w::Response reply;Check(w::DecodeResponse(bytes.data(),bytes.size(),reply)&&l::Same(reply.authority,canonical)&&reply.costs.size()==20,"large quote roundtrip");
    for(unsigned n:{0u,1u,1024u,1480u,1768u,unsigned(gift::kMaximumExtensionPayloadBytes)}){
        gift::Request request;request.command=static_cast<gift::Command>(w::quote);request.request_id.bytes[0]=1;
        request.extension_bytes=static_cast<std::uint16_t>(n);for(unsigned i=0;i<n;++i)request.extension[i]=std::uint8_t(i*31);
        std::array<std::uint8_t,gift::kCarrierBytes> carrier{};Check(gift::EncodeRequest(request,&carrier),"authenticated native request capacity");
        gift::wire::Write32(carrier.data()+2,101);Check(gift::MarkAgentValidated(carrier.data(),carrier.size()),"agent authentication marker");
        gift::Request requestOut;std::uint32_t actor=0;Check(gift::DecodeRequest(carrier.data(),carrier.size(),&requestOut,&actor)&&actor==101&&requestOut.extension_bytes==n&&requestOut.extension==request.extension,"large request exact readback");
        gift::Reply answer;answer.command=request.command;answer.request_id=request.request_id;answer.extension_bytes=request.extension_bytes;answer.extension=request.extension;
        Check(gift::EncodeReply(actor,answer,&carrier),"fixed-size reply capacity");gift::Reply answerOut;
        Check(gift::DecodeReply(carrier.data(),carrier.size(),&answerOut)&&answerOut.extension==answer.extension,"large reply exact readback");
    }
    l::Design proposal;Check(edit.BeginSave(1,proposal),"freeze quoted design for commit");++proposal.revision;
    Check(edit.Acknowledge(1,Reply::saved,proposal),"ack exact server design");
    Check(edit.Move(first,0)&&edit.StyleValue(first,{700,33,true})&&l::RequiredItems(edit.Saved(),edit.Preview()).empty(),"subsequent transform-only saves are free");
    Check(edit.Duplicate(first,extra)==false,"duplicate at cap refused");
    Check(edit.Remove(edit.Preview().items[1].id)&&edit.Duplicate(first,extra),"delete then duplicate into available capacity");
    Check(l::RequiredItems(edit.Saved(),edit.Preview())==std::map<std::uint32_t,unsigned>{{101,1}},"duplicate creates a new paid identity");
    Check(edit.Cancel()&&l::Same(edit.Preview(),edit.Saved()),"cancel restores saved without any request");
    std::cout<<checks<<" checks passed\n";
}
