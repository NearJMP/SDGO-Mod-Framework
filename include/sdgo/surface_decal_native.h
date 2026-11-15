#pragma once
#include "sdgo/sdgo_api.h"
#include "sdgo/surface_decal.h"

namespace sdgo::surface_decal::native {

struct Node {
    std::uint32_t index = 0;
    std::uint64_t geometry = 0;
    std::uint64_t fixedGroup = 0;
    Mesh mesh;
    std::vector<std::uint32_t> triangleGroups;
    std::array<float,16> world{};
    std::array<float,16> modelPose{};
    bool modelPoseValid = false;
};
// Fixed absolute comparison ceiling for independently exported native M/S/Q/P and key values.
// It is never scaled by coordinate magnitude; descriptor and snapshot stability remain bit exact.
constexpr float kNativeExportRoundoffTolerance = 3.814697265625e-6f;
enum class TrackDiagnosticState : std::uint8_t {
    nullSlot,
    notInspected,
    exportStable,
    dynamic,
    unknown
};
enum class TrackDiagnosticReason : std::uint8_t {
    none,
    nullTrackSlot,
    nonRigidBoundary,
    dynamicModifierBoundary,
    commonTransformUnavailable,
    wrapperRecordUnreadable,
    recordMetadataUnreadable,
    positionDescriptorMalformed,
    quaternionDescriptorMalformed,
    scaleDescriptorMalformed,
    modelKeyBudgetExceeded,
    payloadUnreadable,
    recordMetadataChanged,
    payloadChanged,
    wrapperRecordChanged,
    commonMatrixNonFinite,
    commonScaleNonFinite,
    commonQuaternionNonFinite,
    commonPositionNonFinite,
    commonQuaternionOutsideClosePath,
    recordScaleNonFinite,
    recordQuaternionNonFinite,
    recordPositionNonFinite,
    commonMatrixSqtMismatch,
    recordScaleDefaultMismatch,
    recordQuaternionDefaultMismatch,
    recordPositionDefaultMismatch,
    recordMatrixDefaultMismatch,
    positionKeysMalformed,
    quaternionKeysMalformed,
    scaleKeysMalformed,
    positionKeysDynamic,
    quaternionKeysDynamic,
    scaleKeysDynamic,
    quaternionPathMissingScale,
    quaternionPathMissingPosition
};
struct TrackVectorMetadata {
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
    std::uintptr_t capacity = 0;
    std::uint32_t count = 0;
    std::uint32_t stride = 0;
};
struct TrackConsumerMetadata {
    std::uintptr_t wrapper = 0;
    std::uintptr_t record = 0;
    TrackVectorMetadata positionKeys;
    TrackVectorMetadata quaternionKeys;
    TrackVectorMetadata scaleKeys;
    std::uint32_t decodedKeyCount = 0;
    std::array<float,16> defaultMatrix{};
    std::array<float,3> defaultScale{};
    std::array<float,4> defaultQuaternion{};
    std::array<float,3> defaultPosition{};
};
struct TrackDiagnostic {
    std::uint32_t slot = 0;
    TrackDiagnosticState state = TrackDiagnosticState::nullSlot;
    TrackDiagnosticReason reason = TrackDiagnosticReason::nullTrackSlot;
    TrackConsumerMetadata consumer;
};
const char* TrackDiagnosticStateName(TrackDiagnosticState) noexcept;
const char* TrackDiagnosticReasonName(TrackDiagnosticReason) noexcept;

struct FixedBinding {
    std::uint32_t index = 0;
    std::int32_t parentIndex = -1;
    std::int32_t runtimeParentIndex = -1;
    bool valid = false;
    std::uintptr_t vtable = 0;
    std::uintptr_t common = 0;
    std::uintptr_t trackBegin = 0;
    std::uintptr_t trackEnd = 0;
    std::uintptr_t trackCapacity = 0;
    std::uintptr_t dynamicModifier = 0;
    std::vector<std::uint32_t> nonNullTrackSlots;
    // Slots stable within the fixed native export-roundoff bound; this is not
    // a claim that independently exported floats are bit-identical.
    std::vector<std::uint32_t> constantTrackSlots;
    std::vector<std::uint32_t> unknownTrackSlots;
    std::vector<TrackDiagnostic> trackDiagnostics;
    std::uint64_t group = 0;
};
enum class ReadResult { ok, unbound, wrongModel, malformed, unavailable, budgetExceeded };
class GeometryReader {
public:
    bool Bind(const SdgoApi*, SdgoMod*);
    // Call only during a native model's live, game-thread render/update scope.
    // Pointers and shared resource arrays are never retained or mutated.
    ReadResult Read(void* model, std::vector<Node>& output) const;
    // Complete one-call snapshot for reuse only inside the current native render
    // callback. Numeric pointer observations must not be cached across frames.
    ReadResult ReadFixedBindings(void* model, std::vector<FixedBinding>& output) const;
    bool FixedGroupMatches(void* model, std::uint32_t node, std::uint64_t expectedGroup) const;
private:
    const void* modelVtable_ = nullptr;
    const void* modelObsVtable_ = nullptr;
    const void* rigidMeshVtable_ = nullptr;
    const void* nodeParentGuard_ = nullptr;
    const void* nodeAnimateGuard_ = nullptr;
    using World = const float*(__thiscall*)(void*);
    World world_ = nullptr;
};
} // namespace sdgo::surface_decal::native
