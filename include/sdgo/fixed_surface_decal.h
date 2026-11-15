#pragma once

#include "sdgo/surface_decal_view.h"

#include <array>
#include <cstdint>
#include <vector>

namespace sdgo::surface_decal {

// fixedGroup must come from a validated native ownership relation. Equal
// transforms are not evidence that two model nodes are one rigid surface.
struct FixedPart {
    std::uint32_t node = 0;
    std::uint64_t geometry = 0;
    std::uint64_t fixedGroup = 0;
    const Mesh* mesh = nullptr;
    // Row-vector affine transform from this part's local coordinates into the
    // primary/anchor part's local coordinates. The primary transform is identity.
    Matrix toAnchor{};
};

struct PartPatch {
    std::uint32_t node = 0;
    std::uint64_t geometry = 0;
    Patch patch;
};

// Extra renderer guard for reusing part-local decal vertices after native fixed
// ownership was established elsewhere. This only bounds per-frame pose drift
// for cached part-local decal vertices; it must not infer fixed groups or
// override trajectory classification. Unknown or independently driven nodes
// remain separate.
bool FixedPoseWithinDisplacement(const std::array<double, 3>& maxAbsCoordinates,
                                 const Matrix& reference, const Matrix& current,
                                 double maxDisplacement);

// Projects one wrapped decal across adjacent meshes in the primary part's
// non-zero fixed group. Group zero and different groups stay on the primary.
// Invalid, disconnected or over-budget secondary parts are excluded; invalid
// primary input fails and leaves output unchanged. Each returned patch uses its
// original part-local positions and original mesh triangle indices.
Result ProjectFixedWrapped(const std::vector<FixedPart>& parts,
                           const Placement& primary,
                           std::vector<PartPatch>& output,
                           const Limits& limits = {});

} // namespace sdgo::surface_decal
