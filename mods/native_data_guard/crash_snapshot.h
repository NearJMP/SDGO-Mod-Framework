#pragma once
#include <windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sdgo::native_crash {
inline bool Copy(const void* source,void* destination,std::size_t bytes) noexcept {
    const auto start=reinterpret_cast<std::uintptr_t>(source);
    if(!source||!destination||!bytes||bytes>512||start+bytes<start)return false;
    auto cursor=start;const auto end=start+bytes;
    for(unsigned regions=0;cursor<end&&regions<3;++regions){
        MEMORY_BASIC_INFORMATION region{};
        if(!VirtualQuery(reinterpret_cast<const void*>(cursor),&region,sizeof region)||
           region.State!=MEM_COMMIT||(region.Protect&(PAGE_GUARD|PAGE_NOACCESS))||
           !(region.Protect&(PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|
                            PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
        const auto next=reinterpret_cast<std::uintptr_t>(region.BaseAddress)+region.RegionSize;
        if(next<=cursor)return false;cursor=next;
    }
    if(cursor<end)return false;
    __try{std::memcpy(destination,source,bytes);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct ModelSnapshot {
    std::uint32_t vtable=0,resource=0,begin=0,end=0,aux12c=0;
    std::uint32_t entries=0,window_start=0,window_count=0;
    bool layout=false,header=false,bounds=false,window=false;
    std::array<std::uint32_t,5> nodes{};
};
inline ModelSnapshot Model(std::uintptr_t address,std::uintptr_t faultSlot,
                           std::uint32_t baseTable,std::uint32_t observedTable) noexcept {
    ModelSnapshot result;
    if(!Copy(reinterpret_cast<const void*>(address),&result.vtable,4)||
       (result.vtable!=baseTable&&result.vtable!=observedTable))return result;
    result.layout=true;
    std::array<std::uint32_t,0x130/4> words{};
    if(!Copy(reinterpret_cast<const void*>(address),words.data(),sizeof words))return result;
    result.header=true;result.resource=words[0x11c/4];result.begin=words[0x124/4];
    result.end=words[0x128/4];result.aux12c=words[0x12c/4];
    if(!result.begin||result.end<result.begin||(result.end-result.begin)%4||
       (result.end-result.begin)/4>4096)return result;
    result.bounds=true;result.entries=(result.end-result.begin)/4;
    if(!result.entries)return result;
    if(faultSlot>=result.begin&&faultSlot<result.end&&(faultSlot-result.begin)%4==0){
        const auto index=static_cast<std::uint32_t>((faultSlot-result.begin)/4);
        result.window_start=index>2?index-2:0;
    }
    const auto remaining=result.entries-result.window_start;
    result.window_count=remaining<result.nodes.size()?remaining:static_cast<std::uint32_t>(result.nodes.size());
    result.window=Copy(reinterpret_cast<const void*>(std::uintptr_t(result.begin)+result.window_start*4),
                       result.nodes.data(),result.window_count*4);
    if(!result.window)result.nodes={};
    return result;
}
} // namespace sdgo::native_crash
