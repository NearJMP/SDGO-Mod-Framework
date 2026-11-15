#include "sdgo/surface_decal_native.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

using namespace sdgo::surface_decal;
namespace {
unsigned checks=0;
void Check(bool value,const char* message) {
    ++checks;if(!value){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
int modelTable=0,obsTable=0,nodeTable=0,parentGuard=0,animateGuard=0,otherNodeMethod=0;
std::array<void*,21> boneNodeTable{},unsupportedNodeTable{};
bool fixedAliases=false;
std::array<float,16> matrix{{1,0,0,0,0,1,0,0,0,0,1,0,10,20,30,1}};
const std::array<float,16> identity{{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
const std::array<float,3> identityScale{{1,1,1}};
const std::array<float,4> identityRotation{{0,0,0,1}};
const std::array<float,3> zeroPosition{{0,0,0}};
const float* __fastcall World(void*,void*) {return matrix.data();}
void* Symbol(SdgoMod*,const char* name) {
    if(!std::strcmp(name,"surface_decal.model_vtable"))return &modelTable;
    if(!std::strcmp(name,"surface_decal.model_obs_vtable"))return &obsTable;
    if(!std::strcmp(name,"surface_decal.rigid_mesh_vtable"))return &nodeTable;
    if(!std::strcmp(name,"surface_decal.node_world"))return reinterpret_cast<void*>(&World);
    if(fixedAliases&&!std::strcmp(name,"surface_decal.node_parent"))return &parentGuard;
    if(fixedAliases&&!std::strcmp(name,"surface_decal.node_animate"))return &animateGuard;
    return nullptr;
}
template<class T,class A>void Put(A& bytes,std::size_t offset,T value) {std::memcpy(bytes.data()+offset,&value,sizeof value);}
template<class T,class A>void PutArray(A& bytes,std::size_t offset,const T& value) {std::memcpy(bytes.data()+offset,value.data(),sizeof value);}
void SetIdentityCommon(std::array<std::uint8_t,0x200>& common) {
    PutArray(common,0x12c,identity);PutArray(common,0x16c,identityScale);
    PutArray(common,0x178,identityRotation);PutArray(common,0x188,zeroPosition);
}
void SetIdentityCommon(std::array<std::uint8_t,0x400>& common) {
    PutArray(common,0x12c,identity);PutArray(common,0x16c,identityScale);
    PutArray(common,0x178,identityRotation);PutArray(common,0x188,zeroPosition);
}
struct Key3 {std::int16_t start=0,end=0;std::array<float,3> value{};};
struct Key4 {std::int16_t start=0,end=0;std::array<float,4> value{};};
static_assert(sizeof(Key3)==16&&sizeof(Key4)==20);
struct TrackFixture {
    std::array<std::uint8_t,0x18> wrapper{};
    std::array<std::uint8_t,0xdc> record{};
    std::vector<Key3> position;
    std::vector<Key4> rotation;
    std::vector<Key3> scale;
    TrackFixture() {Reset();}
    template<class T> void Descriptor(std::size_t offset,std::vector<T>& keys) {
        const auto begin=keys.capacity()?reinterpret_cast<std::uintptr_t>(keys.data()):std::uintptr_t{};
        const auto end=begin+keys.size()*sizeof(T);
        const auto capacity=begin+keys.capacity()*sizeof(T);
        Put(record,offset,begin);Put(record,offset+4,end);Put(record,offset+8,capacity);
    }
    void Sync() {
        Descriptor(0x04,position);Descriptor(0x14,rotation);Descriptor(0x24,scale);
    }
    void Reset() {
        position.clear();rotation.clear();scale.clear();record.fill(0);wrapper.fill(0);
        Put(wrapper,0,record.data());
        PutArray(record,0x34,identity);PutArray(record,0xb4,identityScale);
        PutArray(record,0xc0,identityRotation);PutArray(record,0xd0,zeroPosition);
        Sync();
    }
    void SetMatrix(const std::array<float,16>& value) {PutArray(record,0x34,value);}
    void SetScaleDefault(const std::array<float,3>& value) {PutArray(record,0xb4,value);}
    void SetRotationDefault(const std::array<float,4>& value) {PutArray(record,0xc0,value);}
    void SetPositionDefault(const std::array<float,3>& value) {PutArray(record,0xd0,value);}
};
struct Fixture {
    std::array<std::uint8_t,0x400> model{},node{},common{};
    std::array<void*,1> nodes{{node.data()}};
    std::array<std::array<float,8>,6> vertices{{
        {{-1,-1,0,0,0,1,0,0}},{{1,-1,0,0,0,1,1,0}},{{0,1,0,0,0,1,0.5f,1}},
        {{-1,-1,0.1f,0,0,1,0,0}},{{1,-1,0.1f,0,0,1,1,0}},{{0,1,0.1f,0,0,1,0.5f,1}}
    }};
    std::array<std::uint16_t,6> indices{{0,1,2,3,4,5}};
    std::array<std::uint32_t,6> groups{{0,1,0,3,1,7}};
    Fixture() {
        Put(model,0,&modelTable);Put(model,0x124,nodes.data());Put(model,0x128,nodes.data()+1);Put(model,0x12c,nodes.data()+1);
        Put(node,0,&nodeTable);Put(node,0x118,model.data());Put(node,0x11c,common.data());
        std::memcpy(node.data()+0x10,matrix.data(),sizeof(float)*matrix.size());
        Put(common,0x34,vertices.data());Put(common,0x38,6u);
        Put(common,0x58,indices.data());Put(common,0x5c,6u);
        Put(common,0x70,2u);Put(common,0xa4,6u);Put(common,0xa8,2u);
        Put(common,0x194,2u);Put(common,0x198,groups.data());
    }
};
struct FixedFixture {
    static constexpr std::size_t count=4;
    std::array<std::uint8_t,0x240> model{};
    std::array<std::array<std::uint8_t,0x240>,count> node{};
    std::array<std::array<std::uint8_t,0x200>,count> common{};
    std::array<void*,count> nodes{};
    std::array<std::array<void*,2>,count> tracks{};
    std::array<std::array<TrackFixture,2>,count> animation{};
    std::array<int,3> clipObjects{{1,2,3}};
    std::array<void*,3> clips{{&clipObjects[0],&clipObjects[1],&clipObjects[2]}};
    FixedFixture() {
        Put(model,0,&modelTable);
        Put(model,0x194,clips.data());Put(model,0x198,clips.data()+2);Put(model,0x19c,clips.data()+clips.size());
        for(std::uint32_t index=0;index<count;++index) {
            nodes[index]=node[index].data();SetIdentityCommon(common[index]);
            Put(node[index],0,&nodeTable);Put(node[index],0x118,model.data());Put(node[index],0x11c,common[index].data());
            Put(node[index],0x120,tracks[index].data());Put(node[index],0x124,tracks[index].data()+tracks[index].size());
            Put(node[index],0x128,tracks[index].data()+tracks[index].size());Put(node[index],0x1b0,static_cast<void*>(nullptr));
            Put(node[index],0x200,static_cast<void*>(nullptr));Put<std::int32_t>(common[index],0xa0,-1);
        }
        Put(model,0x124,nodes.data());Put(model,0x128,nodes.data()+nodes.size());Put(model,0x12c,nodes.data()+nodes.size());
        SetParent(1,0);SetParent(2,0);SetTrack(0,0,true);
    }
    void SetParent(std::uint32_t child,std::int32_t parent) {
        Put(common[child],0xa0,parent);Put(node[child],0x1b0,parent<0?nullptr:nodes[static_cast<std::size_t>(parent)]);
    }
    void SetOwner(std::uint32_t index,void* owner) {Put(node[index],0x118,owner);}
    void SetVtable(std::uint32_t index,void* vtable) {Put(node[index],0,vtable);}
    void SetModifier(std::uint32_t index,void* modifier) {Put(node[index],0x200,modifier);}
    void SetTrack(std::uint32_t index,std::uint32_t slot,bool enabled) {
        tracks[index][slot]=enabled?animation[index][slot].wrapper.data():nullptr;
    }
    void SetClipCount(std::size_t countValue) {Put(model,0x198,clips.data()+countValue);}
};
}
int main() {
    native::GeometryReader reader;std::vector<native::Node> nodes;
    Fixture f;
    Check(reader.Read(f.model.data(),nodes)==native::ReadResult::unbound,"unbound reader never touches native pointers");
    SdgoApi api{};api.version=1;api.Symbol=Symbol;
    Check(reader.Bind(&api,nullptr),"bind verified symbols");
    Check(reader.Read(f.model.data(),nodes)==native::ReadResult::ok,"read native-shaped geometry");
    Check(nodes.size()==1&&nodes[0].fixedGroup==0,"legacy declarations keep cross-node grouping disabled");
    Check(nodes.size()==1&&nodes[0].mesh.triangles.size()==1,"fixed decal material group excluded from paint surfaces");
    Check(nodes[0].triangleGroups==std::vector<std::uint32_t>{0},"surface triangles retain their draw group after excluding native stickers");
    f.groups={{0,1,7,3,1,0}};
    Check(reader.Read(f.model.data(),nodes)==native::ReadResult::ok&&nodes[0].triangleGroups==std::vector<std::uint32_t>{1},
          "excluding an earlier sticker group does not shift native surface group identity");
    f.groups={{0,1,0,3,1,7}};reader.Read(f.model.data(),nodes);
    Check(nodes[0].world==matrix&&nodes[0].index==0,"native transform and stable node index retained");
    Check(nodes[0].modelPoseValid&&nodes[0].modelPose==matrix,"accumulated model-space pose copied separately from world cache");
    const auto geometryIdentity=nodes[0].geometry;
    Put(f.model,0,&obsTable);
    Check(reader.Read(f.model.data(),nodes)==native::ReadResult::ok,"verified CModelObs shares the model-node contract");
    Put(f.model,0,&modelTable);f.vertices[0][0]=-2;
    Check(reader.Read(f.model.data(),nodes)==native::ReadResult::ok&&nodes[0].geometry!=geometryIdentity,"topology cache rejects changed vertex positions");
    f.vertices[0][0]=-1;f.vertices[0][6]=0.25f;matrix[12]=40;
    Check(reader.Read(f.model.data(),nodes)==native::ReadResult::ok&&nodes[0].geometry==geometryIdentity,"pose and UV do not change surface identity");
    Check(nodes[0].world[12]==40,"current animation transform copied each read");
    Check(reader.Read(reinterpret_cast<void*>(1),nodes)==native::ReadResult::wrongModel,"unreadable model fails safely");
    Put(f.model,0,&nodeTable);
    Check(reader.Read(f.model.data(),nodes)==native::ReadResult::wrongModel,"other native class rejected");
    Put(f.model,0,&modelTable);Put(f.model,0x128,reinterpret_cast<std::uintptr_t>(f.nodes.data())+1);
    Check(reader.Read(f.model.data(),nodes)==native::ReadResult::malformed,"misaligned node vector rejected");
    Put(f.model,0x128,f.nodes.data()+1);Put(f.node,0x118,reinterpret_cast<void*>(1));
    Check(reader.Read(f.model.data(),nodes)==native::ReadResult::malformed,"node must belong to selected model");
    Put(f.node,0x118,f.model.data());Put<std::uint8_t>(f.node,0x108,1);
    Check(reader.Read(f.model.data(),nodes)==native::ReadResult::unavailable,"hidden node not selectable");
    Put<std::uint8_t>(f.node,0x108,0);Put(f.common,0x38,5u);
    Check(reader.Read(f.model.data(),nodes)==native::ReadResult::malformed,"vertex count cannot exceed allocation");
    Put(f.common,0x38,6u);Put(f.common,0x5c,5u);
    Check(reader.Read(f.model.data(),nodes)==native::ReadResult::malformed,"index count cannot exceed allocation");
    Put(f.common,0x5c,6u);f.groups[3]=0;
    Check(reader.Read(f.model.data(),nodes)==native::ReadResult::malformed,"overlapping draw groups rejected");
    f.groups[3]=3;f.groups[4]=2;
    Check(reader.Read(f.model.data(),nodes)==native::ReadResult::malformed,"draw group past index buffer rejected");
    f.groups[4]=1;f.indices[1]=65535;
    Check(reader.Read(f.model.data(),nodes)==native::ReadResult::malformed,"triangle indices cannot escape vertex buffer");
    f.indices[1]=1;Put(f.common,0x34,reinterpret_cast<void*>(1));
    Check(reader.Read(f.model.data(),nodes)==native::ReadResult::malformed,"unreadable vertex buffer fails safely");
    Put(f.common,0x34,f.vertices.data());matrix[0]=std::numeric_limits<float>::infinity();
    Check(reader.Read(f.model.data(),nodes)==native::ReadResult::malformed,"invalid animation matrix rejected");
    matrix[0]=1;Put<float>(f.node,0x1c,1.0f);
    Check(reader.Read(f.model.data(),nodes)==native::ReadResult::ok&&!nodes[0].modelPoseValid,
          "invalid affine model pose disables cross-part transform without breaking single-node read");
    Put<float>(f.node,0x1c,0.0f);
    Check(nodes.size()==1&&nodes[0].geometry==geometryIdentity,"failures leave previous output untouched");
    Put(f.common,0xa4,1000000u);
    Check(reader.Read(f.model.data(),nodes)==native::ReadResult::budgetExceeded,"native geometry allocation is bounded");
    Fixture expanded;
    expanded.vertices[2]={{1,1,0,0,0,1,1,1}};expanded.vertices[3]={{-1,-1,0,0,0,1,0.25f,0.25f}};
    expanded.vertices[4]={{1,1,0,0,0,1,0.75f,0.75f}};expanded.vertices[5]={{-1,1,0,0,0,1,0,1}};expanded.groups[5]=0;
    Check(reader.Read(expanded.model.data(),nodes)==native::ReadResult::ok&&nodes[0].mesh.vertices.size()==4,
          "native expanded triangle corners are welded across UV seams");
    Placement p;Patch patch;Hit hit{0,{{0.2,0.2,0.6}},0};
    Check(MakePlacement(nodes[0].mesh,hit,nodes[0].geometry,0,{1,0,0},4,4,0.5,p)==Result::ok&&
          Project(nodes[0].mesh,nodes[0].geometry,0,p,patch)==Result::ok&&patch.vertices.size()==6,
          "a projected sticker crosses adjacent expanded native faces");

    std::vector<native::FixedBinding> bindings;
    Check(reader.ReadFixedBindings(f.model.data(),bindings)==native::ReadResult::unbound,
          "legacy declaration cannot expose unguarded fixed-part ABI");
    fixedAliases=true;native::GeometryReader groupReader;
    Check(groupReader.Bind(&api,nullptr),"fixed-part reader binds guarded aliases without changing legacy requirements");
    boneNodeTable[18]=&animateGuard;unsupportedNodeTable[18]=&otherNodeMethod;
    FixedFixture fixed;
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok,"read bounded fixed-part ownership snapshot");
    Check(bindings.size()==FixedFixture::count&&bindings[1].valid&&bindings[2].valid&&
          bindings[1].group!=0&&bindings[1].group==bindings[2].group,
          "rigid children with fixed locals share the same static root group");
    Check(bindings[1].parentIndex==0&&bindings[1].runtimeParentIndex==0&&
          bindings[0].nonNullTrackSlots==std::vector<std::uint32_t>{0}&&
          bindings[0].constantTrackSlots==std::vector<std::uint32_t>{0}&&bindings[0].unknownTrackSlots.empty(),
          "serialized parent, runtime parent and native export-roundoff-stable slots are reported");
    Check(bindings[0].trackDiagnostics.size()==2&&
          bindings[0].trackDiagnostics[0].state==native::TrackDiagnosticState::exportStable&&
          bindings[0].trackDiagnostics[0].reason==native::TrackDiagnosticReason::none&&
          bindings[0].trackDiagnostics[0].consumer.record==reinterpret_cast<std::uintptr_t>(fixed.animation[0][0].record.data())&&
          bindings[0].trackDiagnostics[0].consumer.quaternionKeys.stride==20&&
          bindings[0].trackDiagnostics[1].state==native::TrackDiagnosticState::nullSlot&&
          !std::strcmp(native::TrackDiagnosticStateName(bindings[0].trackDiagnostics[0].state),"native_export_roundoff_stable"),
          "bounded per-slot diagnostics expose consumer-proven metadata and readable names");
    const auto sharedGroup=bindings[1].group;

    fixed.SetTrack(1,1,true);
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&
          bindings[1].group==sharedGroup&&bindings[1].constantTrackSlots==std::vector<std::uint32_t>{1},
          "export-roundoff-stable nonnull rigid track inherits its parent driver");
    const std::array<float,3> exportJitterScale{{1.0000005f,0.9999995f,1.0000002f}};
    const std::array<float,4> exportJitterRotation{{5.0e-7f,-5.0e-7f,2.0e-7f,0.9999995f}};
    const std::array<float,3> exportJitterPosition{{5.0e-7f,-1.37e-6f,3.0e-7f}};
    auto exportJitterMatrix=identity;
    exportJitterMatrix[0]+=5.0e-7f;exportJitterMatrix[5]-=5.0e-7f;exportJitterMatrix[13]=-1.37e-6f;
    fixed.animation[1][1].SetScaleDefault(exportJitterScale);
    fixed.animation[1][1].SetRotationDefault(exportJitterRotation);
    fixed.animation[1][1].SetPositionDefault(exportJitterPosition);
    fixed.animation[1][1].SetMatrix(exportJitterMatrix);
    fixed.animation[1][1].position.push_back({0,10,exportJitterPosition});
    fixed.animation[1][1].rotation.push_back({0,10,exportJitterRotation});
    fixed.animation[1][1].scale.push_back({0,10,exportJitterScale});fixed.animation[1][1].Sync();
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&
          bindings[1].group==sharedGroup&&
          bindings[1].trackDiagnostics[1].state==native::TrackDiagnosticState::exportStable&&
          bindings[1].trackDiagnostics[1].consumer.decodedKeyCount==3,
          "generic M/S/Q/P defaults and keys within native export roundoff remain stable");
    fixed.animation[1][1].Reset();
    fixed.animation[1][1].scale.push_back({0,10,{{2,1,1}}});fixed.animation[1][1].Sync();
    Check(!groupReader.FixedGroupMatches(fixed.model.data(),1,sharedGroup)&&
          groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&bindings[1].group!=bindings[2].group&&
          bindings[1].trackDiagnostics[1].state==native::TrackDiagnosticState::dynamic&&
          bindings[1].trackDiagnostics[1].reason==native::TrackDiagnosticReason::scaleKeysDynamic,
          "one differing key makes the rigid node its own driver and reports the scale-key reason");
    fixed.animation[1][1].Reset();
    fixed.animation[1][1].SetScaleDefault({{2,1,1}});
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&bindings[1].group!=bindings[2].group&&
          bindings[1].trackDiagnostics[1].reason==native::TrackDiagnosticReason::recordScaleDefaultMismatch,
          "empty component default mismatch rejects export-stable track inheritance with an exact reason");
    fixed.animation[1][1].Reset();
    const std::array<float,3> largePosition{{0,1000.0f,0}};
    auto largeMatrix=identity;largeMatrix[13]=largePosition[1];
    PutArray(fixed.common[1],0x188,largePosition);PutArray(fixed.common[1],0x12c,largeMatrix);
    auto changedLargePosition=largePosition;changedLargePosition[1]+=1.0e-4f;
    auto changedLargeMatrix=largeMatrix;changedLargeMatrix[13]=changedLargePosition[1];
    fixed.animation[1][1].SetPositionDefault(changedLargePosition);
    fixed.animation[1][1].SetMatrix(changedLargeMatrix);
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&
          bindings[1].group!=bindings[2].group&&
          bindings[1].trackDiagnostics[1].reason==native::TrackDiagnosticReason::recordPositionDefaultMismatch,
          "a roughly 1e-4 default delta at a large coordinate exceeds the fixed absolute export bound");
    fixed.animation[1][1].SetPositionDefault(largePosition);fixed.animation[1][1].SetMatrix(largeMatrix);
    fixed.animation[1][1].position.push_back({0,10,changedLargePosition});fixed.animation[1][1].Sync();
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&
          bindings[1].group!=bindings[2].group&&
          bindings[1].trackDiagnostics[1].reason==native::TrackDiagnosticReason::positionKeysDynamic,
          "a roughly 1e-4 key delta remains a real driver even at a large coordinate");
    SetIdentityCommon(fixed.common[1]);Put<std::int32_t>(fixed.common[1],0xa0,0);fixed.animation[1][1].Reset();
    const std::array<float,3> doubledScale{{2,1,1}};
    auto doubledScaleMatrix=identity;doubledScaleMatrix[0]=2;
    PutArray(fixed.common[1],0x16c,doubledScale);PutArray(fixed.common[1],0x12c,doubledScaleMatrix);
    fixed.animation[1][1].SetScaleDefault(doubledScale);fixed.animation[1][1].SetMatrix(doubledScaleMatrix);
    fixed.animation[1][1].rotation.push_back({0,10,identityRotation});fixed.animation[1][1].Sync();
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&bindings[1].group!=bindings[2].group&&
          bindings[1].trackDiagnostics[1].reason==native::TrackDiagnosticReason::quaternionPathMissingScale,
          "quaternion-key path rejects empty scale whose default is not the combined sampler identity");
    PutArray(fixed.common[1],0x16c,identityScale);PutArray(fixed.common[1],0x12c,identity);fixed.animation[1][1].Reset();

    const float halfSqrt=0.70710677f;
    const std::array<float,3> composedScale{{2,3,4}};
    const std::array<float,4> composedRotation{{0,0,halfSqrt,halfSqrt}};
    const std::array<float,3> composedPosition{{5,6,7}};
    const std::array<float,16> composedMatrix{{0,2,0,0,-3,0,0,0,0,0,4,0,5,6,7,1}};
    PutArray(fixed.common[1],0x16c,composedScale);PutArray(fixed.common[1],0x178,composedRotation);
    PutArray(fixed.common[1],0x188,composedPosition);PutArray(fixed.common[1],0x12c,composedMatrix);
    fixed.animation[1][1].SetScaleDefault(composedScale);fixed.animation[1][1].SetRotationDefault(composedRotation);
    fixed.animation[1][1].SetPositionDefault(composedPosition);fixed.animation[1][1].SetMatrix(composedMatrix);
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&bindings[1].group==sharedGroup,
          "native scale-rotation-translation composition accepts an export-roundoff-stable nonidentity local");
    SetIdentityCommon(fixed.common[1]);Put<std::int32_t>(fixed.common[1],0xa0,0);fixed.animation[1][1].Reset();

    const std::array<float,4> exportedSuperunitRotation{{0.707107008f,0.707107008f,0.0f,9.09962978e-7f}};
    const std::array<float,16> exportedSuperunitMatrix{{
        -5.96046448e-7f,1.00000060f,-1.28688237e-6f,0.0f,
        1.00000060f,-5.96046448e-7f,1.28688237e-6f,0.0f,
        1.28688237e-6f,-1.28688237e-6f,-1.00000119f,0.0f,
        0.0f,0.0f,0.0f,1.0f}};
    PutArray(fixed.common[1],0x178,exportedSuperunitRotation);PutArray(fixed.common[1],0x12c,exportedSuperunitMatrix);
    fixed.animation[1][1].SetRotationDefault(exportedSuperunitRotation);
    fixed.animation[1][1].SetMatrix(exportedSuperunitMatrix);
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&
          bindings[1].group==sharedGroup&&bindings[1].trackDiagnostics[1].state==native::TrackDiagnosticState::exportStable,
          "native close-path accepts the observed export-rounded quaternion whose squared norm is slightly above one");
    const std::array<float,4> outsideCloseRotation{{0,0,0,0.5f}};
    PutArray(fixed.common[1],0x178,outsideCloseRotation);PutArray(fixed.common[1],0x12c,identity);
    fixed.animation[1][1].SetRotationDefault(outsideCloseRotation);fixed.animation[1][1].SetMatrix(identity);
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&
          bindings[1].group!=bindings[2].group&&bindings[1].unknownTrackSlots==std::vector<std::uint32_t>{1}&&
          bindings[1].trackDiagnostics[1].reason==native::TrackDiagnosticReason::commonQuaternionOutsideClosePath,
          "quaternion outside the native close path remains unknown and nonmergeable");
    PutArray(fixed.common[1],0x178,identityRotation);fixed.animation[1][1].Reset();
    auto mismatchedMatrix=identity;mismatchedMatrix[0]=2;
    PutArray(fixed.common[1],0x12c,mismatchedMatrix);fixed.animation[1][1].SetMatrix(mismatchedMatrix);
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&
          bindings[1].group!=bindings[2].group&&bindings[1].unknownTrackSlots==std::vector<std::uint32_t>{1}&&
          bindings[1].trackDiagnostics[1].reason==native::TrackDiagnosticReason::commonMatrixSqtMismatch,
          "common matrix and common SQT mismatch stays unknown and reports the matrix reason");
    PutArray(fixed.common[1],0x12c,identity);fixed.animation[1][1].Reset();

    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&bindings[1].group==sharedGroup,
          "restored export-stable record returns to the fixed parent group");
    const auto restoredGroup=bindings[1].group;
    Put(fixed.animation[1][1].record,0x28,reinterpret_cast<std::uintptr_t>(fixed.animation[1][1].record.data())+1);
    Check(!groupReader.FixedGroupMatches(fixed.model.data(),1,restoredGroup)&&
          groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&
          bindings[1].unknownTrackSlots==std::vector<std::uint32_t>{1}&&
          bindings[1].trackDiagnostics[1].reason==native::TrackDiagnosticReason::scaleDescriptorMalformed,
          "a one-byte descriptor change remains exact, malformed and nonmergeable");
    fixed.animation[1][1].Reset();fixed.animation[1][1].scale.push_back({0,10,identityScale});fixed.animation[1][1].Sync();
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&bindings[1].group==sharedGroup,
          "export-stable payload remains eligible before mutation");
    fixed.animation[1][1].scale[0].value[0]=2;
    Check(!groupReader.FixedGroupMatches(fixed.model.data(),1,sharedGroup),"changed payload rejects the previously observed group");
    fixed.animation[1][1].Reset();

    fixed.SetClipCount(3);
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::malformed&&
          !groupReader.FixedGroupMatches(fixed.model.data(),1,sharedGroup),
          "clip growth before node-slot append rejects the old group");
    fixed.SetClipCount(2);
    fixed.SetVtable(0,boneNodeTable.data());
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&bindings[0].valid&&
          bindings[0].group==bindings[1].group&&static_cast<std::uint32_t>(bindings[0].group)==1&&
          bindings[0].trackDiagnostics[0].state==native::TrackDiagnosticState::notInspected&&
          bindings[0].trackDiagnostics[0].reason==native::TrackDiagnosticReason::nonRigidBoundary&&
          bindings[0].trackDiagnostics[0].consumer.record==0&&
          bindings[0].trackDiagnostics[0].consumer.decodedKeyCount==0,
          "bone track remains its own driver boundary without dereferencing its record");
    fixed.SetVtable(0,&nodeTable);
    matrix[0]=2;
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&bindings[1].group==sharedGroup,
          "world-matrix changes do not classify fixed ownership");
    matrix[0]=1;

    fixed.SetModifier(1,reinterpret_cast<void*>(0x3456));
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&bindings[1].group!=bindings[2].group&&
          bindings[1].trackDiagnostics[1].state==native::TrackDiagnosticState::notInspected&&
          bindings[1].trackDiagnostics[1].reason==native::TrackDiagnosticReason::dynamicModifierBoundary,
          "dynamic modifier makes that node its own driver without decoding its tracks");
    fixed.SetModifier(1,nullptr);
    fixed.SetParent(1,-1);fixed.SetParent(2,-1);
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&
          bindings[1].group!=0&&bindings[2].group!=0&&bindings[1].group!=bindings[2].group,
          "separate static roots use distinct model-local sentinels");
    fixed.SetParent(1,0);fixed.SetParent(2,0);fixed.SetParent(1,2);fixed.SetParent(2,1);
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&
          !bindings[1].valid&&!bindings[2].valid&&!bindings[1].group&&!bindings[2].group,
          "cyclic parent chains remain nonmergeable");
    fixed.SetParent(1,0);fixed.SetParent(2,0);fixed.SetOwner(1,reinterpret_cast<void*>(1));
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&
          !bindings[1].valid&&!bindings[1].group,"owner mismatch remains nonmergeable");
    fixed.SetOwner(1,fixed.model.data());Put(fixed.node[1],0x1b0,fixed.nodes[2]);
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&
          !bindings[1].valid&&!bindings[1].group,"serialized and runtime parent mismatch remains nonmergeable");
    fixed.SetParent(1,0);fixed.SetVtable(1,unsupportedNodeTable.data());
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&
          !bindings[1].valid&&!bindings[1].group,"unsupported Animate override remains nonmergeable");
    fixed.SetVtable(1,reinterpret_cast<void*>(1));
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&
          !bindings[1].valid&&!bindings[1].group,"unreadable wrong vtable remains nonmergeable");
    fixed.SetVtable(1,&nodeTable);fixed.SetTrack(0,0,false);fixed.SetTrack(1,1,false);
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::ok&&
          std::all_of(bindings.begin(),bindings.end(),[](const native::FixedBinding& binding){return binding.group==0;}),
          "an entirely unbound animation graph stays conservatively disabled");
    fixed.SetTrack(0,0,true);
    Put(fixed.node[1],0x124,reinterpret_cast<std::uintptr_t>(fixed.tracks[1].data())+1);
    Check(groupReader.ReadFixedBindings(fixed.model.data(),bindings)==native::ReadResult::malformed&&
          !groupReader.FixedGroupMatches(fixed.model.data(),1,sharedGroup),
          "misaligned track vector and unknown group zero fail closed");
    Put(fixed.node[1],0x124,fixed.tracks[1].data()+fixed.tracks[1].size());
    Check(!groupReader.FixedGroupMatches(fixed.model.data(),1,0),"group zero is never a shared identity");

    FixedFixture boneBudget;
    for(std::uint32_t index=0;index<3;++index) {
        boneBudget.SetVtable(index,boneNodeTable.data());
        for(std::uint32_t slot=0;slot<2;++slot) {
            auto& track=boneBudget.animation[index][slot];
            track.position.assign(4096,Key3{0,0,zeroPosition});
            track.rotation.assign(4096,Key4{0,0,identityRotation});
            track.scale.assign(4096,Key3{0,0,identityScale});
            track.Sync();
            boneBudget.SetTrack(index,slot,true);
        }
    }
    auto& laterRigid=boneBudget.animation[3][0];
    laterRigid.position.assign(4096,Key3{0,0,zeroPosition});
    laterRigid.scale.assign(4096,Key3{0,0,identityScale});
    laterRigid.Sync();
    boneBudget.SetTrack(3,0,true);
    Check(groupReader.ReadFixedBindings(boneBudget.model.data(),bindings)==native::ReadResult::ok&&
          bindings[0].trackDiagnostics[0].state==native::TrackDiagnosticState::notInspected&&
          bindings[2].trackDiagnostics[1].consumer.decodedKeyCount==0&&
          bindings[3].constantTrackSlots==std::vector<std::uint32_t>{0}&&bindings[3].unknownTrackSlots.empty()&&
          bindings[3].trackDiagnostics[0].state==native::TrackDiagnosticState::exportStable&&
          bindings[3].trackDiagnostics[0].consumer.decodedKeyCount==8192,
          "bone tracks do not consume the whole-model key budget before a later rigid track");
    Fixture groupedGeometry;TrackFixture groupedTrack;std::array<void*,1> groupedSlots{{groupedTrack.wrapper.data()}};
    int groupedClipObject=1;std::array<void*,1> groupedClips{{&groupedClipObject}};
    Put(groupedGeometry.model,0x194,groupedClips.data());Put(groupedGeometry.model,0x198,groupedClips.data()+1);
    Put(groupedGeometry.model,0x19c,groupedClips.data()+1);SetIdentityCommon(groupedGeometry.common);
    Put(groupedGeometry.node,0x120,groupedSlots.data());Put(groupedGeometry.node,0x124,groupedSlots.data()+1);
    Put(groupedGeometry.node,0x128,groupedSlots.data()+1);Put(groupedGeometry.node,0x1b0,static_cast<void*>(nullptr));
    Put<std::int32_t>(groupedGeometry.common,0xa0,-1);
    Check(groupReader.Read(groupedGeometry.model.data(),nodes)==native::ReadResult::ok&&nodes[0].fixedGroup!=0,
          "geometry read carries the guarded fixed group into the selected native node");
    Check(reader.Read(groupedGeometry.model.data(),nodes)==native::ReadResult::ok&&nodes[0].fixedGroup==0,
          "the same geometry remains single-node for a legacy declaration");
    std::cout<<checks<<" checks passed\n";
}