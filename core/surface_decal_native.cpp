#include "sdgo/surface_decal_native.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <tuple>

namespace sdgo::surface_decal::native {
static_assert(sizeof(void*)==4, "The native GOnline mesh reader requires Win32");
namespace {
// GOnline-54BE1568-00C97000: CModel::ExpandVertex4Lightmap (0x00BEEB20)
// walks +0x124/+0x128, CNode::GetNodeCommon (0x00C13820) reads +0x11C,
// and the native index expansion reads common +0x34/+0x58, stride 32/2.
// CModel::LinkNodes (0x00BEF1E0) links common+0xA0 to node+0x1B0.
// CNode animation slots are the vector at +0x120/+0x124/+0x128 and the
// independent dynamic modifier is +0x200.  The optional guarded aliases in
// Bind gate this private ABI; legacy declarations keep fixedGroup disabled.
bool Copy(const void* p, std::size_t offset, void* out, std::size_t bytes) noexcept {
    if (!p || !out || !bytes || offset>std::numeric_limits<std::uintptr_t>::max()-reinterpret_cast<std::uintptr_t>(p)) return false;
    const auto address=reinterpret_cast<std::uintptr_t>(p)+offset;
    if (bytes>std::numeric_limits<std::uintptr_t>::max()-address) return false;
    __try {std::memcpy(out,reinterpret_cast<const void*>(address),bytes);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
template<class T> bool ReadAt(const void* p,std::size_t offset,T& out) {return Copy(p,offset,&out,sizeof out);}
bool Matrix(void* node,const float*(__thiscall* world)(void*),std::array<float,16>& matrix) noexcept {
    __try {
        const auto* source=world(node);
        return Copy(source,0,matrix.data(),sizeof(float)*matrix.size());
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool ModelPose(void* node,std::array<float,16>& matrix) noexcept {
    // Animate composes node+0x10 with parent+0x10; GetWorldTM 0x00C13530
    // then multiplies this accumulated model-space pose by owner world and
    // writes the separate world cache at +0x170.  It is not a raw local TM.
    if(!Copy(node,0x10,matrix.data(),sizeof(float)*matrix.size()))return false;
    for(const auto value:matrix)if(!std::isfinite(value))return false;
    constexpr float epsilon=0.001f;
    return std::abs(matrix[3])<=epsilon&&std::abs(matrix[7])<=epsilon&&
           std::abs(matrix[11])<=epsilon&&std::abs(matrix[15]-1.0f)<=epsilon;
}
struct NativeVertex {float x,y,z,nx,ny,nz,u,v;};
struct Group {std::uint32_t start,triangles,material;};
struct VectorTriple {std::uintptr_t begin=0,end=0,capacity=0;};
static_assert(sizeof(NativeVertex)==32 && sizeof(Group)==12);
constexpr std::size_t kMaxNodes=4096;
constexpr std::size_t kMaxClips=4096;
constexpr std::size_t kMaxTrackSlotsPerNode=4096;
constexpr std::size_t kMaxTrackSlotsPerModel=65536;
constexpr std::size_t kMaxTrackKeysPerComponent=4096;
constexpr std::size_t kMaxTrackKeysPerModel=65536;

bool ReadTriple(const void* object,std::size_t offset,VectorTriple& triple) {
    return ReadAt(object,offset,triple.begin)&&ReadAt(object,offset+4,triple.end)&&ReadAt(object,offset+8,triple.capacity);
}
bool Same(const VectorTriple& a,const VectorTriple& b) {
    return a.begin==b.begin&&a.end==b.end&&a.capacity==b.capacity;
}
ReadResult SnapshotPointerVector(const void* object,std::size_t offset,std::size_t maxCount,
                                 std::size_t maxCapacity,std::vector<std::uintptr_t>& values,
                                 VectorTriple& observed) {
    VectorTriple first;
    if(!ReadTriple(object,offset,first))return ReadResult::malformed;
    if(first.end<first.begin||first.capacity<first.end||first.begin%4||first.end%4||first.capacity%4)
        return ReadResult::malformed;
    if(!first.begin&&(first.end||first.capacity))return ReadResult::malformed;
    const auto countBytes=first.end-first.begin;
    const auto capacityBytes=first.capacity-first.begin;
    if(countBytes%4||capacityBytes%4)return ReadResult::malformed;
    const auto count=static_cast<std::size_t>(countBytes/4);
    const auto capacity=static_cast<std::size_t>(capacityBytes/4);
    if(count>maxCount||capacity>maxCapacity)return ReadResult::budgetExceeded;
    std::vector<std::uintptr_t> firstValues(count);
    if(count&&!Copy(reinterpret_cast<const void*>(first.begin),0,firstValues.data(),count*4))return ReadResult::malformed;
    VectorTriple second;
    if(!ReadTriple(object,offset,second)||!Same(first,second))return ReadResult::malformed;
    std::vector<std::uintptr_t> secondValues(count);
    if(count&&!Copy(reinterpret_cast<const void*>(second.begin),0,secondValues.data(),count*4))return ReadResult::malformed;
    VectorTriple third;
    if(!ReadTriple(object,offset,third)||!Same(first,third)||firstValues!=secondValues)return ReadResult::malformed;
    values=std::move(firstValues);observed=first;return ReadResult::ok;
}
ReadResult SnapshotModelNodes(void* model,std::vector<std::uintptr_t>& nodes) {
    VectorTriple ignored;
    const auto result=SnapshotPointerVector(model,0x124,kMaxNodes,kMaxNodes,nodes,ignored);
    if(result!=ReadResult::ok)return result;
    return nodes.empty()?ReadResult::unavailable:ReadResult::ok;
}
ReadResult SnapshotModelClips(void* model,std::vector<std::uintptr_t>& clips) {
    VectorTriple ignored;
    const auto result=SnapshotPointerVector(model,0x194,kMaxClips,kMaxClips,clips,ignored);
    if(result!=ReadResult::ok)return result;
    return std::any_of(clips.begin(),clips.end(),[](std::uintptr_t clip){return !clip;})
        ?ReadResult::malformed:ReadResult::ok;
}
std::uint64_t FixedGroup(std::uintptr_t model,std::uint32_t node,bool staticRoot) {
    auto low=node+1u;
    if(staticRoot)low|=0x80000000u;
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(model))<<32)|low;
}
bool SupportedNodeVtable(std::uintptr_t vtable,const void* rigidMeshVtable,const void* nodeAnimateGuard) {
    if(vtable==reinterpret_cast<std::uintptr_t>(rigidMeshVtable))return true;
    std::uintptr_t animate=0;
    // The CNode base vtable 0x00EED85C and the non-rigid/simple-node
    // vtable 0x00EE965C both retain CNode::Animate at slot +0x48.  The
    // 0x00EED90C derived family overrides that slot and remains unsupported.
    return vtable&&nodeAnimateGuard&&
           ReadAt(reinterpret_cast<const void*>(vtable),0x48,animate)&&
           animate==reinterpret_cast<std::uintptr_t>(nodeAnimateGuard);
}
struct LocalTransform {
    std::array<float,16> matrix{};
    std::array<float,3> scale{};
    std::array<float,4> rotation{};
    std::array<float,3> position{};
};
struct TrackRecordMetadata {
    VectorTriple positionKeys;
    VectorTriple rotationKeys;
    VectorTriple scaleKeys;
    std::array<float,16> matrix{};
    std::array<float,3> scaleDefault{};
    std::array<float,4> rotationDefault{};
    std::array<float,3> positionDefault{};
};
struct TrackPayload {
    std::vector<std::uint8_t> position;
    std::vector<std::uint8_t> rotation;
    std::vector<std::uint8_t> scale;
};
enum class KeyState {constant,dynamic,malformed};

bool ReadLocalTransform(const void* common,LocalTransform& value) {
    // Transition/Blend consume +0x16C as scale and place it on the matrix
    // diagonal, +0x178 as quaternion, and +0x188 as translation.  The field
    // order is therefore intentionally not position/quaternion/scale.
    return Copy(common,0x12c,value.matrix.data(),sizeof value.matrix)&&
           Copy(common,0x16c,value.scale.data(),sizeof value.scale)&&
           Copy(common,0x178,value.rotation.data(),sizeof value.rotation)&&
           Copy(common,0x188,value.position.data(),sizeof value.position);
}
bool ReadTrackRecordMetadata(const void* record,TrackRecordMetadata& value) {
    // CTrackRecord: +0x04 position keys (16-byte), +0x14 quaternion keys
    // (20-byte), +0x24 scale keys (16-byte), +0x34 default matrix,
    // +0xB4 scale, +0xC0 quaternion, +0xD0 position.
    return ReadTriple(record,0x04,value.positionKeys)&&ReadTriple(record,0x14,value.rotationKeys)&&
           ReadTriple(record,0x24,value.scaleKeys)&&
           Copy(record,0x34,value.matrix.data(),sizeof value.matrix)&&
           Copy(record,0xb4,value.scaleDefault.data(),sizeof value.scaleDefault)&&
           Copy(record,0xc0,value.rotationDefault.data(),sizeof value.rotationDefault)&&
           Copy(record,0xd0,value.positionDefault.data(),sizeof value.positionDefault);
}
template<std::size_t N> bool SameBits(const std::array<float,N>& a,const std::array<float,N>& b) {
    return !std::memcmp(a.data(),b.data(),sizeof a);
}
bool Same(const LocalTransform& a,const LocalTransform& b) {
    return SameBits(a.matrix,b.matrix)&&SameBits(a.scale,b.scale)&&
           SameBits(a.rotation,b.rotation)&&SameBits(a.position,b.position);
}
bool Same(const TrackRecordMetadata& a,const TrackRecordMetadata& b) {
    return Same(a.positionKeys,b.positionKeys)&&Same(a.rotationKeys,b.rotationKeys)&&Same(a.scaleKeys,b.scaleKeys)&&
           SameBits(a.matrix,b.matrix)&&SameBits(a.scaleDefault,b.scaleDefault)&&
           SameBits(a.rotationDefault,b.rotationDefault)&&SameBits(a.positionDefault,b.positionDefault);
}
bool Finite(const LocalTransform& value) {
    for(const auto x:value.matrix)if(!std::isfinite(x))return false;
    for(const auto x:value.scale)if(!std::isfinite(x))return false;
    for(const auto x:value.rotation)if(!std::isfinite(x))return false;
    for(const auto x:value.position)if(!std::isfinite(x))return false;
    return true;
}
template<std::size_t N> bool Finite(const std::array<float,N>& value) {
    for(const auto x:value)if(!std::isfinite(x))return false;
    return true;
}
template<std::size_t N> bool NativeExportRoundoffStable(const std::array<float,N>& a,
                                                        const std::array<float,N>& b) {
    // Independently exported native defaults can differ by a few float ULPs.
    // Keep a fixed absolute ceiling so large coordinates cannot amplify it.
    for(std::size_t i=0;i<N;++i) {
        if(!std::isfinite(a[i])||!std::isfinite(b[i])||
           std::abs(static_cast<double>(a[i])-static_cast<double>(b[i]))>kNativeExportRoundoffTolerance)
            return false;
    }
    return true;
}
bool NativeCloseQuaternion(const std::array<float,4>& q) {
    double dot=0;
    for(const auto x:q)dot+=static_cast<double>(x)*x;
    // Native CA5EA9 selects the quaternion sign, then uses the linear close path when
    // 1-abs(dot)<=FLT_EPSILON. Export rounding can therefore produce dot slightly above one.
    return std::isfinite(dot)&&1.0-std::abs(dot)<=std::numeric_limits<float>::epsilon();
}
std::array<float,16> ComposeLocal(const LocalTransform& value) {
    const auto x=value.rotation[0],y=value.rotation[1],z=value.rotation[2],w=value.rotation[3];
    const auto x2=x+x,y2=y+y,z2=z+z;
    const auto xx=x*x2,xy=x*y2,xz=x*z2;
    const auto yy=y*y2,yz=y*z2,zz=z*z2;
    const auto wx=w*x2,wy=w*y2,wz=w*z2;
    std::array<float,16> matrix{};
    // Native C23D70 builds Scale * RotationQuaternion in D3DX row-vector
    // layout, then writes translation into the last row.
    matrix[0]=value.scale[0]*(1.0f-yy-zz);
    matrix[1]=value.scale[0]*(xy+wz);
    matrix[2]=value.scale[0]*(xz-wy);
    matrix[4]=value.scale[1]*(xy-wz);
    matrix[5]=value.scale[1]*(1.0f-xx-zz);
    matrix[6]=value.scale[1]*(yz+wx);
    matrix[8]=value.scale[2]*(xz+wy);
    matrix[9]=value.scale[2]*(yz-wx);
    matrix[10]=value.scale[2]*(1.0f-xx-yy);
    matrix[12]=value.position[0];matrix[13]=value.position[1];matrix[14]=value.position[2];matrix[15]=1.0f;
    return matrix;
}
bool MatrixMatches(const std::array<float,16>& actual,const std::array<float,16>& expected) {
    return NativeExportRoundoffStable(actual,expected);
}
bool ValidateDescriptor(const VectorTriple& triple,std::size_t stride,std::size_t& count) {
    count=0;
    if(!triple.begin)return !triple.end&&!triple.capacity;
    if(triple.begin%4||triple.end%4||triple.capacity%4||triple.end<triple.begin||triple.capacity<triple.end)return false;
    const auto bytes=triple.end-triple.begin;
    const auto capacityBytes=triple.capacity-triple.begin;
    if(bytes%stride||capacityBytes%stride)return false;
    count=static_cast<std::size_t>(bytes/stride);
    return count<=kMaxTrackKeysPerComponent&&capacityBytes/stride<=kMaxTrackKeysPerComponent;
}
bool ReadPayload(const VectorTriple& descriptor,std::vector<std::uint8_t>& bytes) {
    const auto size=static_cast<std::size_t>(descriptor.end-descriptor.begin);
    bytes.resize(size);
    return !size||Copy(reinterpret_cast<const void*>(descriptor.begin),0,bytes.data(),size);
}
bool ReadPayload(const TrackRecordMetadata& metadata,TrackPayload& payload) {
    return ReadPayload(metadata.positionKeys,payload.position)&&
           ReadPayload(metadata.rotationKeys,payload.rotation)&&
           ReadPayload(metadata.scaleKeys,payload.scale);
}
template<std::size_t N> KeyState ConstantKeys(const std::vector<std::uint8_t>& bytes,std::size_t stride,
                                              const std::array<float,N>& expected) {
    std::int16_t previousStart=std::numeric_limits<std::int16_t>::min();
    std::int16_t previousEnd=std::numeric_limits<std::int16_t>::min();
    bool first=true,different=false;
    for(std::size_t offset=0;offset<bytes.size();offset+=stride) {
        std::int16_t start=0,end=0;
        std::array<float,N> value{};
        std::memcpy(&start,bytes.data()+offset,sizeof start);
        std::memcpy(&end,bytes.data()+offset+2,sizeof end);
        std::memcpy(value.data(),bytes.data()+offset+4,sizeof value);
        if(start>end||(!first&&(start<previousStart||end<previousEnd))||!Finite(value))return KeyState::malformed;
        if(!NativeExportRoundoffStable(value,expected))different=true;
        previousStart=start;previousEnd=end;first=false;
    }
    return different?KeyState::dynamic:KeyState::constant;
}
void ObserveDescriptor(TrackVectorMetadata& observed,const VectorTriple& descriptor,std::uint32_t stride) {
    observed.begin=descriptor.begin;
    observed.end=descriptor.end;
    observed.capacity=descriptor.capacity;
    observed.stride=stride;
}
TrackDiagnosticState FinishDiagnostic(TrackDiagnostic& diagnostic,TrackDiagnosticState state,
                                      TrackDiagnosticReason reason) {
    diagnostic.state=state;
    diagnostic.reason=reason;
    return state;
}
TrackDiagnosticState SnapshotConstantTrack(std::uintptr_t wrapper,const LocalTransform& common,
                                           std::size_t& totalKeys,TrackDiagnostic& diagnostic) {
    diagnostic.consumer.wrapper=wrapper;
    std::uintptr_t recordBefore=0,recordAfter=0;
    if(!ReadAt(reinterpret_cast<const void*>(wrapper),0,recordBefore)||!recordBefore)
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::wrapperRecordUnreadable);
    diagnostic.consumer.record=recordBefore;
    TrackRecordMetadata first,second,third;
    if(!ReadTrackRecordMetadata(reinterpret_cast<const void*>(recordBefore),first))
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::recordMetadataUnreadable);
    ObserveDescriptor(diagnostic.consumer.positionKeys,first.positionKeys,16);
    ObserveDescriptor(diagnostic.consumer.quaternionKeys,first.rotationKeys,20);
    ObserveDescriptor(diagnostic.consumer.scaleKeys,first.scaleKeys,16);
    diagnostic.consumer.defaultMatrix=first.matrix;
    diagnostic.consumer.defaultScale=first.scaleDefault;
    diagnostic.consumer.defaultQuaternion=first.rotationDefault;
    diagnostic.consumer.defaultPosition=first.positionDefault;
    std::size_t positionCount=0,rotationCount=0,scaleCount=0;
    if(!ValidateDescriptor(first.positionKeys,16,positionCount))
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::positionDescriptorMalformed);
    diagnostic.consumer.positionKeys.count=static_cast<std::uint32_t>(positionCount);
    if(!ValidateDescriptor(first.rotationKeys,20,rotationCount))
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::quaternionDescriptorMalformed);
    diagnostic.consumer.quaternionKeys.count=static_cast<std::uint32_t>(rotationCount);
    if(!ValidateDescriptor(first.scaleKeys,16,scaleCount))
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::scaleDescriptorMalformed);
    diagnostic.consumer.scaleKeys.count=static_cast<std::uint32_t>(scaleCount);
    const auto keyCount=positionCount+rotationCount+scaleCount;
    if(totalKeys>kMaxTrackKeysPerModel||keyCount>kMaxTrackKeysPerModel-totalKeys)
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::modelKeyBudgetExceeded);
    TrackPayload firstPayload,secondPayload;
    if(!ReadPayload(first,firstPayload))
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::payloadUnreadable);
    if(!ReadTrackRecordMetadata(reinterpret_cast<const void*>(recordBefore),second)||!Same(first,second))
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::recordMetadataChanged);
    if(!ReadPayload(second,secondPayload))
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::payloadUnreadable);
    if(!ReadTrackRecordMetadata(reinterpret_cast<const void*>(recordBefore),third)||!Same(first,third))
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::recordMetadataChanged);
    if(firstPayload.position!=secondPayload.position||firstPayload.rotation!=secondPayload.rotation||
       firstPayload.scale!=secondPayload.scale)
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::payloadChanged);
    if(!ReadAt(reinterpret_cast<const void*>(wrapper),0,recordAfter)||recordAfter!=recordBefore)
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::wrapperRecordChanged);
    diagnostic.consumer.decodedKeyCount=static_cast<std::uint32_t>(keyCount);
    totalKeys+=keyCount;
    if(!Finite(common.matrix))
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::commonMatrixNonFinite);
    if(!Finite(common.scale))
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::commonScaleNonFinite);
    if(!Finite(common.rotation))
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::commonQuaternionNonFinite);
    if(!Finite(common.position))
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::commonPositionNonFinite);
    if(!NativeCloseQuaternion(common.rotation))
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::commonQuaternionOutsideClosePath);
    if(!Finite(first.scaleDefault))
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::recordScaleNonFinite);
    if(!Finite(first.rotationDefault))
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::recordQuaternionNonFinite);
    if(!Finite(first.positionDefault))
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::recordPositionNonFinite);
    if(!MatrixMatches(common.matrix,ComposeLocal(common)))
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::commonMatrixSqtMismatch);
    if(!NativeExportRoundoffStable(first.scaleDefault,common.scale))
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::dynamic,TrackDiagnosticReason::recordScaleDefaultMismatch);
    if(!NativeExportRoundoffStable(first.rotationDefault,common.rotation))
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::dynamic,TrackDiagnosticReason::recordQuaternionDefaultMismatch);
    if(!NativeExportRoundoffStable(first.positionDefault,common.position))
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::dynamic,TrackDiagnosticReason::recordPositionDefaultMismatch);
    if(!NativeExportRoundoffStable(first.matrix,common.matrix))
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::dynamic,TrackDiagnosticReason::recordMatrixDefaultMismatch);
    const auto positionState=ConstantKeys(firstPayload.position,16,common.position);
    const auto rotationState=ConstantKeys(firstPayload.rotation,20,common.rotation);
    const auto scaleState=ConstantKeys(firstPayload.scale,16,common.scale);
    if(positionState==KeyState::malformed)
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::positionKeysMalformed);
    if(rotationState==KeyState::malformed)
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::quaternionKeysMalformed);
    if(scaleState==KeyState::malformed)
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::scaleKeysMalformed);
    if(positionState==KeyState::dynamic)
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::dynamic,TrackDiagnosticReason::positionKeysDynamic);
    if(rotationState==KeyState::dynamic)
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::dynamic,TrackDiagnosticReason::quaternionKeysDynamic);
    if(scaleState==KeyState::dynamic)
        return FinishDiagnostic(diagnostic,TrackDiagnosticState::dynamic,TrackDiagnosticReason::scaleKeysDynamic);
    // With quaternion keys C23D70 ignores empty component defaults and emits
    // identity scale / zero translation.  Component wrappers use the defaults,
    // so both paths agree only for these native identity values.
    if(rotationCount) {
        const std::array<float,3> identityScale{{1.0f,1.0f,1.0f}};
        const std::array<float,3> zeroPosition{{0.0f,0.0f,0.0f}};
        if(!scaleCount&&!NativeExportRoundoffStable(common.scale,identityScale))
            return FinishDiagnostic(diagnostic,TrackDiagnosticState::dynamic,TrackDiagnosticReason::quaternionPathMissingScale);
        if(!positionCount&&!NativeExportRoundoffStable(common.position,zeroPosition))
            return FinishDiagnostic(diagnostic,TrackDiagnosticState::dynamic,TrackDiagnosticReason::quaternionPathMissingPosition);
    }
    return FinishDiagnostic(diagnostic,TrackDiagnosticState::exportStable,TrackDiagnosticReason::none);
}
ReadResult BuildFixedBindings(void* model,const std::vector<std::uintptr_t>& nodes,std::size_t clipCount,
                              const void* rigidMeshVtable,const void* nodeAnimateGuard,
                              std::vector<FixedBinding>& output) {
    const auto modelAddress=reinterpret_cast<std::uintptr_t>(model);
    std::map<std::uintptr_t,std::uint32_t> nodeIndex;
    for(std::uint32_t index=0;index<nodes.size();++index) {
        if(!nodes[index]||!nodeIndex.emplace(nodes[index],index).second)return ReadResult::malformed;
    }
    std::vector<FixedBinding> bindings(nodes.size());
    std::vector<std::uintptr_t> runtimeParents(nodes.size());
    std::vector<bool> baseValid(nodes.size(),false);
    std::size_t totalTrackSlots=0,totalTrackKeys=0;
    bool anyBoundTrack=false;
    for(std::uint32_t index=0;index<nodes.size();++index) {
        const auto node=reinterpret_cast<const void*>(nodes[index]);
        std::uintptr_t vtableBefore=0,ownerBefore=0,commonBefore=0,parentBefore=0,modifierBefore=0;
        std::uintptr_t vtableAfter=0,ownerAfter=0,commonAfter=0,parentAfter=0,modifierAfter=0;
        if(!ReadAt(node,0,vtableBefore)||!ReadAt(node,0x118,ownerBefore)||!ReadAt(node,0x11c,commonBefore)||
           !ReadAt(node,0x1b0,parentBefore)||!ReadAt(node,0x200,modifierBefore))return ReadResult::malformed;
        std::int32_t parentIndexBefore=-2,parentIndexAfter=-2;
        if(commonBefore&&!ReadAt(reinterpret_cast<const void*>(commonBefore),0xa0,parentIndexBefore))return ReadResult::malformed;
        std::vector<std::uintptr_t> tracks;
        VectorTriple trackTriple;
        const auto remaining=totalTrackSlots>=kMaxTrackSlotsPerModel?0:kMaxTrackSlotsPerModel-totalTrackSlots;
        const auto trackResult=SnapshotPointerVector(node,0x120,std::min(kMaxTrackSlotsPerNode,remaining),
                                                     kMaxTrackSlotsPerNode,tracks,trackTriple);
        if(trackResult!=ReadResult::ok)return trackResult;
        if(tracks.size()!=clipCount)return ReadResult::malformed;
        totalTrackSlots+=tracks.size();
        auto& binding=bindings[index];
        binding.index=index;
        binding.parentIndex=parentIndexBefore;
        binding.vtable=vtableBefore;
        binding.common=commonBefore;
        binding.trackBegin=trackTriple.begin;
        binding.trackEnd=trackTriple.end;
        binding.trackCapacity=trackTriple.capacity;
        binding.dynamicModifier=modifierBefore;
        const auto exactRigid=vtableBefore==reinterpret_cast<std::uintptr_t>(rigidMeshVtable);
        const auto inspectTracks=exactRigid&&!modifierBefore;
        LocalTransform commonFirst,commonSecond;
        const auto hasCommonSnapshot=inspectTracks&&commonBefore&&
            ReadLocalTransform(reinterpret_cast<const void*>(commonBefore),commonFirst);
        binding.trackDiagnostics.reserve(tracks.size());
        for(std::uint32_t slot=0;slot<tracks.size();++slot) {
            TrackDiagnostic diagnostic;
            diagnostic.slot=slot;
            diagnostic.consumer.wrapper=tracks[slot];
            if(!tracks[slot]) {
                FinishDiagnostic(diagnostic,TrackDiagnosticState::nullSlot,TrackDiagnosticReason::nullTrackSlot);
            } else {
                binding.nonNullTrackSlots.push_back(slot);
                if(ownerBefore==modelAddress&&commonBefore)anyBoundTrack=true;
                if(!exactRigid) {
                    FinishDiagnostic(diagnostic,TrackDiagnosticState::notInspected,TrackDiagnosticReason::nonRigidBoundary);
                } else if(modifierBefore) {
                    FinishDiagnostic(diagnostic,TrackDiagnosticState::notInspected,TrackDiagnosticReason::dynamicModifierBoundary);
                } else if(!hasCommonSnapshot) {
                    FinishDiagnostic(diagnostic,TrackDiagnosticState::unknown,TrackDiagnosticReason::commonTransformUnavailable);
                } else {
                    SnapshotConstantTrack(tracks[slot],commonFirst,totalTrackKeys,diagnostic);
                }
                if(diagnostic.state==TrackDiagnosticState::exportStable)binding.constantTrackSlots.push_back(slot);
                else if(diagnostic.state==TrackDiagnosticState::unknown)binding.unknownTrackSlots.push_back(slot);
            }
            binding.trackDiagnostics.push_back(std::move(diagnostic));
        }
        std::vector<std::uintptr_t> verifiedTracks;
        VectorTriple verifiedTrackTriple;
        const auto verifyTrackResult=SnapshotPointerVector(node,0x120,kMaxTrackSlotsPerNode,kMaxTrackSlotsPerNode,
                                                           verifiedTracks,verifiedTrackTriple);
        if(verifyTrackResult!=ReadResult::ok)return verifyTrackResult;
        if(!Same(trackTriple,verifiedTrackTriple)||tracks!=verifiedTracks)return ReadResult::malformed;
        if(hasCommonSnapshot&&(!ReadLocalTransform(reinterpret_cast<const void*>(commonBefore),commonSecond)||
                              !Same(commonFirst,commonSecond)))return ReadResult::malformed;
        if(!ReadAt(node,0,vtableAfter)||!ReadAt(node,0x118,ownerAfter)||!ReadAt(node,0x11c,commonAfter)||
           !ReadAt(node,0x1b0,parentAfter)||!ReadAt(node,0x200,modifierAfter))return ReadResult::malformed;
        if(commonAfter&&!ReadAt(reinterpret_cast<const void*>(commonAfter),0xa0,parentIndexAfter))return ReadResult::malformed;
        if(vtableBefore!=vtableAfter||ownerBefore!=ownerAfter||commonBefore!=commonAfter||
           parentBefore!=parentAfter||modifierBefore!=modifierAfter||parentIndexBefore!=parentIndexAfter)
            return ReadResult::malformed;
        baseValid[index]=ownerBefore==modelAddress&&commonBefore&&
                         SupportedNodeVtable(vtableBefore,rigidMeshVtable,nodeAnimateGuard);
        runtimeParents[index]=parentBefore;
    }
    for(std::uint32_t index=0;index<nodes.size();++index) {
        auto& binding=bindings[index];
        if(binding.parentIndex<-1||binding.parentIndex>=static_cast<std::int32_t>(nodes.size()))baseValid[index]=false;
        if(!runtimeParents[index])binding.runtimeParentIndex=-1;
        else {
            const auto found=nodeIndex.find(runtimeParents[index]);
            if(found==nodeIndex.end()) {binding.runtimeParentIndex=-2;baseValid[index]=false;}
            else binding.runtimeParentIndex=static_cast<std::int32_t>(found->second);
        }
        if(binding.parentIndex!=binding.runtimeParentIndex)baseValid[index]=false;
    }
    for(std::uint32_t index=0;index<nodes.size();++index) {
        std::vector<std::uint8_t> seen(nodes.size(),0);
        auto current=index;
        auto driver=std::numeric_limits<std::uint32_t>::max();
        auto root=std::numeric_limits<std::uint32_t>::max();
        bool chainValid=true;
        for(;;) {
            if(current>=nodes.size()||!baseValid[current]||seen[current]) {chainValid=false;break;}
            seen[current]=1;
            const auto& currentBinding=bindings[current];
            const auto rigid=currentBinding.vtable==reinterpret_cast<std::uintptr_t>(rigidMeshVtable);
            const auto allTracksExportStable=currentBinding.constantTrackSlots.size()==currentBinding.nonNullTrackSlots.size();
            if(driver==std::numeric_limits<std::uint32_t>::max()&&
               (!rigid||currentBinding.dynamicModifier||!allTracksExportStable))driver=current;
            if(currentBinding.parentIndex==-1) {root=current;break;}
            current=static_cast<std::uint32_t>(currentBinding.parentIndex);
        }
        auto& binding=bindings[index];
        binding.valid=chainValid;
        if(chainValid&&anyBoundTrack) {
            binding.group=driver!=std::numeric_limits<std::uint32_t>::max()
                ?FixedGroup(modelAddress,driver,false):FixedGroup(modelAddress,root,true);
        }
    }
    output=std::move(bindings);return ReadResult::ok;
}
std::uint64_t Fingerprint(const Mesh& mesh) {
    // Stable geometry identity, independent of allocation, pose and UV seams.
    // This is a cache compatibility check, not an authenticity/security digest.
    std::uint64_t hash=14695981039346656037ull;
    const auto word=[&hash](std::uint64_t v) {
        for(unsigned i=0;i<8;++i) {hash^=v&255;hash*=1099511628211ull;v>>=8;}
    };
    word(mesh.vertices.size());word(mesh.triangles.size());
    for(const auto v:mesh.vertices) for(double coordinate:{v.x,v.y,v.z}) {
        std::uint64_t bits=0;
        if(coordinate!=0)std::memcpy(&bits,&coordinate,sizeof bits);
        word(bits);
    }
    for(const auto& t:mesh.triangles)for(auto index:t.vertex)word(index);
    return hash?hash:1;
}
}
const char* TrackDiagnosticStateName(TrackDiagnosticState state) noexcept {
    switch(state) {
    case TrackDiagnosticState::nullSlot:return "null_slot";
    case TrackDiagnosticState::notInspected:return "not_inspected";
    case TrackDiagnosticState::exportStable:return "native_export_roundoff_stable";
    case TrackDiagnosticState::dynamic:return "dynamic";
    case TrackDiagnosticState::unknown:return "unknown";
    }
    return "unknown";
}
const char* TrackDiagnosticReasonName(TrackDiagnosticReason reason) noexcept {
    switch(reason) {
    case TrackDiagnosticReason::none:return "none";
    case TrackDiagnosticReason::nullTrackSlot:return "null_track_slot";
    case TrackDiagnosticReason::nonRigidBoundary:return "non_rigid_boundary";
    case TrackDiagnosticReason::dynamicModifierBoundary:return "dynamic_modifier_boundary";
    case TrackDiagnosticReason::commonTransformUnavailable:return "common_transform_unavailable";
    case TrackDiagnosticReason::wrapperRecordUnreadable:return "wrapper_record_unreadable";
    case TrackDiagnosticReason::recordMetadataUnreadable:return "record_metadata_unreadable";
    case TrackDiagnosticReason::positionDescriptorMalformed:return "position_descriptor_malformed";
    case TrackDiagnosticReason::quaternionDescriptorMalformed:return "quaternion_descriptor_malformed";
    case TrackDiagnosticReason::scaleDescriptorMalformed:return "scale_descriptor_malformed";
    case TrackDiagnosticReason::modelKeyBudgetExceeded:return "model_key_budget_exceeded";
    case TrackDiagnosticReason::payloadUnreadable:return "payload_unreadable";
    case TrackDiagnosticReason::recordMetadataChanged:return "record_metadata_changed";
    case TrackDiagnosticReason::payloadChanged:return "payload_changed";
    case TrackDiagnosticReason::wrapperRecordChanged:return "wrapper_record_changed";
    case TrackDiagnosticReason::commonMatrixNonFinite:return "common_matrix_non_finite";
    case TrackDiagnosticReason::commonScaleNonFinite:return "common_scale_non_finite";
    case TrackDiagnosticReason::commonQuaternionNonFinite:return "common_quaternion_non_finite";
    case TrackDiagnosticReason::commonPositionNonFinite:return "common_position_non_finite";
    case TrackDiagnosticReason::commonQuaternionOutsideClosePath:return "common_quaternion_outside_close_path";
    case TrackDiagnosticReason::recordScaleNonFinite:return "record_scale_non_finite";
    case TrackDiagnosticReason::recordQuaternionNonFinite:return "record_quaternion_non_finite";
    case TrackDiagnosticReason::recordPositionNonFinite:return "record_position_non_finite";
    case TrackDiagnosticReason::commonMatrixSqtMismatch:return "common_matrix_sqt_mismatch";
    case TrackDiagnosticReason::recordScaleDefaultMismatch:return "record_scale_default_mismatch";
    case TrackDiagnosticReason::recordQuaternionDefaultMismatch:return "record_quaternion_default_mismatch";
    case TrackDiagnosticReason::recordPositionDefaultMismatch:return "record_position_default_mismatch";
    case TrackDiagnosticReason::recordMatrixDefaultMismatch:return "record_matrix_default_mismatch";
    case TrackDiagnosticReason::positionKeysMalformed:return "position_keys_malformed";
    case TrackDiagnosticReason::quaternionKeysMalformed:return "quaternion_keys_malformed";
    case TrackDiagnosticReason::scaleKeysMalformed:return "scale_keys_malformed";
    case TrackDiagnosticReason::positionKeysDynamic:return "position_keys_dynamic";
    case TrackDiagnosticReason::quaternionKeysDynamic:return "quaternion_keys_dynamic";
    case TrackDiagnosticReason::scaleKeysDynamic:return "scale_keys_dynamic";
    case TrackDiagnosticReason::quaternionPathMissingScale:return "quaternion_path_missing_scale";
    case TrackDiagnosticReason::quaternionPathMissingPosition:return "quaternion_path_missing_position";
    }
    return "unknown";
}
bool GeometryReader::Bind(const SdgoApi* api,SdgoMod* mod) {
    modelVtable_=modelObsVtable_=rigidMeshVtable_=nodeParentGuard_=nodeAnimateGuard_=nullptr;world_=nullptr;
    if(!api || api->version!=SDGO_API_VERSION || !api->Symbol)return false;
    modelVtable_=api->Symbol(mod,"surface_decal.model_vtable");
    modelObsVtable_=api->Symbol(mod,"surface_decal.model_obs_vtable");
    rigidMeshVtable_=api->Symbol(mod,"surface_decal.rigid_mesh_vtable");
    world_=reinterpret_cast<World>(api->Symbol(mod,"surface_decal.node_world"));
    nodeParentGuard_=api->Symbol(mod,"surface_decal.node_parent");
    nodeAnimateGuard_=api->Symbol(mod,"surface_decal.node_animate");
    return modelVtable_ && rigidMeshVtable_ && world_;
}
ReadResult GeometryReader::ReadFixedBindings(void* model,std::vector<FixedBinding>& output) const {
    if(!modelVtable_||!rigidMeshVtable_||!nodeParentGuard_||!nodeAnimateGuard_)return ReadResult::unbound;
    const void* actual=nullptr;
    if(!ReadAt(model,0,actual)||!actual||(actual!=modelVtable_&&actual!=modelObsVtable_))return ReadResult::wrongModel;
    std::vector<std::uintptr_t> nodes;
    const auto nodeResult=SnapshotModelNodes(model,nodes);
    if(nodeResult!=ReadResult::ok)return nodeResult;
    std::vector<std::uintptr_t> clips;
    const auto clipResult=SnapshotModelClips(model,clips);
    if(clipResult!=ReadResult::ok)return clipResult;
    std::vector<FixedBinding> result;
    const auto bindingResult=BuildFixedBindings(model,nodes,clips.size(),rigidMeshVtable_,nodeAnimateGuard_,result);
    if(bindingResult!=ReadResult::ok)return bindingResult;
    std::vector<std::uintptr_t> verifiedNodes,verifiedClips;
    const auto verifyNodeResult=SnapshotModelNodes(model,verifiedNodes);
    if(verifyNodeResult!=ReadResult::ok)return verifyNodeResult;
    const auto verifyClipResult=SnapshotModelClips(model,verifiedClips);
    if(verifyClipResult!=ReadResult::ok)return verifyClipResult;
    if(nodes!=verifiedNodes||clips!=verifiedClips)return ReadResult::malformed;
    output=std::move(result);return ReadResult::ok;
}
bool GeometryReader::FixedGroupMatches(void* model,std::uint32_t node,std::uint64_t expectedGroup) const {
    if(!expectedGroup)return false;
    std::vector<FixedBinding> bindings;
    return ReadFixedBindings(model,bindings)==ReadResult::ok&&node<bindings.size()&&bindings[node].valid&&
           bindings[node].group==expectedGroup;
}
ReadResult GeometryReader::Read(void* model,std::vector<Node>& output) const {
    if(!modelVtable_ || !rigidMeshVtable_ || !world_)return ReadResult::unbound;
    const void* actual=nullptr;
    if(!ReadAt(model,0,actual) || !actual || (actual!=modelVtable_&&actual!=modelObsVtable_))return ReadResult::wrongModel;
    std::vector<std::uintptr_t> nodeAddresses;
    const auto nodeResult=SnapshotModelNodes(model,nodeAddresses);
    if(nodeResult!=ReadResult::ok)return nodeResult;
    std::vector<FixedBinding> fixedBindings;
    if(nodeParentGuard_&&nodeAnimateGuard_) {
        std::vector<std::uintptr_t> clips;
        std::vector<FixedBinding> candidate;
        if(SnapshotModelClips(model,clips)==ReadResult::ok&&
           BuildFixedBindings(model,nodeAddresses,clips.size(),rigidMeshVtable_,nodeAnimateGuard_,candidate)==ReadResult::ok) {
            std::vector<std::uintptr_t> verifiedNodes,verifiedClips;
            if(SnapshotModelNodes(model,verifiedNodes)==ReadResult::ok&&verifiedNodes==nodeAddresses&&
               SnapshotModelClips(model,verifiedClips)==ReadResult::ok&&verifiedClips==clips)
                fixedBindings=std::move(candidate);
        }
    }
    std::vector<void*> nodes;
    nodes.reserve(nodeAddresses.size());
    for(const auto address:nodeAddresses)nodes.push_back(reinterpret_cast<void*>(address));
    std::vector<Node> result;
    std::size_t totalVertices=0,totalTriangles=0;
    for(std::uint32_t index=0;index<nodes.size();++index) {
        void* owner=nullptr;void* common=nullptr;std::uint8_t hidden=0;
        if(!ReadAt(nodes[index],0,actual)||!ReadAt(nodes[index],0x118,owner)||owner!=model||
           !ReadAt(nodes[index],0x11c,common)||!ReadAt(nodes[index],0x108,hidden))return ReadResult::malformed;
        if(actual!=rigidMeshVtable_ || hidden)continue; // bones/hidden nodes are not paint surfaces
        std::uint32_t vertices=0,faces=0,kind=0,groupCount=0,vertexCapacity=0,indexCapacity=0;
        void* vertexData=nullptr;void* indexData=nullptr;void* groupData=nullptr;
        if(!ReadAt(common,0xa4,vertices)||!ReadAt(common,0xa8,faces)||!ReadAt(common,0x70,kind))return ReadResult::malformed;
        if(!vertices||!faces)continue;
        // Native collision/helper meshes may have kind 1 and real vertices,
        // but no material draw groups. They are not rendered paint surfaces
        // and must not reject the visible kind-2 nodes in the same model.
        if(!ReadAt(common,0x194,groupCount))return ReadResult::malformed;
        if(!groupCount)continue;
        if(kind!=2)return ReadResult::unavailable;
        if(vertices>65536 || faces>262144 || totalVertices+vertices>262144 || totalTriangles+faces>262144)
            return ReadResult::budgetExceeded;
        if(!ReadAt(common,0x34,vertexData)||!ReadAt(common,0x38,vertexCapacity)||
           !ReadAt(common,0x58,indexData)||!ReadAt(common,0x5c,indexCapacity)||
           !ReadAt(common,0x198,groupData)||
           vertexCapacity<vertices||indexCapacity<faces*3||!groupCount||groupCount>4096)return ReadResult::malformed;
        std::vector<NativeVertex> nativeVertices(vertices);
        std::vector<std::uint16_t> indices(faces*3);
        std::vector<Group> groups(groupCount);
        if(!Copy(vertexData,0,nativeVertices.data(),nativeVertices.size()*sizeof(NativeVertex))||
           !Copy(indexData,0,indices.data(),indices.size()*sizeof(std::uint16_t))||
           !Copy(groupData,0,groups.data(),groups.size()*sizeof(Group)))return ReadResult::malformed;
        Node node;node.index=index;
        if(index<fixedBindings.size()&&fixedBindings[index].valid)node.fixedGroup=fixedBindings[index].group;
        node.modelPoseValid=ModelPose(nodes[index],node.modelPose);
        if(!Matrix(nodes[index],world_,node.world))return ReadResult::malformed;
        for(float x:node.world)if(!std::isfinite(x))return ReadResult::malformed;
        node.mesh.vertices.reserve(vertices);
        std::map<std::tuple<float,float,float>,std::uint32_t> unique;
        std::vector<std::uint32_t> remap;
        remap.reserve(vertices);
        for(const auto& v:nativeVertices) {
            if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)||
               std::max({std::abs(v.x),std::abs(v.y),std::abs(v.z)})>1000000)return ReadResult::malformed;
            // Native unit meshes expand the same corner once per triangle.
            // Weld exact positions within this rigid node, independently of
            // texture seams. Non-manifold coincident edges remain blocked by
            // the projector instead of joining overlapping surface sheets.
            const auto found=unique.emplace(std::make_tuple(v.x,v.y,v.z),static_cast<std::uint32_t>(node.mesh.vertices.size()));
            if(found.second)node.mesh.vertices.push_back({v.x,v.y,v.z});
            remap.push_back(found.first->second);
        }
        std::vector<std::uint8_t> used(faces,0);
        for(std::uint32_t groupIndex=0;groupIndex<groups.size();++groupIndex) {
            const auto& group=groups[groupIndex];
            if(group.start%3||group.start>indices.size()||group.triangles>(indices.size()-group.start)/3)
                return ReadResult::malformed;
            for(std::uint32_t face=0;face<group.triangles;++face) {
                const auto offset=group.start+face*3;
                if(used[offset/3]++)return ReadResult::malformed;
                Triangle t{{indices[offset],indices[offset+1],indices[offset+2]}};
                for(auto v:t.vertex)if(v>=vertices)return ReadResult::malformed;
                // Existing native sticker meshes must not become surfaces for
                // another decal. Their textures remain owned by the native path.
                if(group.material>=7&&group.material<=9)continue;
                for(auto& v:t.vertex)v=remap[v];
                node.mesh.triangles.push_back(t);
                node.triangleGroups.push_back(groupIndex);
            }
        }
        totalVertices+=vertices;totalTriangles+=faces;
        if(node.mesh.triangles.empty())continue;
        node.geometry=Fingerprint(node.mesh);
        result.push_back(std::move(node));
    }
    if(result.empty())return ReadResult::unavailable;
    output=std::move(result);return ReadResult::ok;
}
} // namespace sdgo::surface_decal::native
