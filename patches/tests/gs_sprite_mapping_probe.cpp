// Executes the production DrawSprite body at its sampler/writer boundary.
// These recorders deliberately do not emulate filtering, VRAM or the GS frontend.
#include "runtime/gs/gs_types.h"
#include "runtime/gs/ps2_gs_common.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace GSInternal;
using u32 = uint32_t;
#include "sprite_helpers.inc"

struct Sample {
    float s, t, q;
    uint16_t u, v;
    bool fixed, linear;
};
struct Pixel {
    int x, y;
    uint32_t z;
    uint8_t r, g, b, a, fog;
};
class GSCpuBackend {
public:
    std::vector<Sample> samples;
    std::vector<Pixel> pixels;
    void DrawSprite(const GSPrimitiveBatch &batch);
    uint32_t SampleTexture(const GSDrawState &state, float s, float t, float q, uint16_t u, uint16_t v) {
        samples.push_back({s, t, q, u, v, state.prim.fst, state.linearFilter});
        return 0x80402010u;
    }
    void WritePixel(const GSDrawState &, int x, int y, int z,
                    uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint8_t fog) {
        pixels.push_back({x, y, static_cast<uint32_t>(z), r, g, b, a, fog});
    }
};
#include "sprite_draw.inc"

namespace {
uint64_t drawCount = 0, sampleChecks = 0;
void require(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}
void near(float value, double expected, const char *label) {
    require(std::isfinite(value) && std::fabs(double(value) - expected) <=
            2e-6 * std::max(1.0, std::fabs(expected)), label);
}
GSPrimitiveBatch batch(bool fixed = true, bool reverseX = false, bool reverseY = false) {
    GSPrimitiveBatch b{};
    b.vertexCount = 2;
    b.state.prim.type = GS_PRIM_SPRITE;
    b.state.prim.tme = true;
    b.state.prim.fst = fixed;
    b.state.context.scissor = {0, 31, 0, 31};
    b.state.context.tex0.tfx = 1; // DECAL: the recorder texel passes through.
    b.state.context.tex0.tcc = 1;
    b.state.textureWidth = 16;
    b.state.textureHeight = 32;
    auto &a = b.vertices[0]; auto &c = b.vertices[1];
    a.x = reverseX ? 8.0f : 4.0f; c.x = reverseX ? 4.0f : 8.0f;
    a.y = reverseY ? 8.0f : 4.0f; c.y = reverseY ? 4.0f : 8.0f;
    a.u = 32; c.u = 96; a.v = 48; c.v = 112;
    a.s = 0.125f; c.s = 0.75f; a.t = 0.25f; c.t = 0.5f;
    a.q = 1.0f; c.q = 1.0f;
    a.z = 1; c.z = 0xFEDCBA98u;
    a.r = a.g = a.b = a.a = a.fog = 1;
    c.r = 31; c.g = 63; c.b = 127; c.a = 65; c.fog = 173;
    return b;
}

// Integer oracle for interpolation in 1/16-texel units. No copied float
// endpoint normalization: the weight is measured from the original first corner.
uint16_t fixedExpected(uint16_t a, uint16_t b, int pixel, int p0, int p1) {
    const int64_t span = std::abs(p1 - p0);
    const int64_t weight = p1 > p0 ? 2 * (pixel - p0) + 1 : 2 * (p0 - pixel) - 1;
    const int64_t numerator = (2 * span - weight) * a + weight * b;
    return static_cast<uint16_t>((numerator + span) / (2 * span));
}
void verify(const GSPrimitiveBatch &b) {
    ++drawCount;
    GSCpuBackend renderer;
    renderer.DrawSprite(b);
    const auto &a = b.vertices[0]; const auto &c = b.vertices[1];
    const auto &offset = b.state.context.xyoffset;
    const auto &scissor = b.state.context.scissor;
    // Deliberately preserve this backend's existing coverage policy. This test
    // does not declare floor coverage or the half-pixel sample point GS-accurate.
    const int x0 = int(std::floor(double(a.x) - double(offset.ofx) / 16.0));
    const int x1 = int(std::floor(double(c.x) - double(offset.ofx) / 16.0));
    const int y0 = int(std::floor(double(a.y) - double(offset.ofy) / 16.0));
    const int y1 = int(std::floor(double(c.y) - double(offset.ofy) / 16.0));
    require(x0 != x1 && y0 != y1, "fixture must have nonzero extent");
    const int left = std::max<int>(std::min(x0, x1), scissor.x0);
    const int right = std::min<int>(std::max(x0, x1) - 1, scissor.x1);
    const int top = std::max<int>(std::min(y0, y1), scissor.y0);
    const int bottom = std::min<int>(std::max(y0, y1) - 1, scissor.y1);
    const size_t count = size_t(std::max(0, right-left+1)) * size_t(std::max(0, bottom-top+1));
    require(renderer.pixels.size() == count, "sprite coverage changed");
    require(renderer.samples.size() == (b.state.prim.tme ? count : 0), "wrong texture sample count");
    size_t i = 0;
    for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x, ++i) {
        const auto &pixel = renderer.pixels.at(i);
        require(pixel.x == x && pixel.y == y && pixel.z == uint32_t(c.z) && pixel.fog == c.fog,
                "clipping, second-vertex depth or fog changed");
        if (!b.state.prim.tme) {
            require(pixel.r == c.r && pixel.g == c.g && pixel.b == c.b && pixel.a == c.a,
                    "untextured flat color changed");
            continue;
        }
        ++sampleChecks;
        const auto &sample = renderer.samples.at(i);
        require(sample.fixed == b.state.prim.fst && sample.linear == b.state.linearFilter,
                "sampler flags changed");
        require(pixel.r == 0x10 && pixel.g == 0x20 && pixel.b == 0x40 &&
                pixel.a == (b.state.context.tex0.tcc ? 0x80 : c.a), "DECAL flat alpha changed");
        if (b.state.prim.fst) {
            require(sample.u == fixedExpected(a.u, c.u, x, x0, x1), "U endpoint fraction/direction lost");
            require(sample.v == fixedExpected(a.v, c.v, y, y0, y1), "V endpoint fraction/direction lost");
            require(sample.s == 0 && sample.t == 0 && sample.q == 1, "UV sampler arguments changed");
        } else {
            const double tx = (double(x) + 0.5 - x0) / (x1 - x0);
            const double ty = (double(y) + 0.5 - y0) / (y1 - y0);
            const double divisor = std::fabs(c.q) > 1e-8f ? c.q : 1.0;
            near(sample.s, ((1-tx)*a.s + tx*c.s) / divisor, "S must use the original endpoints and last Q");
            near(sample.t, ((1-ty)*a.t + ty*c.t) / divisor, "T must use the original endpoints and last Q");
            require(sample.q == 1 && sample.u == 0 && sample.v == 0, "ST sampler arguments changed");
        }
    }
}
void integralControl() {
    for (bool fixed : {false, true}) for (bool linear : {false, true}) {
        auto b = batch(fixed); b.state.linearFilter = linear; verify(b);
    }
}
void fractionalUV() {
    // Every low-nibble combination at both endpoints, on both axes.
    for (unsigned u0=0; u0<16; ++u0) for (unsigned u1=0; u1<16; ++u1)
    for (unsigned v0=0; v0<16; ++v0) for (unsigned v1=0; v1<16; ++v1) {
        auto b = batch();
        b.vertices[0].u += u0; b.vertices[1].u += u1;
        b.vertices[0].v += v0; b.vertices[1].v += v1;
        verify(b);
    }
}
void reversedUV() {
    for (bool rx : {false, true}) for (bool ry : {false, true}) verify(batch(true, rx, ry));
}
void reversedST() {
    for (bool rx : {false, true}) for (bool ry : {false, true}) verify(batch(false, rx, ry));
}
void lastQ() {
    for (float q : {0.25f, 0.5f, 2.0f, 4.0f, -2.0f}) {
        auto b = batch(false); b.vertices[0].q = 3; b.vertices[1].q = q; verify(b);
    }
}
void ignoredFirstQ() {
    for (float q : {0.0f, -7.0f, 1e-9f, 1000.0f,
                    std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        auto b = batch(false); b.vertices[0].q = q; b.vertices[1].q = 2; verify(b);
    }
}
void clipped(bool fixed) {
    for (bool rx : {false, true}) for (bool ry : {false, true}) {
        auto b = batch(fixed, rx, ry);
        b.state.context.xyoffset = {17, 25};
        b.state.context.scissor = {4, 5, 3, 5};
        // The half-pixel vertex offset exactly cancels the fractional offset
        // already covered by patch 0054; clipping must not restart interpolation.
        for (auto &v : b.vertices) { v.x += 0.0625f; v.y += 0.5625f; }
        verify(b);
    }
}
void clippedUV() { clipped(true); }
void clippedST() { clipped(false); }
void extremeUV() {
    for (bool rx : {false, true}) for (bool ry : {false, true}) {
        auto b = batch(true, rx, ry);
        b.vertices[0].u=0; b.vertices[1].u=0x3FFF;
        b.vertices[0].v=0x3FFF; b.vertices[1].v=0;
        verify(b);
    }
}
void flatStateControl() {
    for (bool iip : {false, true}) for (bool alpha : {false, true}) {
        auto b = batch(); b.state.prim.iip = iip; b.state.context.tex0.tcc = alpha; verify(b);
    }
}
void untexturedControl() {
    for (bool rx : {false, true}) for (bool ry : {false, true}) {
        auto b = batch(false, rx, ry); b.state.prim.tme = false;
        b.vertices[0].q = b.vertices[1].q = std::numeric_limits<float>::quiet_NaN(); verify(b);
    }
}
void offscreenControl() {
    for (bool fixed : {false, true}) for (bool rx : {false, true}) {
        auto b = batch(fixed, rx); b.state.context.scissor={16,31,16,31}; verify(b);
    }
}
void fallbackQControl() {
    for (float q : {0.0f, 1e-9f, -1e-9f}) {
        auto b = batch(false); b.vertices[0].q=q; b.vertices[1].q=q; verify(b);
    }
}
}
int main() {
    struct Test { const char *name; void (*run)(); };
    const Test tests[] = {
        {"integral UV/ST and filter flags (control)", integralControl},
        {"all four UV fractional nibbles", fractionalUV},
        {"reversed UV axes", reversedUV},
        {"reversed ST axes", reversedST},
        {"second-vertex Q for both endpoints", lastQ},
        {"first-vertex Q ignored", ignoredFirstQ},
        {"UV clipping and fractional XYOFFSET", clippedUV},
        {"ST clipping and fractional XYOFFSET", clippedST},
        {"14-bit UV bounds and descending coordinates", extremeUV},
        {"second-vertex flat state (control)", flatStateControl},
        {"untextured reversed corners (control)", untexturedControl},
        {"fully clipped sprite (control)", offscreenControl},
        {"existing near-zero Q fallback (control)", fallbackQControl},
    };
    unsigned failed=0;
    for (const auto &test : tests) {
        try { test.run(); std::cout << "PASS " << test.name << '\n'; }
        catch (const std::exception &e) { ++failed; std::cerr << "FAIL " << test.name << ": " << e.what() << '\n'; }
    }
    std::cout << "Groups: " << std::size(tests)-failed << '/' << std::size(tests)
              << "; draws attempted: " << drawCount << "; samples checked: " << sampleChecks << '\n';
    return failed ? 1 : 0;
}
