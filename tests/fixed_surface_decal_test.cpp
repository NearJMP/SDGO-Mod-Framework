#include "sdgo/fixed_surface_decal.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace sdgo::surface_decal;
namespace {
int checks = 0;
void Check(bool value, const char* message) {
    ++checks;
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
bool Near(double a, double b, double epsilon = 1e-7) { return std::abs(a - b) <= epsilon; }
Matrix IdentityMatrix() {
    Matrix result{};
    result[0] = result[5] = result[10] = result[15] = 1;
    return result;
}
Matrix SideToAnchor(double seamError = 0) {
    // Row-vector transform: (x,y,z) -> (z+error,y,-x).
    Matrix result{};
    result[2] = -1;
    result[5] = 1;
    result[8] = 1;
    result[12] = seamError;
    result[15] = 1;
    return result;
}
Mesh PrimaryMesh() {
    return {{{-2,-1,0},{0,-1,0},{0,1,0},{-2,1,0}},
            {{{{0,1,2}}},{{{0,2,3}}}}};
}
Mesh SideMesh() {
    return {{{0,-1,0},{2,-1,0},{2,1,0},{0,1,0}},
            {{{{0,1,2}}},{{{0,2,3}}}}};
}
Placement PlacementAtSeam(const Mesh& mesh) {
    Placement placement;
    Check(MakePlacement(mesh, {0,{{.125,.375,.5}},0}, 77, 4, {1,0,0},
                        2, 1, .05, placement) == Result::ok,
          "make primary placement beside a fixed-part seam");
    return placement;
}
double Area(const Patch& patch) {
    double total = 0;
    for (std::size_t i = 0; i < patch.vertices.size(); i += 3) {
        const auto a = patch.vertices[i].position;
        const auto b = patch.vertices[i+1].position;
        const auto c = patch.vertices[i+2].position;
        const Vec3 ab{b.x-a.x,b.y-a.y,b.z-a.z}, ac{c.x-a.x,c.y-a.y,c.z-a.z};
        const Vec3 cross{ab.y*ac.z-ab.z*ac.y,ab.z*ac.x-ab.x*ac.z,ab.x*ac.y-ab.y*ac.x};
        total += std::hypot(cross.x,cross.y,cross.z)/2;
    }
    return total;
}
const PartPatch* Find(const std::vector<PartPatch>& patches, std::uint32_t node) {
    for (const auto& patch : patches) if (patch.node == node) return &patch;
    return nullptr;
}
void ValidateLocal(const Mesh& mesh, const PartPatch& part) {
    Check(!part.patch.vertices.empty() && part.patch.vertices.size()%3 == 0,
          "part patch is a triangle list");
    for (const auto& vertex : part.patch.vertices) {
        Check(vertex.triangle < mesh.triangles.size(),
              "triangle number maps back to the original part mesh");
        const auto& triangle = mesh.triangles[vertex.triangle].vertex;
        Vec3 reconstructed{}; double sum = 0;
        for (std::size_t i = 0; i < 3; ++i) {
            reconstructed.x += mesh.vertices[triangle[i]].x * vertex.barycentric[i];
            reconstructed.y += mesh.vertices[triangle[i]].y * vertex.barycentric[i];
            reconstructed.z += mesh.vertices[triangle[i]].z * vertex.barycentric[i];
            sum += vertex.barycentric[i];
        }
        Check(Near(sum,1) && Near(reconstructed.x,vertex.position.x) &&
              Near(reconstructed.y,vertex.position.y) && Near(reconstructed.z,vertex.position.z),
              "output position and barycentric coordinates are part-local");
        Check(vertex.uv.x >= 0 && vertex.uv.x <= 1 &&
              vertex.uv.y >= 0 && vertex.uv.y <= 1,
              "fixed-part UV stays inside the decal");
    }
}

void FixedCorner() {
    const auto primaryMesh = PrimaryMesh();
    const auto sideMesh = SideMesh();
    const auto placement = PlacementAtSeam(primaryMesh);
    std::vector<FixedPart> parts{
        {4,77,100,&primaryMesh,IdentityMatrix()},
        {5,88,100,&sideMesh,SideToAnchor(5e-6)}
    };
    std::vector<PartPatch> output;
    Check(ProjectFixedWrapped(parts,placement,output)==Result::ok,
          "fixed armor panels share one wrapped decal across a tolerant seam weld");
    const auto* primary = Find(output,4); const auto* side = Find(output,5);
    Check(primary && side && primary->geometry==77 && side->geometry==88,
          "each patch preserves its native node and geometry fingerprint");
    ValidateLocal(primaryMesh,*primary); ValidateLocal(sideMesh,*side);
    Check(Near(Area(primary->patch)+Area(side->patch),2,2e-5),
          "right-angle fixed panels preserve the decal's physical area");
    std::vector<double> continuousSeam;
    for (const auto& a : primary->patch.vertices) if (Near(a.position.x,0,2e-5))
        for (const auto& b : side->patch.vertices) if (Near(b.position.x,0,2e-5) && Near(a.position.y,b.position.y,2e-5) &&
            Near(a.uv.x,b.uv.x,2e-5) && Near(a.uv.y,b.uv.y,2e-5)) {
            bool duplicate=false;for(const auto y:continuousSeam)duplicate=duplicate||Near(y,a.position.y,2e-5);
            if(!duplicate)continuousSeam.push_back(a.position.y);
        }
    Check(continuousSeam.size()>=2,"UV coordinates are continuous along the cross-mesh seam");
    bool localSide = false;
    for (const auto& vertex : side->patch.vertices)
        localSide = localSide || (vertex.position.x > .1 && Near(vertex.position.z,0));
    Check(localSide,"non-identity fixed-part output returns to side-part local coordinates");
}

void GroupAndConnectivityBoundaries() {
    const auto primaryMesh=PrimaryMesh(); const auto sideMesh=SideMesh();
    const auto placement=PlacementAtSeam(primaryMesh);
    const auto run=[&](std::vector<FixedPart> parts) {
        std::vector<PartPatch> output;
        Check(ProjectFixedWrapped(parts,placement,output)==Result::ok,
              "boundary case retains the primary projection");
        Check(Find(output,4) && !Find(output,5),
              "boundary case cannot cross into the secondary part");
    };
    run({{4,77,10,&primaryMesh,IdentityMatrix()},{5,88,11,&sideMesh,IdentityMatrix()}});
    run({{4,77,0,&primaryMesh,IdentityMatrix()},{5,88,0,&sideMesh,IdentityMatrix()}});
    auto far=SideToAnchor();far[12]=10;
    run({{4,77,10,&primaryMesh,IdentityMatrix()},{5,88,10,&sideMesh,far}});

    run({{4,77,10,&primaryMesh,IdentityMatrix()},{5,88,10,&primaryMesh,IdentityMatrix()}});
    Mesh sameSide{{{0,-1,0},{0,1,0},{-1,0,0}},{{{{0,1,2}}}}};
    run({{4,77,10,&primaryMesh,IdentityMatrix()},{5,88,10,&sameSide,IdentityMatrix()}});
    run({{4,77,10,&primaryMesh,IdentityMatrix()},
         {5,88,10,&sideMesh,SideToAnchor()},{5,89,10,&sideMesh,SideToAnchor()}});

    std::vector<FixedPart> ambiguous{
        {4,77,10,&primaryMesh,IdentityMatrix()},
        {5,88,10,&sideMesh,SideToAnchor()},
        {6,99,10,&sideMesh,SideToAnchor()}
    };
    std::vector<PartPatch> output;
    Check(ProjectFixedWrapped(ambiguous,placement,output)==Result::ok &&
          Find(output,4) && !Find(output,5) && !Find(output,6),
          "non-manifold cross-part seam refuses every ambiguous continuation");

    auto invalidSide=sideMesh; invalidSide.vertices[0].x=std::numeric_limits<double>::infinity();
    run({{4,77,10,&primaryMesh,IdentityMatrix()},{5,88,10,&invalidSide,SideToAnchor()}});
    Mesh degenerateSide{{{20,0,0},{21,0,0},{22,0,0}},{{{{0,1,2}}}}};
    run({{4,77,10,&primaryMesh,IdentityMatrix()},{5,88,10,&degenerateSide,IdentityMatrix()}});
}

void InvalidAndBudgets() {
    const auto primaryMesh=PrimaryMesh(); const auto placement=PlacementAtSeam(primaryMesh);
    std::vector<FixedPart> parts{{4,77,10,&primaryMesh,IdentityMatrix()}};
    std::vector<PartPatch> output(1);
    output[0].node=123; output[0].geometry=456;
    output[0].patch.vertices.push_back({{1,2,3},{.2,.3},0,{{1,0,0}}});
    const auto unchanged=output.front().node;

    auto moved=parts; moved[0].toAnchor[12]=1e-4;
    Check(ProjectFixedWrapped(moved,placement,output)==Result::invalidPlacement && output.front().node==unchanged,
          "primary transform must be finite identity and failure preserves output");

    auto nonfinite=primaryMesh;nonfinite.vertices[0].z=std::numeric_limits<double>::quiet_NaN();
    std::vector<FixedPart> bad{{4,77,10,&nonfinite,IdentityMatrix()}};
    Check(ProjectFixedWrapped(bad,placement,output)==Result::invalidMesh && output.front().node==unchanged,
          "non-finite primary mesh is rejected without changing output");

    Limits limits;limits.vertices=3;
    Check(ProjectFixedWrapped(parts,placement,output,limits)==Result::budgetExceeded && output.front().node==unchanged,
          "input vertex budget is enforced without changing output");
    limits={};limits.outputTriangles=1;
    Check(ProjectFixedWrapped(parts,placement,output,limits)==Result::budgetExceeded && output.front().node==unchanged,
          "output triangle budget is enforced without changing output");

    const auto sideMesh=SideMesh();
    std::vector<FixedPart> secondaryBudget{{4,77,10,&primaryMesh,IdentityMatrix()},
                                           {5,88,10,&sideMesh,SideToAnchor()}};
    limits={};limits.vertices=4;std::vector<PartPatch> primaryOnly;
    Check(ProjectFixedWrapped(secondaryBudget,placement,primaryOnly,limits)==Result::ok &&
          Find(primaryOnly,4) && !Find(primaryOnly,5),
          "over-budget secondary part is excluded without suppressing the primary patch");

    Mesh degenerate{{{0,0,0},{1,0,0},{2,0,0}},{{{{0,1,2}}}}};
    auto invalidPlacement=placement;invalidPlacement.anchor.triangle=0;
    std::vector<FixedPart> flat{{4,77,10,&degenerate,IdentityMatrix()}};
    Check(ProjectFixedWrapped(flat,invalidPlacement,output)==Result::invalidPlacement && output.front().node==unchanged,
          "degenerate primary anchor is rejected without changing output");

    auto noPrimary=parts;noPrimary[0].geometry=88;
    Check(ProjectFixedWrapped(noPrimary,placement,output)==Result::wrongGeometry && output.front().node==unchanged,
          "placement cannot borrow another geometry identity");
    auto duplicateNode=parts;duplicateNode.push_back({4,88,10,&primaryMesh,IdentityMatrix()});
    Check(ProjectFixedWrapped(duplicateNode,placement,output)==Result::invalidPlacement && output.front().node==unchanged,
          "primary node identity must be unique inside its fixed group");
}

void FixedPoseGuard() {
    const auto reference = IdentityMatrix();
    Check(FixedPoseWithinDisplacement({2,3,4},reference,reference,0),
          "an unchanged affine pose stays within a zero displacement budget");

    auto roundoff = reference;
    roundoff[0] += 1e-10;
    roundoff[12] = 2e-8; roundoff[13] = -3e-8; roundoff[14] = 1e-8;
    Check(FixedPoseWithinDisplacement({2,3,4},reference,roundoff,1e-6),
          "sub-micrometre frame roundoff keeps cached local decal vertices stable");

    auto translated = reference;
    translated[12] = .03; translated[13] = .04;
    Check(!FixedPoseWithinDisplacement({2,3,4},reference,translated,.049),
          "a five-centimetre pose translation exceeds a smaller cache budget");

    auto rotated = reference;
    const double angle = .001;
    rotated[0] = std::cos(angle); rotated[1] = std::sin(angle);
    rotated[4] = -std::sin(angle); rotated[5] = std::cos(angle);
    Check(!FixedPoseWithinDisplacement({1000,1,1},reference,rotated,.5),
          "a small rotation moves a distant armor vertex beyond the cache budget");

    auto scaled = reference; scaled[0] = 1.000001;
    Check(!FixedPoseWithinDisplacement({100000,1,1},reference,scaled,.05),
          "a slight scale change moves a large-coordinate vertex too far");

    auto perspective = reference; perspective[3] = 1e-12;
    Check(!FixedPoseWithinDisplacement({2,3,4},reference,perspective,1),
          "even tiny perspective terms are not accepted as fixed affine poses");
    auto singular = reference; singular[0] = 0;
    Check(!FixedPoseWithinDisplacement({2,3,4},reference,singular,1),
          "a singular affine matrix is not a reusable fixed pose");

    auto nonfinite = reference; nonfinite[5] = std::numeric_limits<double>::quiet_NaN();
    Check(!FixedPoseWithinDisplacement({2,3,4},reference,nonfinite,1),
          "a non-finite pose is rejected");
    Check(!FixedPoseWithinDisplacement({2,std::numeric_limits<double>::quiet_NaN(),4},
                                       reference,reference,1),
          "NaN local coordinate bounds are rejected");
    Check(!FixedPoseWithinDisplacement({2,std::numeric_limits<double>::infinity(),4},
                                       reference,reference,1),
          "infinite local coordinate bounds are rejected");
    Check(!FixedPoseWithinDisplacement({2,-3,4},reference,reference,1),
          "negative local coordinate bounds are rejected");
    Check(!FixedPoseWithinDisplacement({2,3,4},reference,reference,-1),
          "a negative displacement budget is rejected");
    Check(!FixedPoseWithinDisplacement({2,3,4},reference,reference,
                                       std::numeric_limits<double>::quiet_NaN()),
          "a NaN displacement budget is rejected");
    Check(!FixedPoseWithinDisplacement({2,3,4},reference,reference,
                                       std::numeric_limits<double>::infinity()),
          "an infinite displacement budget is rejected");
}
} // namespace

int main() {
    FixedPoseGuard();
    FixedCorner();
    GroupAndConnectivityBoundaries();
    InvalidAndBudgets();
    std::cout << checks << " checks passed\n";
}
