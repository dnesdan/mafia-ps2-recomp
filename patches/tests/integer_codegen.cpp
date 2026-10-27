// SPDX-License-Identifier: GPL-3.0-or-later
// Emit a host test from the actual upstream instruction translator.
#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"
#include <cstdint>
#include <iostream>

using namespace ps2recomp;

int main()
{
    CodeGenerator generator({}, {});
    std::cout << R"CPP(
#include "ps2_runtime_macros.h"
#include <cstdlib>
#include <iostream>
struct TrapRuntime {
    bool trapped = false;
    void SignalException(R5900Context *, PS2Exception) { trapped = true; }
};
)CPP";
    auto emit = [&](const char *name, uint32_t opcode, uint32_t function, int16_t imm) {
        Instruction inst{};
        inst.opcode = opcode;
        inst.function = function;
        inst.rs = 4;
        inst.rt = 5;
        inst.rd = 6;
        inst.simmediate = static_cast<uint32_t>(static_cast<int32_t>(imm));
        inst.immediate = static_cast<uint16_t>(imm);
        std::cout << "void " << name << "(R5900Context *ctx, TrapRuntime *runtime) {\n"
                  << generator.translateInstruction(inst) << "\n}\n";
    };
    emit("add64", OPCODE_SPECIAL, SPECIAL_DADD, 0);
    emit("sub64", OPCODE_SPECIAL, SPECIAL_DSUB, 0);
    emit("addi64", OPCODE_DADDI, 0, 1);
    emit("addi64_negative", OPCODE_DADDI, 0, -1);
    emit("addiu64", OPCODE_DADDIU, 0, 1);
    emit("addiu64_negative", OPCODE_DADDIU, 0, -1);
    std::cout << R"CPP(
using Op = void (*)(R5900Context *, TrapRuntime *);
void check(Op op, uint64_t a, uint64_t b, int destination, uint64_t expected, bool overflow) {
    R5900Context ctx{};
    TrapRuntime runtime;
    const uint64_t sentinel = 0x1122334455667788ull;
    SET_GPR_U64(&ctx, destination, sentinel);
    SET_GPR_U64(&ctx, 4, a);
    if (destination != 5) SET_GPR_U64(&ctx, 5, b);
    op(&ctx, &runtime);
    if (runtime.trapped != overflow || GPR_U64((&ctx), destination) != (overflow ? sentinel : expected)) {
        std::cerr << "wrong overflow/result for " << std::hex << a << ", " << b << '\n';
        std::exit(1);
    }
}
int main() {
    constexpr uint64_t high = 0x7fffffffffffffffull;
    constexpr uint64_t low = 0x8000000000000000ull;
    constexpr uint64_t minus1 = 0xffffffffffffffffull;
    check(add64, high, 1, 6, 0, true);
    check(add64, low, minus1, 6, 0, true);
    check(add64, low, high, 6, minus1, false);
    check(add64, high, minus1, 6, high - 1, false);
    check(add64, 1, minus1, 6, 0, false);
    check(sub64, low, 1, 6, 0, true);
    check(sub64, high, minus1, 6, 0, true);
    check(sub64, low, minus1, 6, low + 1, false);
    check(sub64, minus1, minus1, 6, 0, false);
    check(sub64, low, low, 6, 0, false);
    check(addi64, high, 0, 5, 0, true);
    check(addi64, minus1, 0, 5, 0, false);
    check(addi64_negative, low, 0, 5, 0, true);
    check(addi64_negative, 0, 0, 5, minus1, false);
    check(addiu64, high, 0, 5, low, false);
    check(addiu64, minus1, 0, 5, 0, false);
    check(addiu64_negative, low, 0, 5, high, false);
    check(addiu64_negative, 0, 0, 5, minus1, false);
    const uint32_t values[] = {0u, 1u, 0x7fffffffu, 0x80000000u, 0xffffffffu};
    for (uint32_t a : values) for (uint32_t b : values) {
        const int64_t sa = static_cast<int32_t>(a), sb = static_cast<int32_t>(b);
        uint32_t result;
        bool overflow;
        ADD32_OV(a, b, result, overflow);
        if (result != static_cast<uint32_t>(sa + sb) || overflow != (sa + sb > INT32_MAX || sa + sb < INT32_MIN)) return 1;
        SUB32_OV(a, b, result, overflow);
        if (result != static_cast<uint32_t>(sa - sb) || overflow != (sa - sb > INT32_MAX || sa - sb < INT32_MIN)) return 1;
    }
    std::cout << "68 integer arithmetic cases passed\n";
}
)CPP";
}
