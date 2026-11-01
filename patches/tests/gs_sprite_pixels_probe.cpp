// Same assertions in complete-backend and explicitly selected CT32 slice modes.
#ifdef MAFIA_SPRITE_SOURCE_SLICES
#include "gs_sprite_pixels_slice.h"
#else
#include "runtime/gs/gs_cpu_backend.h"
#endif
#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr uint32_t sentinel = 0x9aee4dc2u;
uint64_t draws = 0, comparisons = 0;
uint32_t color(unsigned x, unsigned y) {
    return 0x80000000u | (8u + 16u * x) | ((8u + 16u * y) << 8u) |
           ((4u + 8u * (x + y)) << 16u);
}
// Independent integer oracle for exact sixteenths, not a copy of the FP fix.
int floor16(int value) { return value >= 0 ? value / 16 : -((15 - value) / 16); }
unsigned repeat(int value) { return static_cast<unsigned>((value % 8 + 8) % 8); }

struct Fixture {
    std::vector<uint8_t> memory = std::vector<uint8_t>(4u * 1024u * 1024u, 0);
    GSCpuBackend backend;
    Fixture() {
        backend.Initialize(memory.data(), static_cast<uint32_t>(memory.size()));
        for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 8; ++x)
            backend.WriteVram(GS_PSM_CT32, 64, 1, x, y, color(x, y));
        backend.TextureFlush();
    }
    GSPrimitiveBatch batch(unsigned width = 1, unsigned height = 1) {
        GSPrimitiveBatch b{};
        b.vertexCount = 2;
        b.state.prim.type = GS_PRIM_SPRITE;
        b.state.prim.tme = true;
        b.state.context.frame.fbw = 1;
        b.state.context.frame.psm = GS_PSM_CT32;
        b.state.context.scissor = {0, 5, 0, 5};
        b.state.context.zbuf.psm = GS_PSM_Z32;
        b.state.context.zbuf.zmask = true;
        b.state.context.test = (1ull << 16u) | (1ull << 17u); // Z enabled, ALWAYS; no Z writes.
        b.state.context.tex0.tbp0 = 64;
        b.state.context.tex0.tbw = 1;
        b.state.context.tex0.psm = GS_PSM_CT32;
        b.state.context.tex0.tfx = 1; // DECAL, preserve sampled RGBA.
        b.state.context.tex0.tcc = 1;
        b.state.textureWidth = b.state.textureHeight = 8;
        b.vertices[1].x = static_cast<float>(width);
        b.vertices[1].y = static_cast<float>(height);
        b.vertices[1].a = 0x80;
        return b;
    }
    GSPrimitiveBatch st(float u0, float v0, float u1, float v1,
                        unsigned width = 1, unsigned height = 1, float q = 1) {
        auto b = batch(width, height);
        b.vertices[0].s = u0 * q / 8; b.vertices[0].t = v0 * q / 8;
        b.vertices[1].s = u1 * q / 8; b.vertices[1].t = v1 * q / 8;
        b.vertices[0].q = b.vertices[1].q = q;
        return b;
    }
    void render(const GSPrimitiveBatch &b) {
        for (unsigned y = 0; y < 6; ++y) for (unsigned x = 0; x < 6; ++x)
            backend.WriteVram(GS_PSM_CT32, 0, 1, x, y, sentinel);
        backend.Submit(b);
        ++draws;
    }
    void frame(const std::function<uint32_t(unsigned, unsigned)> &expected) {
        for (unsigned y = 0; y < 6; ++y) for (unsigned x = 0; x < 6; ++x) {
            const auto actual = backend.ReadVram(GS_PSM_CT32, 0, 1, x, y);
            const auto want = expected(x, y);
            ++comparisons;
            if (actual != want) {
                std::ostringstream error;
                error << "pixel (" << x << ',' << y << ") expected 0x" << std::hex << want
                      << ", got 0x" << actual;
                throw std::runtime_error(error.str());
            }
        }
    }
    void constant(int u16, int v16, unsigned wantU, unsigned wantV, uint64_t clamp = 0, float q = 1) {
        auto b = st(u16 / 16.0f, v16 / 16.0f, u16 / 16.0f, v16 / 16.0f, 1, 1, q);
        b.state.context.clamp = clamp;
        render(b);
        frame([&](unsigned x, unsigned y) { return x == 0 && y == 0 ? color(wantU, wantV) : sentinel; });
    }
};

struct Test { const char *name; std::function<void()> run; };
}

int main() {
    const Test tests[] = {
        {"negative fractional U repeats the preceding texel", [] {
            Fixture f;
            for (int n = -15; n < 0; ++n) f.constant(n, 32, 7, 2);
        }},
        {"negative fractional V repeats the preceding texel", [] {
            Fixture f;
            for (int n = -15; n < 0; ++n) f.constant(32, n, 2, 7);
        }},
        {"both negative axes use the preceding texel", [] {
            Fixture f;
            for (int n = -15; n < 0; ++n) f.constant(n, -16 - n, 7, 7);
        }},
        {"negative coordinates wrap beyond the first tile", [] {
            Fixture f;
            for (int n : {-129, -143, -255, -257, -1023})
                f.constant(n, n, repeat(floor16(n)), repeat(floor16(n)));
        }},
        {"region repeat masks the floored negative coordinate", [] {
            Fixture f;
            const uint64_t clamp = 3ull | (3ull << 2) | (3ull << 4) | (4ull << 14) |
                                   (3ull << 24) | (4ull << 34);
            for (int n = -63; n < 0; ++n)
                f.constant(n, n, (static_cast<unsigned>(floor16(n)) & 3u) | 4u,
                           (static_cast<unsigned>(floor16(n)) & 3u) | 4u, clamp);
        }},
        {"negative Q retains signed ST projection", [] {
            Fixture f;
            f.constant(-4, -20, 7, 6, 0, -2);
        }},
        {"sprite interpolation crosses zero without a false texel zero", [] {
            Fixture f;
            f.render(f.st(-1.5f, 2, 1.5f, 2, 4, 1));
            const unsigned columns[] = {6, 7, 0, 1};
            f.frame([&](unsigned x, unsigned y) { return x < 4 && y == 0 ? color(columns[x], 2) : sentinel; });
        }},
        {"reversed axes retain signed texture interpolation", [] {
            Fixture f;
            auto b = f.st(-1.5f, -1.5f, 1.5f, 1.5f, 4, 4);
            b.vertices[0].x = b.vertices[0].y = 4;
            b.vertices[1].x = b.vertices[1].y = 0;
            f.render(b);
            const unsigned axis[] = {1, 0, 7, 6};
            f.frame([&](unsigned x, unsigned y) { return x < 4 && y < 4 ? color(axis[x], axis[y]) : sentinel; });
        }},
        {"scissor clipping does not restart the signed texture phase", [] {
            Fixture f;
            auto b = f.st(-1.5f, 2, 1.5f, 2, 4, 1);
            b.state.context.scissor.x0 = 1; b.state.context.scissor.x1 = 2;
            f.render(b);
            f.frame([](unsigned x, unsigned y) { return y == 0 && x >= 1 && x <= 2 ? color(x == 1 ? 7 : 0, 2) : sentinel; });
        }},
        {"signed sixteenth grid produces exact framebuffer colors", [] {
            Fixture f;
            for (int v = -128; v < 128; ++v) for (int u = -128; u < 128; ++u)
                f.constant(u, v, repeat(floor16(u)), repeat(floor16(v)));
        }},
        {"exact negative integers are unchanged", [] {
            Fixture f;
            for (int n = -16; n <= 0; ++n) f.constant(n * 16, n * 16, repeat(n), repeat(n));
        }},
        {"positive fractions are unchanged", [] {
            Fixture f;
            for (int n = 0; n < 256; ++n) f.constant(n, n, repeat(n / 16), repeat(n / 16));
        }},
        {"clamp mode retains edge texels", [] {
            Fixture f;
            f.constant(-1, -127, 0, 0, 5);
            f.constant(255, 255, 7, 7, 5);
        }},
        {"region clamp retains the bounded region", [] {
            Fixture f;
            const uint64_t clamp = 2ull | (2ull << 2) | (2ull << 4) | (5ull << 14) |
                                   (1ull << 24) | (6ull << 34);
            f.constant(-1, -127, 2, 1, clamp);
            f.constant(255, 255, 5, 6, clamp);
        }},
        {"UV fractional endpoints from patch 0061 survive to pixels", [] {
            Fixture f;
            auto b = f.batch(4, 1);
            b.state.prim.fst = true;
            b.vertices[0].u = 15; b.vertices[1].u = 47;
            b.vertices[0].v = b.vertices[1].v = 32;
            f.render(b);
            f.frame([](unsigned x, unsigned y) { return x < 4 && y == 0 ? color(1 + x / 2, 2) : sentinel; });
        }},
        {"reversed UV endpoints from patch 0061 survive to pixels", [] {
            Fixture f;
            auto b = f.batch(4, 4);
            b.state.prim.fst = true;
            b.vertices[0].x = b.vertices[0].y = 4;
            b.vertices[1].x = b.vertices[1].y = 0;
            b.vertices[1].u = b.vertices[1].v = 64;
            f.render(b);
            f.frame([](unsigned x, unsigned y) { return x < 4 && y < 4 ? color(3 - x, 3 - y) : sentinel; });
        }},
        {"last-vertex Q from patch 0061 survives to pixels", [] {
            Fixture f;
            auto b = f.st(1, 2, 5, 2, 4, 1, 2);
            b.vertices[0].q = 8;
            f.render(b);
            f.frame([](unsigned x, unsigned y) { return x < 4 && y == 0 ? color(1 + x, 2) : sentinel; });
        }},
        {"bilinear sampling is unchanged", [] {
            Fixture f;
            auto b = f.st(1, 1, 1, 1);
            b.state.linearFilter = true;
            f.render(b);
            f.frame([](unsigned x, unsigned y) { return x == 0 && y == 0 ? 0x800c1010u : sentinel; });
        }},
        {"framebuffer alpha mask still applies after sampling", [] {
            Fixture f;
            auto b = f.st(-0.25f, 2, -0.25f, 2);
            b.state.context.frame.fbmsk = 0xff000000u;
            f.render(b);
            f.frame([](unsigned x, unsigned y) { return x == 0 && y == 0 ? ((sentinel & 0xff000000u) | (color(7, 2) & 0xffffffu)) : sentinel; });
        }},
        {"alpha rejection does not write sampled pixels", [] {
            Fixture f;
            auto b = f.st(-0.25f, 2, -0.25f, 2);
            b.state.context.test |= 1; // ATE, NEVER, KEEP.
            f.render(b);
            f.frame([](unsigned, unsigned) { return sentinel; });
        }},
        {"untextured and fully clipped sprites retain their behavior", [] {
            Fixture f;
            auto b = f.batch(); b.state.prim.tme = false;
            b.vertices[1].r = 9; b.vertices[1].g = 17; b.vertices[1].b = 25;
            f.render(b);
            f.frame([](unsigned x, unsigned y) { return x == 0 && y == 0 ? 0x80191109u : sentinel; });
            b = f.st(-0.25f, 2, -0.25f, 2); b.state.context.scissor.x0 = 3;
            f.render(b); f.frame([](unsigned, unsigned) { return sentinel; });
        }},
    };
    unsigned failed = 0;
    for (const auto &test : tests) {
        try { test.run(); std::cout << "PASS " << test.name << '\n'; }
        catch (const std::exception &error) {
            ++failed; std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
        }
    }
    std::cout << "RESULT groups=" << std::size(tests) << " failed=" << failed
              << " draws=" << draws << " pixel_checks=" << comparisons << '\n';
    return failed ? 1 : 0;
}
