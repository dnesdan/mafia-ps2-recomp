// Pixel-level triangle coverage checks against the complete production GS backend.
// The oracle uses rational scanline intersections, not the runtime edge predicate.
#include "runtime/gs/gs_cpu_backend.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
struct Point { int32_t x, y; }; // Sixteenths, after XYOFFSET.
using Triangle = std::array<Point, 3>;
constexpr std::array<std::array<unsigned, 3>, 6> permutations{{
    {0,1,2}, {0,2,1}, {1,0,2}, {1,2,0}, {2,0,1}, {2,1,0}
}};
Triangle permuted(const Triangle &t, const std::array<unsigned, 3> &p) {
    return {t[p[0]], t[p[1]], t[p[2]]};
}
constexpr uint32_t side = 18, background = 0x80100C08u, ink = 0x80201810u;
uint64_t checks = 0, draws = 0;
void require(bool ok, const std::string &message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}

bool covered(const Triangle &t, int x, int y) {
    // At the existing backend sample phase (x+1/2, y+1/2), not a new
    // claim about hardware sample location. Top/left inclusive, bottom/right exclusive.
    const int64_t sx = 16 * x + 8, sy = 16 * y + 8;
    struct Fraction { int64_t n, d; } hits[3]{};
    unsigned count = 0;
    for (unsigned i = 0; i < 3; ++i) {
        Point a = t[i], b = t[(i + 1) % 3];
        if (a.y > b.y) std::swap(a, b);
        if (sy < a.y || sy >= b.y) continue; // Includes rejection of horizontal edges.
        const int64_t dy = int64_t(b.y) - a.y;
        hits[count++] = {int64_t(a.x) * dy + (sy - a.y) * (int64_t(b.x) - a.x), dy};
    }
    if (count != 2) return false;
    if (hits[0].n * hits[1].d > hits[1].n * hits[0].d) std::swap(hits[0], hits[1]);
    return sx * hits[0].d >= hits[0].n && sx * hits[1].d < hits[1].n;
}

struct Fixture {
    std::vector<uint8_t> ram = std::vector<uint8_t>(4u * 1024u * 1024u, 0xA5u);
    GSCpuBackend gs;
    GSPrimitiveBatch batch{};
    Fixture() {
        gs.Initialize(ram.data(), static_cast<uint32_t>(ram.size()));
        batch.vertexCount = 3;
        auto &s = batch.state;
        s.prim.type = GS_PRIM_TRIANGLE;
        s.prim.abe = true;
        s.context.frame = {2u, 1u, GS_PSM_CT32, 0u};
        s.context.scissor = {0u, 15u, 0u, 15u};
        s.context.xyoffset = {32768u, 32768u};
        s.context.zbuf = {20u, GS_PSM_Z32, true};
        s.context.test = (1ull << 16u) | (1ull << 17u); // Z always.
        s.context.alpha = (2ull << 2u) | (2ull << 4u) | (1ull << 6u) | (128ull << 32u);
        s.colclamp = 1;
        for (auto &v : batch.vertices) {
            v.r = 16; v.g = 24; v.b = 32; v.a = 128; v.z = 100;
        }
    }
    void clear() {
        GSContext c = batch.state.context;
        c.scissor = {0u, side-1, 0u, side-1};
        c.frame.fbmsk = 0;
        require(gs.ClearFramebuffer(c, background), "frame setup failed");
    }
    void draw(const Triangle &t) {
        for (unsigned i = 0; i < 3; ++i) {
            const int64_t rawX = int64_t(t[i].x) + batch.state.context.xyoffset.ofx;
            const int64_t rawY = int64_t(t[i].y) + batch.state.context.xyoffset.ofy;
            require(rawX >= 0 && rawX <= 65535 && rawY >= 0 && rawY <= 65535,
                    "fixture exceeds original 12.4 vertex range");
            batch.vertices[i].x = static_cast<float>(rawX) / 16.0f;
            batch.vertices[i].y = static_cast<float>(rawY) / 16.0f;
        }
        gs.Submit(batch); ++draws;
    }
    uint32_t pixel(unsigned x, unsigned y) {
        return gs.ReadVram(GS_PSM_CT32, 64u, 1u, x, y);
    }
    void verify(const std::vector<Triangle> &triangles) {
        for (unsigned y = 0; y < side; ++y) for (unsigned x = 0; x < side; ++x) {
            unsigned hits = 0;
            const auto &c = batch.state.context.scissor;
            if (x >= c.x0 && x <= c.x1 && y >= c.y0 && y <= c.y1)
                for (const auto &t : triangles) hits += covered(t, x, y);
            const uint32_t mask = batch.state.context.frame.fbmsk;
            const uint32_t expected = ((background + hits * (ink & 0xFFFFFFu)) & ~mask) | (background & mask);
            require(pixel(x, y) == expected, "wrong coverage/blending at " + std::to_string(x) + "," +
                    std::to_string(y) + " expected=" + std::to_string(expected) + " actual=" + std::to_string(pixel(x,y)));
        }
    }
    void run(const std::vector<Triangle> &t) { clear(); for (const auto &v : t) draw(v); verify(t); }
};

std::vector<Triangle> quad(int dx, int dy, bool diagonal, unsigned order) {
    const Point a{16+dx,16+dy}, b{208+dx,16+dy}, c{208+dx,208+dy}, d{16+dx,208+dy};
    std::vector<Triangle> q = diagonal ? std::vector<Triangle>{{a,b,d},{b,c,d}} :
                                       std::vector<Triangle>{{a,b,c},{a,c,d}};
    if (order & 1) std::swap(q[0][0],q[0][1]);
    if (order & 2) std::swap(q[1][0],q[1][1]);
    if (order & 4) std::swap(q[0],q[1]);
    return q;
}
void quadGrid(bool diagonal, bool clipped) {
    Fixture f;
    if (clipped) f.batch.state.context.scissor = {3u, 9u, 4u, 11u};
    for (int dx = 0; dx < 16; ++dx) for (int dy = 0; dy < 16; ++dy)
        for (unsigned order = 0; order < 8; ++order) f.run(quad(dx,dy,diagonal,order));
}
void straightSharedEdge(bool horizontal) {
    Fixture f;
    for (int fraction : {0,1,7,8,9,15}) {
        Point a{24,24+fraction}, b{200,24+fraction}, c{200,200+fraction}, d{24,200+fraction};
        Point center{120,120+fraction};
        std::vector<Triangle> mesh = horizontal ? std::vector<Triangle>{{a,b,center},{b,c,center},{c,d,center},{d,a,center}} :
            std::vector<Triangle>{Triangle{{{24+fraction,24},{112+fraction,24},{112+fraction,200}}},
                                  Triangle{{{24+fraction,24},{112+fraction,200},{24+fraction,200}}},
                                  Triangle{{{112+fraction,24},{200+fraction,24},{200+fraction,200}}},
                                  Triangle{{{112+fraction,24},{200+fraction,200},{112+fraction,200}}}};
        for (unsigned order=0; order<2; ++order) {
            f.run(mesh);
            std::reverse(mesh.begin(),mesh.end());
            for (auto &t : mesh) std::swap(t[0],t[1]);
        }
    }
}
void horizontalSharedEdge() {
    Fixture f;
    for (int fraction = 0; fraction < 16; ++fraction) {
        const Point a{24,24}, b{200,24}, c{24,112+fraction}, d{200,112+fraction},
                    e{24,200}, g{200,200};
        std::vector<Triangle> mesh{{a,b,d},{a,d,c},{c,d,g},{c,g,e}};
        for (unsigned order=0; order<2; ++order) {
            f.run(mesh);
            std::reverse(mesh.begin(),mesh.end());
            for (auto &t:mesh) std::swap(t[0],t[1]);
        }
    }
}

void nearEdge(bool vertical) {
    Fixture f;
    Triangle t{{{9,-16000},{9,16000},{32000,8}}};
    if (!vertical) for (auto &p : t) std::swap(p.x,p.y);
    for (const auto &p : permutations) f.run({permuted(t,p)});
}
void windingAndClipping() {
    Fixture f;
    uint32_t rng=0x512A3u;
    auto next=[&] { rng=rng*1664525u+1013904223u;return int((rng>>8)%600)-180; };
    for (unsigned i=0; i<256; ++i) {
        Triangle t; for (auto &p:t) p={next(),next()};
        const int64_t area=int64_t(t[1].x-t[0].x)*(t[2].y-t[0].y)-int64_t(t[1].y-t[0].y)*(t[2].x-t[0].x);
        if (!area) continue;
        for (const auto &p : permutations) f.run({permuted(t,p)});
    }
}
void offsetTranslation() {
    Fixture f;
    for (uint16_t off : {0u,1u,7u,8u,15u,128u,32768u,65000u}) {
        f.batch.state.context.xyoffset={off,static_cast<uint16_t>(off+1u)};
        f.run(quad(8,8,false,3));
    }
}
void rejectedGeometry() {
    Fixture f;
    f.run({{{{16,16},{80,80},{160,160}}}, {{{0,0},{0,0},{0,0}}}});
    f.run({{{{-240,-240},{-16,-240},{-240,-16}}}});
    f.run({{{{1024,1024},{1040,1024},{1024,1040}}}});
    f.batch.state.context.scissor={8,3,8,3};
    f.run(quad(0,0,false,0));
}
void deliberateOverlap() {
    Fixture f;
    const Triangle t{{{16,16},{208,16},{208,208}}};
    f.clear(); f.draw(t); f.draw(t);
    // Strictly interior samples still receive both deliberate overlapping calls.
    require(f.pixel(8,3) == background + 2u * (ink & 0xFFFFFFu), "intentional overdraw was deduplicated");
    require(f.pixel(3,8) == background, "overdraw changed an exterior pixel");
}
void primitiveDispatch() {
    Fixture f;
    for (auto type : {GS_PRIM_TRIANGLE,GS_PRIM_TRISTRIP,GS_PRIM_TRIFAN}) {
        f.batch.state.prim.type=type;
        f.run(quad(8,8,false,5));
    }
}
void depthAndMasks() {
    Fixture f;
    for (unsigned method : {2u,3u}) for (uint32_t mask : {0u,0xFF000000u,0xFF00FF00u}) {
        auto &c=f.batch.state.context;
        c.test=(1ull<<16u)|(uint64_t(method)<<17u);
        c.zbuf.zmask=false; c.frame.fbmsk=mask;
        for (unsigned y=0;y<side;++y) for (unsigned x=0;x<side;++x)
            f.gs.WriteVram(GS_PSM_Z32,640,1,x,y,0u);
        f.run(quad(8,8,true,5));
    }
}
void texturedTriangles() {
    for (auto psm : {GS_PSM_CT32,GS_PSM_CT24,GS_PSM_CT16,GS_PSM_CT16S,
                    GS_PSM_T8,GS_PSM_T8H,GS_PSM_T4,GS_PSM_T4HL,GS_PSM_T4HH}) {
        Fixture f;
        auto &s=f.batch.state;
        s.prim.tme=true;
        s.textureWidth=8; s.textureHeight=8;
        auto &t=s.context.tex0;
        t.tbp0=1024; t.tbw=1; t.psm=psm; t.tw=3; t.th=3; t.tfx=1; t.tcc=1;
        s.texa.ta0=128; s.texa.ta1=128;
        uint32_t raw=ink;
        if (psm==GS_PSM_CT16 || psm==GS_PSM_CT16S)
            raw=0x8000u | 2u | (3u<<5u) | (4u<<10u);
        if (psm==GS_PSM_T8 || psm==GS_PSM_T8H || psm==GS_PSM_T4 ||
            psm==GS_PSM_T4HL || psm==GS_PSM_T4HH) {
            raw=3; t.cbp=2048; t.cpsm=GS_PSM_CT32; t.cld=1;
            f.gs.WriteVram(GS_PSM_CT32,t.cbp,1,3,0,ink);
            f.gs.LoadClut(t,s.texclut);
        }
        f.gs.WriteVram(psm,t.tbp0,1,0,0,raw);
        f.gs.TextureFlush();
        for (auto &v:f.batch.vertices) {v.u=8;v.v=8;v.s=0.0625f;v.t=0.0625f;v.q=1.0f;}
        for (bool fst : {false,true}) for (bool diag : {false,true}) {
            s.prim.fst=fst;
            f.run(quad(8,8,diag,5));
            f.run(quad(0,0,diag,2)); // Reuse the actual warmed texture cache and CLUT.
        }
    }
}

void flatVertexState() {
    Fixture f;
    f.batch.state.prim.abe=false;
    f.batch.state.context.zbuf.zmask=false;
    auto q=quad(0,0,false,0);
    for (unsigned perm=0;perm<3;++perm) {
        f.clear();
        f.batch.vertices[0].r=1;f.batch.vertices[1].r=2;f.batch.vertices[2].r=37;
        for (auto &v:f.batch.vertices) {v.g=41;v.b=43;v.a=128;v.z=0xFFFFFF00u;}
        f.draw(q[0]);
        require(f.pixel(8,3)==0x802B2925u,"coverage fix changed the original last-vertex flat color");
        require(f.gs.ReadVram(GS_PSM_Z32,640,1,8,3)==0xFFFFFF00u,"constant Z lost precision");
        std::rotate(q[0].begin(),q[0].begin()+1,q[0].end());
    }
}
}
int main() {
    const std::pair<const char*,std::function<void()>> tests[] = {
        {"both windings and all fractional phases share descending diagonal once",[]{quadGrid(false,false);}},
        {"both windings and all fractional phases share ascending diagonal once",[]{quadGrid(true,false);}},
        {"clipped descending-diagonal mesh retains unique pixel ownership",[]{quadGrid(false,true);}},
        {"clipped ascending-diagonal mesh retains unique pixel ownership",[]{quadGrid(true,true);}},
        {"shared vertices and fan edges have unique owners",[]{straightSharedEdge(true);}},
        {"vertical edges and tiled quads have unique owners",[]{straightSharedEdge(false);}},
        {"horizontal shared edges have unique owners",horizontalSharedEdge},
        {"large triangle cannot accept a sample just outside its left edge",[]{nearEdge(true);}},
        {"large triangle cannot accept a sample just outside its top edge",[]{nearEdge(false);}},
        {"rational scanline oracle agrees across vertex orders",windingAndClipping},
        {"fractional XYOFFSET translation preserves ownership",offsetTranslation},
        {"degenerate, outside and inverted-scissor draws remain empty",rejectedGeometry},
        {"intentional overlapping draws are not deduplicated",deliberateOverlap},
        {"flat provoking vertex and full-width constant Z stay intact",flatVertexState},
        {"triangle strip and fan batches share the same coverage",primitiveDispatch},
        {"depth comparisons and framebuffer masks retain seam-free coverage",depthAndMasks},
        {"texture and palette-backed triangles retain unique edge ownership",texturedTriangles},
    };
    unsigned failed=0;
    for (const auto &[name,test]:tests) {
        try {test();std::cout<<"PASS "<<name<<'\n';}
        catch (const std::exception &e) {++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    }
    std::cout<<"RESULT groups="<<std::size(tests)<<" failed="<<failed<<" draws="<<draws<<" assertions="<<checks<<'\n';
    return failed?1:0;
}
