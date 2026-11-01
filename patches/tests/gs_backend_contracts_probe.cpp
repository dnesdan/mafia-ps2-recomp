// Exercise the production CPU backend and its actual VRAM, without game data.
#include "runtime/gs/gs_cpu_backend.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr uint32_t ramBytes = 4u * 1024u * 1024u;
size_t checks = 0;
void require(bool ok, const char *message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}

struct Fixture {
    std::vector<uint8_t> ram = std::vector<uint8_t>(ramBytes, 0xA5u);
    GSCpuBackend gs;
    Fixture() { gs.Initialize(ram.data(), static_cast<uint32_t>(ram.size())); }
    static GSTransferCommand command(uint32_t direction, uint8_t psm = GS_PSM_CT32,
                                     uint16_t width = 7, uint16_t height = 2) {
        GSTransferCommand c{};
        c.direction = direction;
        c.bitbltbuf = {32u, 2u, psm, 96u, 2u, psm};
        c.trxpos = {3u, 5u, 11u, 9u, 0u};
        c.trxreg = {width, height};
        return c;
    }
    void seed(const GSTransferCommand &c) {
        for (uint32_t i = 0; i < uint32_t(c.trxreg.rrw) * c.trxreg.rrh; ++i)
            gs.WriteVram(c.bitbltbuf.spsm, c.bitbltbuf.sbp, c.bitbltbuf.sbw,
                c.trxpos.ssax + i % c.trxreg.rrw, c.trxpos.ssay + i / c.trxreg.rrw,
                0xE1234567u + i * 0x01030507u);
    }
    void pending(bool partlyConsumed) {
        auto c = command(1u); seed(c); gs.BeginTransfer(c);
        if (partlyConsumed) {
            std::array<uint8_t, 5> out{};
            require(gs.ConsumeLocalToHostBytes(out.data(), out.size()) == out.size(), "initial partial read failed");
        }
        require(gs.GetTransferSnapshot().localToHostPendingBytes == (partlyConsumed ? 51u : 56u),
                "initial readback length differs");
    }
    void noPending() {
        require(gs.GetTransferSnapshot().localToHostPendingBytes == 0u, "retired readback remains pending");
        std::array<uint8_t, 73> out; out.fill(0xCCu);
        require(gs.ConsumeLocalToHostBytes(out.data() + 1, 71u) == 0u, "retired readback returned stale bytes");
        require(std::all_of(out.begin(), out.end(), [](uint8_t b) { return b == 0xCCu; }),
                "empty readback modified the output buffer");
    }
};

void replacement(uint32_t direction, bool partlyConsumed) {
    Fixture f; f.pending(partlyConsumed);
    auto c = Fixture::command(direction);
    f.gs.BeginTransfer(c);
    f.noPending();
    if (direction == 0u) {
        const std::array<uint32_t, 14> payload{0x11223344u, 0x55667788u};
        f.gs.UploadImage(reinterpret_cast<const uint8_t *>(payload.data()), sizeof(payload));
        require(f.gs.ReadVram(GS_PSM_CT32, 96u, 2u, 11u, 9u) == payload[0], "new upload lost its first pixel");
    } else if (direction == 2u) {
        for (uint32_t i = 0; i < 14; ++i)
            require(f.gs.ReadVram(GS_PSM_CT32, 96u, 2u, 11u + i % 7, 9u + i / 7) ==
                    f.gs.ReadVram(GS_PSM_CT32, 32u, 2u, 3u + i % 7, 5u + i / 7),
                    "replacement local copy differs from VRAM source");
    }
}

void packedRoundTrips() {
    const std::array<std::pair<uint8_t, unsigned>, 13> formats{{
        {GS_PSM_CT32,32}, {GS_PSM_Z32,32}, {GS_PSM_CT24,24}, {GS_PSM_Z24,24},
        {GS_PSM_CT16,16}, {GS_PSM_CT16S,16}, {GS_PSM_Z16,16}, {GS_PSM_Z16S,16},
        {GS_PSM_T8,8}, {GS_PSM_T8H,8}, {GS_PSM_T4,4}, {GS_PSM_T4HL,4}, {GS_PSM_T4HH,4}}};
    for (auto [psm, bits] : formats) for (unsigned chunk : {1u, 3u, 8u, 16u, 71u}) {
        Fixture f;
        auto c = Fixture::command(1u, psm, 7, 3); f.seed(c);
        std::vector<uint8_t> expected((21u * bits + 7u) / 8u, 0u);
        const uint32_t mask = bits == 32 ? UINT32_MAX : (uint32_t{1} << bits) - 1;
        for (uint32_t i = 0; i < 21u; ++i) {
            const uint32_t value = (0xE1234567u + i * 0x01030507u) & mask;
            if (bits == 4) expected[i / 2] |= static_cast<uint8_t>(value << (4u * (i % 2)));
            else for (unsigned b = 0; b < bits / 8; ++b)
                expected[i * (bits / 8) + b] = static_cast<uint8_t>(value >> (8u * b));
        }
        f.gs.BeginTransfer(c);
        std::vector<uint8_t> output(expected.size() + 2, 0xCCu);
        size_t read = 0;
        while (read < expected.size()) {
            const uint32_t wanted = static_cast<uint32_t>(std::min<size_t>(chunk, expected.size() - read));
            require(f.gs.ConsumeLocalToHostBytes(output.data() + 1 + read, wanted) == wanted, "short packed readback");
            read += wanted;
            require(f.gs.GetTransferSnapshot().localToHostPendingBytes == expected.size() - read,
                    "partial packed-read progress differs");
        }
        require(std::equal(expected.begin(), expected.end(), output.begin() + 1), "packed readback pixels differ");
        require(output.front() == 0xCCu && output.back() == 0xCCu, "packed read crossed canaries");
        f.noPending();
    }
}

void framebufferClear(uint8_t psm, bool forceAlpha) {
    Fixture f;
    GSContext context{};
    context.frame = {3u, 1u, psm, 0u};
    context.scissor = {5u, 7u, 9u, 10u};
    context.zbuf.psm = GS_PSM_Z32;
    context.zbuf.zmask = true;
    context.test = (1ull << 16) | (1ull << 17); // Z ALWAYS, no writes.
    context.fba = forceAlpha ? 1u : 0u;
    for (uint32_t rgb : {0x000000u, 0xFFFFFFu, 0x123456u, 0xF80880u, 0x0787F7u})
        for (uint32_t alpha = 0u; alpha < 256u; ++alpha) {
            const uint32_t rgba = rgb | (alpha << 24u);
            const uint32_t effectiveAlpha = alpha | (forceAlpha ? 0x80u : 0u);
            const uint32_t expected = (psm == GS_PSM_CT16 || psm == GS_PSM_CT16S)
                ? ((rgb >> 3u) & 31u) | (((rgb >> 11u) & 31u) << 5u) |
                  (((rgb >> 19u) & 31u) << 10u) | ((effectiveAlpha >> 7u) << 15u)
                : psm == GS_PSM_CT24 ? rgb : rgb | (effectiveAlpha << 24u);
            require(f.gs.ClearFramebuffer(context, rgba), "valid framebuffer clear was rejected");
            for (uint32_t y = 8; y <= 11; ++y) for (uint32_t x = 4; x <= 8; ++x) {
                const bool inside = x >= 5 && x <= 7 && y >= 9 && y <= 10;
                const uint32_t sentinel = psm == GS_PSM_CT32 ? 0xA5A5A5A5u :
                                          psm == GS_PSM_CT24 ? 0xA5A5A5u : 0xA5A5u;
                require(f.gs.ReadVram(psm, 96u, 1u, x, y) == (inside ? expected : sentinel),
                        "framebuffer clear alpha/color or scissor canary differs");
            }
            if (psm == GS_PSM_CT24)
                require((f.gs.ReadVram(GS_PSM_CT32, 96u, 1u, 5u, 9u) >> 24u) == 0xA5u,
                        "RGB24 clear changed its unused high byte");
            GSPrimitiveBatch b{};
            b.vertexCount = 1; b.state.prim.type = GS_PRIM_POINT; b.state.context = context;
            auto &v = b.vertices[0]; v.x = 5; v.y = 9;
            v.r = static_cast<uint8_t>(rgba); v.g = static_cast<uint8_t>(rgba >> 8u);
            v.b = static_cast<uint8_t>(rgba >> 16u); v.a = static_cast<uint8_t>(alpha);
            f.gs.Submit(b);
            require(f.gs.ReadVram(psm, 96u, 1u, 5u, 9u) == expected,
                    "point rasterization and clear encoding disagree");
        }
}

}

int main() {
    unsigned groups = 0, failed = 0;
    const auto test = [&](const std::string &name, const std::function<void()> &fn) {
        ++groups;
        try { fn(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception &e) { ++failed; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
    };
    for (uint32_t direction : {0u, 2u, 3u}) for (bool partial : {false, true})
        test("direction " + std::to_string(direction) + (partial ? " retires partial readback" : " retires unread readback"),
             [=] { replacement(direction, partial); });
    test("replacement readback owns new pixels and resets its cursor", [] {
        Fixture f; f.pending(true);
        auto c = Fixture::command(1u, GS_PSM_CT24, 1, 1); f.seed(c); f.gs.BeginTransfer(c);
        std::array<uint8_t, 9> out; out.fill(0xCCu);
        require(f.gs.ConsumeLocalToHostBytes(out.data(), out.size()) == 3u, "replacement retained old length/cursor");
        require(out[0] == 0x67u && out[1] == 0x45u && out[2] == 0x23u && out[3] == 0xCCu, "replacement bytes differ");
        f.noPending();
    });
    test("empty transfers retire old readbacks", [] {
        for (uint32_t direction : {0u,1u,2u,3u}) for (bool zeroWidth : {false,true}) {
            Fixture f; f.pending(true); auto c = Fixture::command(direction);
            if (zeroWidth) c.trxreg.rrw = 0; else c.trxreg.rrh = 0;
            f.gs.BeginTransfer(c); f.noPending();
        }
    });
    test("reset and reinitialization retire readbacks", [] {
        for (bool initialize : {false,true}) {
            Fixture f; f.pending(true);
            if (initialize) f.gs.Initialize(f.ram.data(), f.ram.size()); else f.gs.Reset();
            f.noPending();
        }
    });
    test("no-op reads and synchronization preserve pending bytes", [] {
        Fixture f; f.pending(true); uint8_t byte = 0;
        require(f.gs.ConsumeLocalToHostBytes(nullptr, 9u) == 0u, "null destination was accepted");
        require(f.gs.ConsumeLocalToHostBytes(&byte, 0u) == 0u, "zero request consumed bytes");
        f.gs.Flush(); f.gs.TextureFlush(); f.gs.Sync(GSSyncReason::LocalToHost);
        require(f.gs.GetTransferSnapshot().localToHostPendingBytes == 51u, "non-transfer operation discarded readback");
    });
    test("readback ownership is per backend", [] {
        Fixture a, b; a.pending(true); b.pending(false);
        a.gs.BeginTransfer(Fixture::command(3u)); a.noPending();
        require(b.gs.GetTransferSnapshot().localToHostPendingBytes == 56u, "retiring one device changed another");
        uint8_t byte = 0; require(b.gs.ConsumeLocalToHostBytes(&byte, 1u) == 1u && byte == 0x67u, "peer readback changed");
    });
    test("repeated retire/start cycles do not retain old bytes", [] {
        Fixture f;
        for (unsigned i = 0; i < 100; ++i) {
            f.pending((i & 1u) != 0u); f.gs.BeginTransfer(Fixture::command(3u)); f.noPending();
        }
    });
    test("all 13 VRAM formats preserve packed readback across chunk sizes", packedRoundTrips);
    test("CT16 clear preserves the original alpha MSB for all alpha values", [] { framebufferClear(GS_PSM_CT16, false); });
    test("CT16S clear preserves the original alpha MSB for all alpha values", [] { framebufferClear(GS_PSM_CT16S, false); });
    test("FBA forces alpha consistently in both 16-bit clear layouts", [] {
        framebufferClear(GS_PSM_CT16, true); framebufferClear(GS_PSM_CT16S, true);
    });
    test("32-bit clears preserve RGBA and FBA", [] {
        framebufferClear(GS_PSM_CT32, false); framebufferClear(GS_PSM_CT32, true);
    });
    test("24-bit clears retain the unused lane with and without FBA", [] {
        framebufferClear(GS_PSM_CT24, false); framebufferClear(GS_PSM_CT24, true);
    });
    test("16-bit clear alpha controls subsequent destination-alpha tests", [] {
        for (auto psm : {GS_PSM_CT16, GS_PSM_CT16S}) for (bool fba : {false,true})
            for (bool datm : {false,true}) {
                Fixture f;
                GSPrimitiveBatch b{}; b.vertexCount = 1; b.state.prim.type = GS_PRIM_POINT;
                auto &c = b.state.context;
                c.frame = {3u, 1u, static_cast<uint8_t>(psm), 0u}; c.scissor = {0u, 0u, 0u, 0u};
                c.zbuf.psm = GS_PSM_Z32; c.zbuf.zmask = true; c.fba = fba;
                c.test = (1ull << 16) | (1ull << 17) | (1ull << 14) | (uint64_t(datm) << 15);
                b.vertices[0].r = 255; b.vertices[0].a = 128;
                for (uint32_t alpha = 0; alpha < 256; ++alpha) {
                    require(f.gs.ClearFramebuffer(c, 0x00123456u | (alpha << 24)), "DATE setup clear failed");
                    const uint32_t alphaBit = (alpha | (fba ? 128u : 0u)) >> 7;
                    const uint32_t before = (0x56u >> 3) | ((0x34u >> 3) << 5) |
                                            ((0x12u >> 3) << 10) | (alphaBit << 15);
                    f.gs.Submit(b);
                    require(f.gs.ReadVram(psm, 96u, 1u, 0u, 0u) == (alphaBit == uint32_t(datm) ? 0x801Fu : before),
                            "clear alpha produced the wrong visible DATE result");
                }
            }
    });
    std::cout << "RESULT groups=" << groups << " failed=" << failed << " checks=" << checks << '\n';
    return failed ? 1 : 0;
}
