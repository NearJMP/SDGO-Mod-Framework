#include "sdgo/fixed_surface_decal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace sdgo::surface_decal {
namespace {
constexpr double identityEpsilon = 1e-9;
constexpr double weldTolerance = 1e-5;

bool Finite(Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Result ValidateMesh(const Mesh& mesh, const Limits& limits) {
    if (mesh.vertices.empty() || mesh.triangles.empty()) return Result::invalidMesh;
    if (mesh.vertices.size() > limits.vertices || mesh.triangles.size() > limits.triangles)
        return Result::budgetExceeded;
    for (const auto vertex : mesh.vertices) {
        if (!Finite(vertex) ||
            std::max({std::abs(vertex.x), std::abs(vertex.y), std::abs(vertex.z)}) > limits.maxCoordinate)
            return Result::invalidMesh;
    }
    for (const auto& triangle : mesh.triangles)
        for (const auto vertex : triangle.vertex)
            if (vertex >= mesh.vertices.size()) return Result::invalidMesh;
    return Result::ok;
}

bool Identity(const Matrix& matrix) {
    for (std::size_t i = 0; i < matrix.size(); ++i) {
        if (!std::isfinite(matrix[i])) return false;
        const double expected = i == 0 || i == 5 || i == 10 || i == 15 ? 1.0 : 0.0;
        if (std::abs(matrix[i] - expected) > identityEpsilon) return false;
    }
    return true;
}

bool Affine(const Matrix& matrix) {
    for (const auto value : matrix) if (!std::isfinite(value)) return false;
    if (std::abs(matrix[3]) > identityEpsilon || std::abs(matrix[7]) > identityEpsilon ||
        std::abs(matrix[11]) > identityEpsilon || std::abs(matrix[15] - 1) > identityEpsilon)
        return false;
    const double determinant =
        matrix[0] * (matrix[5] * matrix[10] - matrix[6] * matrix[9]) -
        matrix[1] * (matrix[4] * matrix[10] - matrix[6] * matrix[8]) +
        matrix[2] * (matrix[4] * matrix[9] - matrix[5] * matrix[8]);
    return std::isfinite(determinant) && std::abs(determinant) > 1e-12;
}

struct Cell {
    std::int64_t x = 0, y = 0, z = 0;
    bool operator<(const Cell& other) const {
        return std::tie(x, y, z) < std::tie(other.x, other.y, other.z);
    }
};

Cell GridCell(Vec3 position) {
    return {
        static_cast<std::int64_t>(std::floor(position.x / weldTolerance)),
        static_cast<std::int64_t>(std::floor(position.y / weldTolerance)),
        static_cast<std::int64_t>(std::floor(position.z / weldTolerance))
    };
}

double DistanceSquared(Vec3 a, Vec3 b) {
    const double x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return x * x + y * y + z * z;
}

struct TriangleOwner {
    std::size_t part = 0;
    std::uint32_t triangle = 0;
    bool synthetic = false;
};

using Edge = std::pair<std::uint32_t, std::uint32_t>;
Edge OrderedEdge(std::uint32_t a, std::uint32_t b) { return {std::min(a,b),std::max(a,b)}; }

struct CombinedMesh {
    Mesh mesh;
    std::vector<std::size_t> vertexPart;
    std::vector<TriangleOwner> triangleOwner;
    std::map<Cell, std::vector<std::uint32_t>> grid;
    std::map<Edge, std::vector<std::uint32_t>> edges;
};

std::uint32_t AddVertex(CombinedMesh& combined, Vec3 position, std::size_t part) {
    const auto cell = GridCell(position);
    std::uint32_t match = std::numeric_limits<std::uint32_t>::max();
    double best = weldTolerance * weldTolerance;
    bool ambiguous = false;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            for (int z = -1; z <= 1; ++z) {
                const Cell neighbor{cell.x + x, cell.y + y, cell.z + z};
                const auto found = combined.grid.find(neighbor);
                if (found == combined.grid.end()) continue;
                for (const auto candidate : found->second) {
                    if (combined.vertexPart[candidate] == part) continue;
                    const double distance = DistanceSquared(position, combined.mesh.vertices[candidate]);
                    if (distance > weldTolerance * weldTolerance) continue;
                    if (match != std::numeric_limits<std::uint32_t>::max() && candidate != match)
                        ambiguous = true;
                    if (match == std::numeric_limits<std::uint32_t>::max() || distance < best) {
                        match = candidate;
                        best = distance;
                    }
                }
            }
    // A point near multiple distinct representatives is a junction/ambiguous
    // weld. Keep it separate so no cross-part edge can be invented there.
    if (match != std::numeric_limits<std::uint32_t>::max() && !ambiguous) return match;
    const auto index = static_cast<std::uint32_t>(combined.mesh.vertices.size());
    combined.mesh.vertices.push_back(position);
    combined.vertexPart.push_back(part);
    combined.grid[cell].push_back(index);
    return index;
}

double Dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vec3 Mul(Vec3 a, double scale) { return {a.x*scale,a.y*scale,a.z*scale}; }

bool Degenerate(const CombinedMesh& combined, const Triangle& triangle) {
    const auto a=combined.mesh.vertices[triangle.vertex[0]];
    const auto ab=Sub(combined.mesh.vertices[triangle.vertex[1]],a);
    const auto ac=Sub(combined.mesh.vertices[triangle.vertex[2]],a);
    const Vec3 cross{ab.y*ac.z-ab.z*ac.y,ab.z*ac.x-ab.x*ac.z,ab.x*ac.y-ab.y*ac.x};
    return !std::isfinite(Dot(cross,cross)) || Dot(cross,cross)<1e-24;
}

std::uint32_t Third(const Triangle& triangle, std::uint32_t a, std::uint32_t b) {
    for(const auto vertex:triangle.vertex)if(vertex!=a&&vertex!=b)return vertex;
    return std::numeric_limits<std::uint32_t>::max();
}

bool OverlapsAcrossPart(const CombinedMesh& combined, const Triangle& candidate,
                        std::size_t partIndex, Edge& blocker) {
    bool overlap=false;std::size_t bestOwners=0,bestAdjacency=0;
    for(std::size_t edge=0;edge<3;++edge) {
        const auto a=candidate.vertex[edge],b=candidate.vertex[(edge+1)%3];
        const auto found=combined.edges.find(OrderedEdge(a,b));
        if(found==combined.edges.end())continue;
        for(const auto triangleIndex:found->second) {
            if(combined.triangleOwner[triangleIndex].part==partIndex)continue;
            const auto& other=combined.mesh.triangles[triangleIndex];
            const auto c=Third(candidate,a,b),d=Third(other,a,b);
            bool sameSide=false;
            if(c==std::numeric_limits<std::uint32_t>::max()||d==std::numeric_limits<std::uint32_t>::max()) {
                sameSide=true;
            } else {
                const auto A=combined.mesh.vertices[a],edgeVector=Sub(combined.mesh.vertices[b],A);
                const double edgeLength=Dot(edgeVector,edgeVector);
                if(edgeLength<1e-24) {
                    sameSide=true;
                } else {
                    const auto radial=[&](std::uint32_t vertex) {
                        const auto delta=Sub(combined.mesh.vertices[vertex],A);
                        return Sub(delta,Mul(edgeVector,Dot(delta,edgeVector)/edgeLength));
                    };
                    const auto first=radial(c),second=radial(d);
                    const double firstLength=Dot(first,first),secondLength=Dot(second,second);
                    sameSide=firstLength<1e-24||secondLength<1e-24||
                        Dot(first,second)/std::sqrt(firstLength*secondLength)>.999999;
                }
            }
            std::set<std::size_t> owners;
            for(const auto adjacent:found->second)if(combined.triangleOwner[adjacent].part!=partIndex)owners.insert(combined.triangleOwner[adjacent].part);
            if(sameSide&&(!overlap||owners.size()>bestOwners||
               (owners.size()==bestOwners&&found->second.size()>bestAdjacency))) {
                overlap=true;bestOwners=owners.size();bestAdjacency=found->second.size();blocker={a,b};
            }
        }
    }
    return overlap;
}

void AddTriangle(CombinedMesh& combined, Triangle triangle, TriangleOwner owner) {
    const auto index=static_cast<std::uint32_t>(combined.mesh.triangles.size());
    combined.mesh.triangles.push_back(triangle);
    combined.triangleOwner.push_back(owner);
    for(std::size_t edge=0;edge<3;++edge)
        combined.edges[OrderedEdge(triangle.vertex[edge],triangle.vertex[(edge+1)%3])].push_back(index);
}

bool AddPart(CombinedMesh& combined, const FixedPart& part, std::size_t partIndex,
             const Limits& limits, bool primary) {
    if (!part.mesh || !part.geometry || (primary ? !Identity(part.toAnchor) : !Affine(part.toAnchor)))
        return false;
    if (ValidateMesh(*part.mesh, limits) != Result::ok) return false;
    if (combined.mesh.vertices.size() + part.mesh->vertices.size() > limits.vertices ||
        combined.mesh.triangles.size() + part.mesh->triangles.size() > limits.triangles)
        return false;

    std::vector<Vec3> transformed;
    transformed.reserve(part.mesh->vertices.size());
    for (const auto vertex : part.mesh->vertices) {
        Vec3 anchor;
        if (!Transform(vertex, part.toAnchor, anchor) ||
            std::max({std::abs(anchor.x), std::abs(anchor.y), std::abs(anchor.z)}) > limits.maxCoordinate)
            return false;
        transformed.push_back(anchor);
    }

    std::vector<std::uint32_t> remap;
    remap.reserve(transformed.size());
    for (const auto vertex : transformed) remap.push_back(AddVertex(combined, vertex, partIndex));
    for (std::uint32_t triangleIndex = 0; triangleIndex < part.mesh->triangles.size(); ++triangleIndex) {
        const auto& source = part.mesh->triangles[triangleIndex];
        Triangle triangle{{remap[source.vertex[0]],remap[source.vertex[1]],remap[source.vertex[2]]}};
        if(!primary&&Degenerate(combined,triangle))continue;
        Edge blocker{};
        if(!primary&&OverlapsAcrossPart(combined,triangle,partIndex,blocker)) {
            // A coplanar/overlapping face is not a continuation. A degenerate
            // blocker makes the coincident edge non-manifold to ProjectWrapped
            // without producing geometry for the rejected secondary triangle.
            AddTriangle(combined,{{blocker.first,blocker.second,blocker.first}},{partIndex,triangleIndex,true});
            continue;
        }
        AddTriangle(combined,triangle,{partIndex,triangleIndex});
    }
    return true;
}

Vec3 LocalPosition(const Mesh& mesh, std::uint32_t triangle,
                   const std::array<double, 3>& barycentric) {
    const auto& indices = mesh.triangles[triangle].vertex;
    Vec3 result{};
    for (std::size_t i = 0; i < 3; ++i) {
        result.x += mesh.vertices[indices[i]].x * barycentric[i];
        result.y += mesh.vertices[indices[i]].y * barycentric[i];
        result.z += mesh.vertices[indices[i]].z * barycentric[i];
    }
    return result;
}
} // namespace

bool FixedPoseWithinDisplacement(const std::array<double, 3>& maxAbsCoordinates,
                                 const Matrix& reference, const Matrix& current,
                                 double maxDisplacement) {
    for (const auto coordinate : maxAbsCoordinates)
        if (!std::isfinite(coordinate) || coordinate < 0) return false;
    if (!std::isfinite(maxDisplacement) || maxDisplacement < 0) return false;

    const auto strictAffine = [](const Matrix& matrix) {
        if (!Affine(matrix)) return false;
        return matrix[3] == 0 && matrix[7] == 0 && matrix[11] == 0 &&
               matrix[15] == 1;
    };
    if (!strictAffine(reference) || !strictAffine(current)) return false;

    std::array<double, 3> bounds{};
    for (std::size_t output = 0; output < bounds.size(); ++output) {
        bounds[output] = std::abs(current[12 + output] - reference[12 + output]);
        for (std::size_t input = 0; input < maxAbsCoordinates.size(); ++input)
            bounds[output] += maxAbsCoordinates[input] *
                              std::abs(current[input * 4 + output] -
                                       reference[input * 4 + output]);
        if (!std::isfinite(bounds[output])) return false;
    }
    return std::hypot(bounds[0], bounds[1], bounds[2]) <= maxDisplacement;
}

Result ProjectFixedWrapped(const std::vector<FixedPart>& parts,
                           const Placement& primary,
                           std::vector<PartPatch>& output,
                           const Limits& limits) {
    if (!Valid(primary, limits)) return Result::invalidPlacement;

    std::size_t primaryIndex = parts.size();
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].node != primary.anchor.node || parts[i].geometry != primary.anchor.geometry) continue;
        if (primaryIndex != parts.size()) return Result::invalidPlacement;
        primaryIndex = i;
    }
    if (primaryIndex == parts.size()) return Result::wrongGeometry;
    const auto& primaryPart = parts[primaryIndex];
    if (!primaryPart.mesh || !Identity(primaryPart.toAnchor)) return Result::invalidPlacement;
    const auto primaryMeshResult = ValidateMesh(*primaryPart.mesh, limits);
    if (primaryMeshResult != Result::ok) return primaryMeshResult;
    if (primary.anchor.triangle >= primaryPart.mesh->triangles.size()) return Result::invalidPlacement;

    CombinedMesh combined;
    if (!AddPart(combined, primaryPart, primaryIndex, limits, true)) return Result::invalidPlacement;

    if (primaryPart.fixedGroup) {
        std::map<std::uint32_t,std::size_t> nodeCounts;
        for(const auto& part:parts)if(part.fixedGroup==primaryPart.fixedGroup)++nodeCounts[part.node];
        if(nodeCounts[primaryPart.node]!=1)return Result::invalidPlacement;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i == primaryIndex || parts[i].fixedGroup != primaryPart.fixedGroup ||
                nodeCounts[parts[i].node] != 1) continue;
            // Secondary parts are fail-closed individually. A malformed, remote
            // or too-large fragment cannot suppress the valid primary patch.
            AddPart(combined, parts[i], i, limits, false);
        }
    }

    Patch projected;
    const auto result = ProjectWrapped(combined.mesh, primary.anchor.geometry,
                                       primary.anchor.node, primary, projected, limits);
    if (result != Result::ok) return result;

    std::map<std::size_t, std::size_t> destinations;
    std::vector<PartPatch> patches;
    for (std::size_t offset = 0; offset < projected.vertices.size(); offset += 3) {
        const auto combinedTriangle = projected.vertices[offset].triangle;
        if (combinedTriangle >= combined.triangleOwner.size()) return Result::invalidMesh;
        const auto owner = combined.triangleOwner[combinedTriangle];
        if (owner.synthetic) return Result::invalidMesh;
        for (std::size_t corner = 1; corner < 3; ++corner)
            if (projected.vertices[offset + corner].triangle != combinedTriangle)
                return Result::invalidMesh;
        if (!parts[owner.part].mesh || owner.triangle >= parts[owner.part].mesh->triangles.size())
            return Result::invalidMesh;
        auto found = destinations.find(owner.part);
        if (found == destinations.end()) {
            const auto destination = patches.size();
            destinations.emplace(owner.part, destination);
            patches.push_back({parts[owner.part].node, parts[owner.part].geometry, {}});
            found = destinations.find(owner.part);
        }
        auto& patch = patches[found->second].patch;
        for (std::size_t corner = 0; corner < 3; ++corner) {
            auto vertex = projected.vertices[offset + corner];
            vertex.triangle = owner.triangle;
            vertex.position = LocalPosition(*parts[owner.part].mesh, owner.triangle,
                                            vertex.barycentric);
            patch.vertices.push_back(vertex);
        }
    }
    if (patches.empty()) return Result::empty;
    output = std::move(patches);
    return Result::ok;
}

} // namespace sdgo::surface_decal
