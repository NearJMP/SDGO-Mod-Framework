#include "sdgo/surface_decal.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace sdgo::surface_decal;
namespace {
unsigned checks=0;
void Check(bool value, const char* message) {
    ++checks;
    if (!value) {std::cerr << "FAIL: " << message << '\n'; std::exit(1);}
}
bool Near(double a,double b) {return std::abs(a-b)<1e-7;}
Mesh Plane() {
    return {{{-2,-2,0},{2,-2,0},{2,2,0},{-2,2,0}},{{{{0,1,2}}},{{{0,2,3}}}}};
}
Placement Place(const Mesh& m, double width=2, double height=2) {
    Hit hit;
    Check(Pick(m,{{0,0,10},{0,0,-2}},hit)==Result::ok,"pick a front-facing surface");
    Check(Near(hit.distance,10),"non-unit rays return physical distance");
    Placement p;
    Check(MakePlacement(m,hit,123,9,{1,0,0},width,height,0.2,p)==Result::ok,"make surface placement");
    return p;
}
double Area(const Patch& p) {
    double total=0;
    for (std::size_t i=0;i<p.vertices.size();i+=3) {
        const auto a=p.vertices[i].position,b=p.vertices[i+1].position,c=p.vertices[i+2].position;
        total+=std::abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))/2;
    }
    return total;
}
void ValidatePatch(const Mesh& m,const Patch& p) {
    Check(!p.vertices.empty()&&p.vertices.size()%3==0,"patch has whole triangles");
    for (const auto& v:p.vertices) {
        Check(v.uv.x>=0&&v.uv.x<=1&&v.uv.y>=0&&v.uv.y<=1,"clipped UV remains inside texture");
        Check(v.triangle<m.triangles.size(),"source triangle retained");
        Check(Near(v.barycentric[0]+v.barycentric[1]+v.barycentric[2],1),"clipped barycentric coordinates sum to one");
        Vec3 reconstructed{};
        for (std::size_t i=0;i<3;++i) {
            const auto source=m.vertices[m.triangles[v.triangle].vertex[i]];
            reconstructed.x+=source.x*v.barycentric[i];
            reconstructed.y+=source.y*v.barycentric[i];
            reconstructed.z+=source.z*v.barycentric[i];
        }
        Check(Near(v.position.x,reconstructed.x)&&Near(v.position.y,reconstructed.y)&&Near(v.position.z,reconstructed.z),
              "clipped vertex stays on original surface");
    }
}
void Geometry() {
    auto m=Plane(); auto p=Place(m); Patch patch;
    Check(Project(m,123,9,p,patch)==Result::ok,"project across connected surface triangles");
    ValidatePatch(m,patch);
    Check(Near(Area(patch),4),"projected area matches decal rectangle");
    for (const auto& v:patch.vertices) {
        Check(Near(v.uv.x,0.5-v.position.x/2)&&Near(v.uv.y,0.5-v.position.y/2),"native left-handed texture orientation preserves lettering");
    }
    p.width=1;p.height=2;p.rotation=3.14159265358979323846/2;
    Check(Project(m,123,9,p,patch)==Result::ok,"rotate and scale");
    ValidatePatch(m,patch);
    Check(Near(Area(patch),2),"rotation preserves decal area");
    for (const auto& v:patch.vertices) {
        Check(std::abs(v.position.x)<=1.0000001&&std::abs(v.position.y)<=0.5000001,"rotated rectangle has expected extents");
    }
    const auto sentinel=patch.vertices.size();
    Check(Project(m,124,9,p,patch)==Result::wrongGeometry&&patch.vertices.size()==sentinel,"model replacement cannot reuse stale triangle anchor");
    Check(Project(m,123,10,p,patch)==Result::wrongGeometry,"different node rejected");
    p.width=0;
    Check(Project(m,123,9,p,patch)==Result::invalidPlacement,"zero width rejected");
    p=Place(m);p.rotation=std::numeric_limits<double>::quiet_NaN();
    Check(Project(m,123,9,p,patch)==Result::invalidPlacement,"NaN rotation rejected");
    p=Place(m);p.anchor.barycentric={0.5,0.5,0.5};
    Check(Project(m,123,9,p,patch)==Result::invalidPlacement,"invalid barycentric sum rejected");
    p=Place(m);p.anchor.tangent={0,0,1};
    Check(Project(m,123,9,p,patch)==Result::invalidPlacement,"surface-normal tangent rejected");
    p=Place(m);Limits l;l.outputTriangles=1;
    Check(Project(m,123,9,p,patch,l)==Result::budgetExceeded&&patch.vertices.size()==sentinel,"budget failure never emits partial geometry");
    Hit hit{77,{{1,0,0}},42};
    Check(Pick(m,{{0,0,-10},{0,0,1}},hit)==Result::miss&&hit.triangle==77,"back side is not selectable");
    Check(Pick(m,{{0,0,10},{0,0,0}},hit)==Result::invalidRay,"zero ray rejected");
    Check(Pick(m,{{0,0,10},{0,0,std::numeric_limits<double>::infinity()}},hit)==Result::invalidRay,"infinite ray rejected");
    m.triangles[0].vertex[2]=999;
    Check(Pick(m,{{0,0,10},{0,0,-1}},hit)==Result::invalidMesh,"out-of-range vertex rejected");
    m=Plane();m.vertices[0].x=std::numeric_limits<double>::quiet_NaN();
    Check(Project(m,123,9,p,patch)==Result::invalidMesh,"invalid model coordinates rejected");

    m=Plane(); p=Place(m);
    // A second, disconnected plate occupies the same projection volume.
    m.vertices.insert(m.vertices.end(),{{-2,-2,-0.05},{2,-2,-0.05},{2,2,-0.05},{-2,2,-0.05}});
    m.triangles.push_back({{{4,5,6}}});m.triangles.push_back({{{4,6,7}}});
    Check(Project(m,123,9,p,patch)==Result::ok&&Near(Area(patch),4),"decal does not copy onto disconnected armor");
    for(const auto& v:patch.vertices)Check(v.triangle<2,"only selected connected surface receives decal");
    Check(Pick(m,{{0,0,10},{0,0,-1}},hit)==Result::ok&&Near(hit.distance,10),"nearest surface wins");

    // A folded edge remains surface-conforming but is clipped at projection depth.
    m=Plane();m.vertices[3].z=1;p=Place(m);p.depth=0.05;
    Check(Project(m,123,9,p,patch)==Result::ok,"project onto bent surface");
    ValidatePatch(m,patch);
    for(const auto& v:patch.vertices)Check(std::abs(v.position.z)<=0.0500001,"projection depth clips folded surface");
    Check(Area(patch)<4,"depth clipping reduces covered area");

    m=Plane();m.vertices[3]={0,-3,0};p=Place(m);
    Check(Project(m,123,9,p,patch)==Result::ok,"back-facing neighbor does not abort front face");
    for(const auto& v:patch.vertices)Check(v.triangle==0,"back-facing neighbor not painted");
    m=Plane();m.vertices.push_back({-3,3,0});m.triangles.push_back({{{0,2,4}}});p=Place(m);
    Check(Project(m,123,9,p,patch)==Result::ok,"non-manifold edge bounded");
    for(const auto& v:patch.vertices)Check(v.triangle==0,"ambiguous seam not traversed");
}
void Editing() {
    Appearance original;original.unitSerial=10;original.unitType=100;original.revision=5;
    original.slots[0].sticker=1234;original.slots[1].sticker=2345;
    Editor editor; const auto placement=Place(Plane());Appearance proposal;
    Check(editor.Open(original),"open authoritative unit appearance");
    Check(!editor.Place(3,placement)&&!editor.Place(2,placement),"cannot place absent sticker or fourth slot");
    Check(editor.Place(0,placement)&&editor.Dirty(),"preview changes draft");
    Check(!editor.Saved().slots[0].positioned,"preview cannot mutate confirmed appearance");
    auto other=original;other.unitSerial=20;
    Check(!editor.Open(other),"changing units cannot discard unconfirmed work");
    Check(editor.Cancel()&&!editor.Dirty()&&editor.State()==EditState::closed,"cancel restores saved placement");
    Check(editor.Open(original)&&editor.Place(0,placement),"reopen and preview");
    Check(editor.BeginSave(1,proposal)&&proposal.revision==5,"request retains expected revision");
    Check(!editor.Place(1,placement)&&!editor.Cancel()&&!editor.Open(other),"in-flight operation cannot be changed or abandoned");
    auto saved=proposal;++saved.revision;
    Check(!editor.Acknowledge(2,Reply::saved,saved),"unrelated receipt ignored");
    Check(!editor.Acknowledge(1,Reply::saved,proposal),"success must advance revision");
    auto altered=saved;altered.slots[1].sticker=777;
    Check(!editor.Acknowledge(1,Reply::saved,altered),"success receipt must match requested appearance");
    Check(editor.Acknowledge(1,Reply::saved,saved)&&!editor.Dirty(),"exact server receipt commits appearance");
    Check(editor.ResetPosition(0),"reset restores original native slot behavior");
    Check(!editor.BeginSave(1,proposal),"request IDs cannot be reused");
    Check(editor.BeginSave(2,proposal),"send new request");editor.Timeout();
    Check(editor.State()==EditState::uncertain&&!editor.BeginSave(3,proposal)&&!editor.Cancel(),"timeout cannot masquerade as rollback or cause duplicate save");
    Check(!editor.Reconcile(other),"another unit cannot reconcile timed-out operation");
    auto inconsistent=saved;inconsistent.slots[1].sticker=999;
    Check(!editor.Reconcile(inconsistent),"same revision cannot have different authoritative content");
    Check(editor.Reconcile(saved)&&!editor.Dirty(),"authoritative refresh resolves uncertainty");
    Check(editor.ResetPosition(0)&&editor.BeginSave(3,proposal),"new request after reconciliation");
    auto conflict=saved;++conflict.revision;
    Check(editor.Acknowledge(3,Reply::conflict,conflict)&&!editor.Dirty(),"conflict restores newer server state");
    Check(editor.ResetPosition(0)&&editor.BeginSave(4,proposal),"request to reject");
    Check(editor.Acknowledge(4,Reply::rejected,conflict)&&!editor.Dirty(),"rejection restores authoritative state");
}
}
void CornersAndLayers(){
    Mesh fold{{{-2,-1,0},{0,-1,0},{0,1,0},{-2,1,0},{0,-1,-2},{0,1,-2}},
        {{{{0,1,2}}},{{{0,2,3}}},{{{1,4,5}}},{{{1,5,2}}}}};
    Placement p;Check(MakePlacement(fold,{0,{{.125,.375,.5}},0},77,4,{1,0,0},2,1,.05,p)==Result::ok,"anchor beside a right-angle armor edge");
    Patch wrapped,flat;Check(Project(fold,77,4,p,flat)==Result::ok,"legacy planar placement remains available");
    Check(ProjectWrapped(fold,77,4,p,wrapped)==Result::ok,"unfold across a right-angle edge");
    bool side=false;for(const auto& v:wrapped.vertices){side=side||v.triangle>=2;Check(v.uv.x>=0&&v.uv.x<=1&&v.uv.y>=0&&v.uv.y<=1,"wrapped UVs stay within sticker");}
    const auto physicalArea=[](const Patch& patch){double total=0;
        for(std::size_t i=0;i<patch.vertices.size();i+=3){const auto a=patch.vertices[i].position,b=patch.vertices[i+1].position,c=patch.vertices[i+2].position;
            const double x=(b.y-a.y)*(c.z-a.z)-(b.z-a.z)*(c.y-a.y),y=(b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z),z=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
            total+=std::hypot(x,y,z)/2;}return total;};
    Check(side&&physicalArea(wrapped)>physicalArea(flat)+.5&&Near(physicalArea(wrapped),2),"corner keeps the sticker's intrinsic area rather than disappearing or stretching");
    ValidatePatch(fold,wrapped);
    const auto count=wrapped.vertices.size();Limits limits;limits.outputTriangles=1;
    Check(ProjectWrapped(fold,77,4,p,wrapped,limits)==Result::budgetExceeded&&wrapped.vertices.size()==count,"corner budget refusal preserves prior geometry");
    Check(ProjectWrapped(fold,78,4,p,wrapped)==Result::wrongGeometry,"another model cannot borrow corner anchors");
    unsigned removed=0,kept=0;bool variation=false;
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){
        const double u=(x+.5)/64,v=(y+.5)/64;const auto a=WearCoverage(u,v,300,17),b=WearCoverage(u,v,600,17);
        Check(WearCoverage(u,v,0,17)==1&&WearCoverage(u,v,1000,17)==0,"wear endpoints preserve or remove original ink");
        Check(a>=b&&a==WearCoverage(u,v,300,17),"wear is deterministic and monotonically removes ink");
        if(b==0)++removed;if(b==1)++kept;variation=variation||b!=WearCoverage(u,v,600,18);
    }
    Check(removed>400&&kept>400&&variation,"wear contains actual scratches and islands, not global transparency");
    Appearance a;a.unitSerial=20;a.unitType=100;a.revision=1;
    a.slots[0].sticker=42;a.slots[1].sticker=42;Editor editor;Check(editor.Open(a)&&editor.Place(0,p),"duplicate equipped stickers retain distinct native slots");
    Check(editor.SetWrap(0,true)&&editor.SetWear(0,450,123)&&editor.MoveLayer(0,2),"layer and wear changes are editable parameters");
    Check(editor.Preview().order==std::array<std::uint8_t,3>{{1,2,0}}&&editor.Preview().slots[0].sticker==42&&editor.Preview().slots[1].sticker==42,"reordering never swaps inventory identities");
    Check(!editor.SetWear(0,1001,1)&&!editor.SetWear(1,1,1)&&!editor.MoveLayer(2,0),"invalid wear and absent/unpositioned stickers are refused");
    auto bad=editor.Preview();bad.order={{0,0,2}};Check(!Valid(bad),"duplicate layers are invalid");
    Appearance proposal;Check(editor.BeginSave(1,proposal)&&!editor.MoveLayer(0,0)&&!editor.SetWear(0,1,1),"in-flight style save cannot be edited");
    auto authority=proposal;++authority.revision;Check(editor.Acknowledge(1,Reply::saved,authority),"server confirms exact style and order");
    Check(editor.ResetPosition(0)&&editor.Preview().styles[0].wear==0&&!editor.Preview().styles[0].wrap,"native-position reset also restores native ink rendering");
}
int main() {Geometry();Editing();CornersAndLayers();std::cout << checks << " checks passed\n";}
