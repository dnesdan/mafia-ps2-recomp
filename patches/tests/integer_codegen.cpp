// SPDX-License-Identifier: GPL-3.0-or-later
// Emit a host test from the actual upstream instruction translator.
#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <iostream>

using namespace ps2recomp;

int main()
{
    CodeGenerator generator({}, {});
    std::cout << R"CPP(
#include "ps2_runtime_macros.h"
#include <array>
#include <bit>
#include <cstdint>
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
    struct Branch { const char *name; uint32_t opcode, rt, comparison; };
    const std::array<Branch, 12> branches{{
        {"blez", OPCODE_BLEZ, 0, 0}, {"blezl", OPCODE_BLEZL, 0, 0},
        {"bgtz", OPCODE_BGTZ, 0, 1}, {"bgtzl", OPCODE_BGTZL, 0, 1},
        {"bltz", OPCODE_REGIMM, REGIMM_BLTZ, 2}, {"bltzl", OPCODE_REGIMM, REGIMM_BLTZL, 2},
        {"bltzal", OPCODE_REGIMM, REGIMM_BLTZAL, 2}, {"bltzall", OPCODE_REGIMM, REGIMM_BLTZALL, 2},
        {"bgez", OPCODE_REGIMM, REGIMM_BGEZ, 3}, {"bgezl", OPCODE_REGIMM, REGIMM_BGEZL, 3},
        {"bgezal", OPCODE_REGIMM, REGIMM_BGEZAL, 3}, {"bgezall", OPCODE_REGIMM, REGIMM_BGEZALL, 3},
    }};
    for (const auto &branch : branches) {
        Function function{};
        function.name = branch.name; function.start = 0x1000; function.end = 0x1010;
        function.isRecompiled = true;
        Instruction instruction{};
        instruction.address = 0x1000; instruction.opcode = branch.opcode; instruction.rs = 4;
        instruction.rt = branch.rt; instruction.simmediate = 2; instruction.hasDelaySlot = true;
        instruction.isBranch = true; instruction.raw = (branch.opcode << 26) | (4u << 21) | (branch.rt << 16) | 2u;
        Instruction delay{}; delay.address = 0x1004; delay.opcode = OPCODE_ADDIU; delay.rt = 0;
        Instruction fallthrough = delay; fallthrough.address = 0x1008;
        Instruction target = delay; target.address = 0x100c;
        CodeGenerator generator({}, {});
        const auto code = generator.generateFunction(function, {instruction, delay, fallthrough, target}, false);
        const std::string marker = "const bool branch_taken_0x1000 = (";
        const auto start = code.find(marker);
        if (start == std::string::npos) throw std::runtime_error("branch expression missing: " + std::string(branch.name));
        const auto begin = start + marker.size();
        const auto end = code.find(");", begin);
        if (end == std::string::npos) throw std::runtime_error("branch expression unterminated");
        std::cout << "bool " << branch.name << "(R5900Context* ctx) {return (" << code.substr(begin, end - begin) << ");}\n";
    }
    std::cout << R"CPP(
using Predicate = bool (*)(R5900Context *);
struct BranchCase { const char *name; Predicate fn; unsigned comparison; };
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
    const std::array<BranchCase,12> cases{{
        {"blez",blez,0}, {"blezl",blezl,0}, {"bgtz",bgtz,1}, {"bgtzl",bgtzl,1},
        {"bltz",bltz,2}, {"bltzl",bltzl,2}, {"bltzal",bltzal,2}, {"bltzall",bltzall,2},
        {"bgez",bgez,3}, {"bgezl",bgezl,3}, {"bgezal",bgezal,3}, {"bgezall",bgezall,3}}};
    const std::array<uint64_t,10> branchValues{{0,1,0x7fffffffull,0x80000000ull,0xffffffffull,
        0x100000000ull,0xffffffff00000000ull,0xffffffff7fffffffull,0x8000000000000000ull,0x7fffffffffffffffull}};
    unsigned failed=0;
    for (const auto &c:cases) for (uint64_t v:branchValues) {
        R5900Context ctx{};
        // Put deliberately different bits in the 128-bit register's upper half.
        ctx.r[4] = _mm_set_epi64x(std::bit_cast<int64_t>(~v),std::bit_cast<int64_t>(v));
        const int64_t signedValue=std::bit_cast<int64_t>(v);
        bool expected=c.comparison==0 ? signedValue<=0 : c.comparison==1 ? signedValue>0 :
                      c.comparison==2 ? signedValue<0 : signedValue>=0;
        if(c.fn(&ctx)!=expected) { std::cerr << c.name << " incorrect for 0x" << std::hex << v << '\n'; ++failed; }
    }
    if (failed) {std::cerr << std::dec << failed << " of 120 signed-64 branch comparisons failed\n"; return 1;}
    std::cout << "68 integer arithmetic cases and 120 signed-64 branch comparisons passed\n";
}
)CPP";
}
