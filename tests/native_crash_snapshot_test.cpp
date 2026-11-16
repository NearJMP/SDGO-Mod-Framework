#include "../mods/native_data_guard/crash_snapshot.h"
#include <cassert>
#include <iostream>

int main(){
    using sdgo::native_crash::Model;
    std::array<std::uint32_t,0x130/4> model{};
    std::array<std::uint32_t,8> nodes={0x11111111,0x22222222,0x33333333,0x27a8,0x55555555,0x66666666,0x77777777,0x88888888};
    const auto pointer=[](const void* p){return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));};
    constexpr std::uint32_t table=0x12345678;
    model[0]=table;model[0x11c/4]=0xabcdef00;
    model[0x124/4]=pointer(nodes.data());model[0x128/4]=pointer(nodes.data()+nodes.size());
    const auto originalModel=model;
    const auto originalNodes=nodes;
    const auto snapshot=Model(pointer(model.data()),pointer(nodes.data()+3),table,table+4);
    assert(snapshot.layout&&snapshot.header&&snapshot.bounds&&snapshot.window);
    assert(snapshot.entries==8&&snapshot.window_start==1&&snapshot.window_count==5);
    assert(snapshot.nodes[2]==0x27a8&&snapshot.resource==0xabcdef00);
    assert(model==originalModel&&nodes==originalNodes);
    assert(!Model(0,0,table,table+4).layout);
    assert(!Model(pointer(model.data()),0,table+8,table+12).layout);
    model[0x128/4]=model[0x124/4]-4;
    assert(!Model(pointer(model.data()),0,table,table+4).bounds);
    model[0x128/4]=model[0x124/4]+7;
    assert(!Model(pointer(model.data()),0,table,table+4).bounds);
    model[0x128/4]=model[0x124/4]+4097*4;
    assert(!Model(pointer(model.data()),0,table,table+4).bounds);
    auto* page=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    assert(page);DWORD previous=0;assert(VirtualProtect(page+4096,4096,PAGE_NOACCESS,&previous));
    std::array<std::uint8_t,32> bytes{};
    assert(!sdgo::native_crash::Copy(page+4090,bytes.data(),bytes.size()));
    assert(!sdgo::native_crash::Copy(page+4096,bytes.data(),bytes.size()));
    model[0x124/4]=pointer(page+4096);model[0x128/4]=model[0x124/4]+20;
    const auto unreadable=Model(pointer(model.data()),model[0x124/4],table,table+4);
    assert(unreadable.layout&&unreadable.header&&unreadable.bounds&&!unreadable.window);
    assert((unreadable.nodes==std::array<std::uint32_t,5>{}));
    assert(VirtualFree(page,0,MEM_RELEASE));
    std::cout<<"Bounded model fault snapshots preserve data and reject unreadable ranges\n";
}
