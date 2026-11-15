#pragma once
#include "sdgo/surface_decal_protocol.h"
#include "sdgo/layered_decal_wire.h"

// Native isolated-test contract. No network endpoint or alternative player UI.
// Both exports require the active native game-test runtime on the game thread.
namespace sdgo::surface_decal::test {
struct Snapshot {
    std::uint32_t size=sizeof(Snapshot),version=1;
    std::uint32_t editing=0,placing=0,state=0,pending=0,selected=0;
    std::uint32_t freshViews=0,triangles=0,draws=0;
    std::uint32_t boundModels=0,positionedModels=0;
    std::uint32_t battleModels=0,battlePositioned=0,battleDraws=0;
    wire::Bytes saved{},preview{};
};
using Observe=int(__cdecl*)(Snapshot*);
using Place=int(__cdecl*)(double,double);
struct TargetSnapshot {
    std::uint32_t size=sizeof(TargetSnapshot),version=1;
    std::uint32_t battleModels=0,triangles=0,draws=0;
    std::uint32_t nodes=0,pendingGeometry=0,matchingStickers=0;
    wire::Bytes appearance{};
};
using Target=int(__cdecl*)(std::uint64_t,TargetSnapshot*);
struct RoomSnapshot {
    std::uint32_t size=sizeof(RoomSnapshot),version=1;
    std::uint32_t models=0,triangles=0,draws=0;
    std::uint32_t nodes=0,pendingGeometry=0,matchingStickers=0;
    wire::Bytes appearance{};
};
using RoomTarget=int(__cdecl*)(std::uint64_t,RoomSnapshot*);
inline constexpr char roomExport[]="SdgoSurfaceDecalTestRoomV1";
inline constexpr char targetExport[]="SdgoSurfaceDecalTestTargetV1";
inline constexpr char observeExport[]="SdgoSurfaceDecalTestObserveV1";
inline constexpr char placeExport[]="SdgoSurfaceDecalTestPlaceV1";
struct LayerSnapshot {
    std::uint32_t size=sizeof(LayerSnapshot),version=2;
    std::uint32_t editing=0,placing=0,state=0,pending=0,selected=0,armed=0;
    std::uint32_t models=0,triangles=0,draws=0,roomDraws=0,battleDraws=0;
    std::uint32_t savedBytes=0,previewBytes=0,stockTypes=0,quotedItems=0,previewLayers=0;
    std::array<std::uint8_t,layers::wire::maxBytes> saved{},preview{};
    std::array<std::uint32_t,layers::maximum> layerIDs{},layerDraws{};
    std::array<std::uint32_t,layers::maximum> roomLayerDraws{},battleLayerDraws{};
    layers::wire::Token lastCommit{};
    std::uint32_t foldedTriangles=0; // visible wrapped faces rejected by the old planar normal cutoff
    std::uint32_t nativeStockKnown=0,nativeStockPage=0,nativeStockPending=0,nativeStockTotal=0;
    std::array<std::uint8_t,313> nativeStock{};
    std::uint32_t fixedParts=0,fixedPartDraws=0,fixedNodes=0,fixedGroups=0;
};
using LayerObserve=int(__cdecl*)(std::uint64_t,LayerSnapshot*);
inline constexpr char layerExport[]="SdgoSurfaceDecalTestLayersV2";
using DumpBindings=int(__cdecl*)(std::uint64_t,const wchar_t*);
inline constexpr char bindingsExport[]="SdgoSurfaceDecalTestDumpBindingsV1";
struct SceneHeader {
    std::uint32_t magic=0x53444150,version=2,state=0,bytes=sizeof(LayerSnapshot);
    std::uint64_t uid=0,tick=0;
};
}
