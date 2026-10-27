#include "ps2_runtime.h"

// Authored test program: LUI v0,0x1234; ORI v0,v0,0x5678; JR ra; NOP.
// It deliberately returns to the unset root RA, which must fail closed. This
// fixture has no commercial instructions and is never linked into MafiaNative.
namespace {
void synthetic_entry(std::uint8_t *ram, R5900Context *ctx, PS2Runtime *runtime) {
    ctx->r[2] = _mm_set_epi64x(0, 0x12345678);
    const auto target = static_cast<std::uint32_t>(_mm_cvtsi128_si32(ctx->r[31]));
    ctx->branch_pc = 0x00100008u;
    ctx->in_delay_slot = false;
    ctx->pc = target;
    runtime->dispatchGuestBranch(ram, ctx, target, 0x00100008u, 0u,
                                PS2Runtime::GuestBranchKind::Return, "synthetic JR $ra");
}
}

extern const std::uint32_t g_ps2RecompiledFunctionTableBase = 0x00100000u;
extern const std::uint32_t g_ps2RecompiledFunctionTableEnd = 0x00100010u;
extern const std::uint32_t g_ps2RecompiledFunctionTableSlotCount = 4u;
PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[4]{
    synthetic_entry, synthetic_entry, synthetic_entry, synthetic_entry};
