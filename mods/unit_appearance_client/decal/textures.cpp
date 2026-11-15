#include "textures.h"
#include <algorithm>
#include <cstring>

namespace sdgo::surface_decal::native {
namespace {
bool Copy(const void* p,void* output,std::size_t size)noexcept{
    __try{if(!p)return false;std::memcpy(output,p,size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
std::wstring Text(const unsigned char* row,unsigned offset,unsigned limit){
    std::wstring out;out.reserve(limit);
    for(unsigned i=0;i<limit;++i){wchar_t ch=0;if(!Copy(row+offset+2*i,&ch,2))return {};
        if(!ch)return out;if(ch<32||ch==127||ch>=0xd800&&ch<=0xdfff)return {};out+=ch;}
    return {};
}
}
bool Textures::Bind(const SdgoApi* api,SdgoMod* mod){
    if(!api||!api->Symbol)return false;
    grid_=reinterpret_cast<Grid>(api->Symbol(mod,"grid.data"));find_=reinterpret_cast<Find>(api->Symbol(mod,"grid.find"));
    allocate_=reinterpret_cast<Allocate>(api->Symbol(mod,"crt.operator_new"));
    construct_=reinterpret_cast<Construct>(api->Symbol(mod,"surface_decal.texture_construct"));
    load_=reinterpret_cast<Load>(api->Symbol(mod,"surface_decal.texture_load"));
    delete_=reinterpret_cast<Delete>(api->Symbol(mod,"surface_decal.texture_delete"));
    vtable_=api->Symbol(mod,"surface_decal.texture_vtable");
    return grid_&&find_&&allocate_&&construct_&&load_&&delete_&&vtable_;
}
const unsigned char* Textures::Record(std::uint32_t sticker)const{
    if(!grid_||!find_||!sticker||sticker>INT32_MAX)return nullptr;
    const unsigned char* record=nullptr;return find_(grid_(),&record,sticker,38,0)?record:nullptr;
}
std::wstring Textures::Name(std::uint32_t sticker)const{const auto* row=Record(sticker);return row?Text(row,5,50):std::wstring{};}
IDirect3DBaseTexture9* Textures::Get(std::uint32_t sticker){
    const auto cached=entries_.find(sticker);if(cached!=entries_.end()){cached->second.age=++sequence_;return cached->second.texture;}
    const auto now=GetTickCount64();const auto prior=failures_.find(sticker);if(prior!=failures_.end()&&now-prior->second<5000)return nullptr;
    if(failures_.size()>=256)failures_.erase(failures_.begin());failures_[sticker]=now;
    const auto* row=Record(sticker);if(!row)return nullptr;
    const auto path=Text(row,0xd1,128);
    if(path.empty()||path.find(L':')!=std::wstring::npos||path.find(L"..")!=std::wstring::npos||path.front()==L'/'||path.front()==L'\\')return nullptr;
    auto* memory=allocate_(0x54);if(!memory)return nullptr;auto* object=construct_(memory);
    void* table=nullptr;if(!Copy(object,&table,4)||table!=vtable_){if(object)delete_(object,1);return nullptr;}
    sdgo::hangar::ui::NativeString name;name.length=static_cast<unsigned>(path.size());
    if(name.length<8)std::memcpy(name.data.small,path.c_str(),(name.length+1)*2);else{name.capacity=name.length;name.data.pointer=path.c_str();}
    if(!load_(object,&name,0,D3DFMT_DXT3,1,-1,-1,0xffff00ff)){delete_(object,1);return nullptr;}
    IDirect3DTexture9* texture=nullptr;if(!Copy(static_cast<unsigned char*>(object)+0x14,&texture,4)||!texture){delete_(object,1);return nullptr;}
    D3DSURFACE_DESC desc{};
    if(texture->GetType()!=D3DRTYPE_TEXTURE||FAILED(texture->GetLevelDesc(0,&desc))||!desc.Width||!desc.Height||desc.Width>2048||desc.Height>2048){delete_(object,1);return nullptr;}
    const auto levels=texture->GetLevelCount();if(!levels||levels>12){delete_(object,1);return nullptr;}
    std::size_t size=0;for(unsigned level=0;level<levels;++level){D3DSURFACE_DESC mip{};if(FAILED(texture->GetLevelDesc(level,&mip))||!mip.Width||!mip.Height||mip.Width>2048||mip.Height>2048){delete_(object,1);return nullptr;}size+=std::size_t(mip.Width)*mip.Height*4;}
    while(!entries_.empty()&&(entries_.size()>=256||bytes_+size>64u*1024u*1024u)){
        const auto oldest=std::min_element(entries_.begin(),entries_.end(),[](const auto& a,const auto& b){return a.second.age<b.second.age;});
        bytes_-=oldest->second.bytes;delete_(oldest->second.object,1);entries_.erase(oldest);
    }
    entries_[sticker]={object,texture,++sequence_,size};failures_.erase(sticker);bytes_+=size;return texture;
}
void Textures::Clear(){for(auto& e:entries_)if(e.second.object&&delete_)delete_(e.second.object,1);entries_.clear();failures_.clear();bytes_=0;}
}
