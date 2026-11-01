#pragma once
// Explicit source-slice test adapter, NOT the complete backend or texture cache.
// DrawSprite, SampleTexture, WritePixel and LookupCLUT are unmodified extracted
// production definitions. Only CT32 backing storage/lifecycle/dispatch are here.
#include "runtime/gs/gs_types.h"
#include "runtime/gs/ps2_gs_common.h"
#include "runtime/gs/ps2_gs_psmct32.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using namespace GSInternal;
#include "sprite_pixel_helpers.inc"

class GSCpuBackend
{
public:
    void Initialize(uint8_t *memory, uint32_t bytes)
    {
        if (!memory || bytes != 4u * 1024u * 1024u)
            throw std::runtime_error("slice requires owned 4 MiB storage");
        memory_ = memory;
    }
    void Submit(const GSPrimitiveBatch &batch)
    {
        if (batch.state.prim.type != GS_PRIM_SPRITE || batch.vertexCount != 2u)
            throw std::runtime_error("slice only supports two-vertex sprites");
        DrawSprite(batch);
    }
    void TextureFlush() {} // No texture cache in this explicit slice mode.
    uint32_t ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const
    { return ReadVramUnlocked(psm, base, bw, x, y); }
    void WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value)
    { WriteVramUnlocked(psm, base, bw, x, y, value); }

private:
    static uint32_t address(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y)
    {
        if (psm != GS_PSM_CT32)
            throw std::runtime_error("slice test reached an unsupported storage format");
        return GSPSMCT32::addrPSMCT32(base, bw, x, y) & (4u * 1024u * 1024u - 4u);
    }
    uint32_t ReadVramUnlocked(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const
    {
        uint32_t value;
        std::memcpy(&value, memory_ + address(psm, base, bw, x, y), sizeof(value));
        return value;
    }
    uint32_t ReadTextureVramUnlocked(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y)
    { return ReadVramUnlocked(psm, base, bw, x, y); }
    void WriteVramUnlocked(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value)
    { std::memcpy(memory_ + address(psm, base, bw, x, y), &value, sizeof(value)); }
    void DrawSprite(const GSPrimitiveBatch &batch);
    uint32_t SampleTexture(const GSDrawState &, float, float, float, uint16_t, uint16_t);
    uint32_t LookupCLUT(const GSDrawState &, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t);
    void WritePixel(const GSDrawState &, int, int, int, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t);
    uint8_t *memory_ = nullptr;
    std::array<uint16_t, 512> m_clut{};
};
#include "sprite_pixel_methods.inc"
