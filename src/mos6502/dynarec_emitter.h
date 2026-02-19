#pragma once
#include <cstdint>
#include <cstddef>

// ARM instruction emitter for 6502 dynarec (ARMv5TE compatible)
// Emits ARM instructions to compile 6502 opcodes

namespace Dynarec {

// Forward declaration
struct DynarecState;

// ARM register allocation for 6502 state
// Callee-saved registers that persist across block execution
constexpr int REG_A      = 4;   // r4 = 6502 Accumulator
constexpr int REG_X      = 5;   // r5 = 6502 X index
constexpr int REG_Y      = 6;   // r6 = 6502 Y index
constexpr int REG_NZ     = 7;   // r7 = lazy NZ result byte (N=bit7, Z=(==0))
constexpr int REG_CARRY  = 8;   // r8 = carry flag (0 or 1)
constexpr int REG_RAM    = 9;   // r9 = RAM base pointer
constexpr int REG_ROM_LO = 10;  // r10 = ROM lo bank pointer
constexpr int REG_ROM_HI = 11;  // r11 = ROM hi bank pointer
constexpr int REG_STATE  = 12;  // r12 = DynarecState* pointer

// Scratch registers (caller-saved, used for temporaries)
constexpr int REG_SCRATCH0 = 0;  // r0
constexpr int REG_SCRATCH1 = 1;  // r1
constexpr int REG_SCRATCH2 = 2;  // r2
constexpr int REG_SCRATCH3 = 3;  // r3

// Condition codes
enum Cond : uint32_t {
    COND_EQ = 0x0,  // Equal (Z set)
    COND_NE = 0x1,  // Not equal (Z clear)
    COND_CS = 0x2,  // Carry set / unsigned higher or same
    COND_CC = 0x3,  // Carry clear / unsigned lower
    COND_MI = 0x4,  // Negative (N set)
    COND_PL = 0x5,  // Positive or zero (N clear)
    COND_VS = 0x6,  // Overflow set
    COND_VC = 0x7,  // Overflow clear
    COND_HI = 0x8,  // Unsigned higher
    COND_LS = 0x9,  // Unsigned lower or same
    COND_GE = 0xA,  // Signed greater or equal
    COND_LT = 0xB,  // Signed less than
    COND_GT = 0xC,  // Signed greater than
    COND_LE = 0xD,  // Signed less or equal
    COND_AL = 0xE,  // Always
};

// Data processing opcodes
enum DPOpcode : uint32_t {
    DP_AND = 0x0,
    DP_EOR = 0x1,
    DP_SUB = 0x2,
    DP_RSB = 0x3,
    DP_ADD = 0x4,
    DP_ADC = 0x5,
    DP_SBC = 0x6,
    DP_RSC = 0x7,
    DP_TST = 0x8,
    DP_TEQ = 0x9,
    DP_CMP = 0xA,
    DP_CMN = 0xB,
    DP_ORR = 0xC,
    DP_MOV = 0xD,
    DP_BIC = 0xE,
    DP_MVN = 0xF,
};

// PC-to-ARM-offset mapping for backward branch resolution
struct PCMapEntry {
    uint16_t pc6502;
    uint32_t arm_offset;  // Offset from code base
};

constexpr int MAX_PC_MAP = 128;

class Emitter {
public:
    uint8_t* ptr;
    uint8_t* const base;
    uint8_t* const end;
    int cycles;

    // PC mapping for branch resolution
    PCMapEntry pc_map[MAX_PC_MAP];
    int pc_map_count;

    Emitter(uint8_t* buf, size_t size)
        : ptr(buf), base(buf), end(buf + size), cycles(0), pc_map_count(0) {}

    // Get current code size
    size_t Size() const { return ptr - base; }

    // Get current ARM offset from base
    uint32_t CurrentOffset() const { return (uint32_t)(ptr - base); }

    // Check if space available
    bool CanEmit(size_t bytes) const { return ptr + bytes <= end; }

    // Emit raw instruction
    void Emit(uint32_t insn) {
        if (CanEmit(4)) {
            *(uint32_t*)ptr = insn;
            ptr += 4;
        }
    }

    // Record 6502 PC → ARM code offset mapping
    void RecordPCMap(uint16_t pc6502) {
        if (pc_map_count < MAX_PC_MAP) {
            pc_map[pc_map_count].pc6502 = pc6502;
            pc_map[pc_map_count].arm_offset = CurrentOffset();
            pc_map_count++;
        }
    }

    // Look up ARM offset for a 6502 PC (returns -1 if not found)
    int32_t GetARMOffset(uint16_t pc6502) const {
        for (int i = 0; i < pc_map_count; i++) {
            if (pc_map[i].pc6502 == pc6502) {
                return (int32_t)pc_map[i].arm_offset;
            }
        }
        return -1;
    }

    // Data processing instructions
    void Emit_MOV(int rd, int rm, bool set_flags = false);
    void Emit_MOV_IMM(int rd, uint8_t imm, bool set_flags = false);
    void Emit_MOV_IMM_ROT(int rd, uint8_t imm, uint8_t rotate, bool set_flags = false);

    // Load 16-bit immediate using MOV + ORR (ARMv5TE compatible)
    void LoadImm16(int rd, uint16_t val);

    // Load/store
    void Emit_LDR_IMM(int rt, int rn, uint16_t offset);
    void Emit_LDRB_IMM(int rt, int rn, uint16_t offset);
    void Emit_STR_IMM(int rt, int rn, uint16_t offset);
    void Emit_STRB_IMM(int rt, int rn, uint16_t offset);

    // Arithmetic
    void Emit_ADD_IMM(int rd, int rn, uint8_t imm, bool set_flags = false);
    void Emit_SUB_IMM(int rd, int rn, uint8_t imm, bool set_flags = false);
    void Emit_CMP_IMM(int rn, uint8_t imm);
    void Emit_TST_IMM(int rn, uint8_t imm);

    // Logical with immediate
    void Emit_AND_IMM(int rd, int rn, uint8_t imm, uint8_t rotate = 0, bool set_flags = false);
    void Emit_ORR_IMM(int rd, int rn, uint8_t imm, uint8_t rotate = 0, bool set_flags = false);
    void Emit_EOR_IMM(int rd, int rn, uint8_t imm, uint8_t rotate = 0, bool set_flags = false);

    // Branches
    void Emit_B(int32_t offset, Cond cond = COND_AL);
    void Emit_BX(int rm);

    // Load/store multiple
    void Emit_PUSH(uint16_t reg_list);
    void Emit_POP(uint16_t reg_list);

    // Block prologue/epilogue
    void Emit_Prologue();
    void Emit_Epilogue(uint16_t exit_pc);

    // Lazy flag helpers
    void Emit_UpdateNZ(int reg);  // MOV r7, reg (just copy result to NZ register)
};

// Instruction encoding helpers
constexpr uint32_t ARM_COND(Cond c) { return (uint32_t)c << 28; }

constexpr uint32_t ARM_DP(DPOpcode op, int rd, int rn, int rm, bool s = false) {
    return (0x0 << 26) | ((uint32_t)op << 21) | (s ? (1 << 20) : 0) |
           (rn << 16) | (rd << 12) | rm;
}

constexpr uint32_t ARM_DP_IMM(DPOpcode op, int rd, int rn, uint8_t imm, uint8_t rotate = 0, bool s = false) {
    return (0x1 << 25) | ((uint32_t)op << 21) | (s ? (1 << 20) : 0) |
           (rn << 16) | (rd << 12) | ((rotate & 0xF) << 8) | imm;
}

// LDR with pre-indexed, up, immediate offset: P=1, U=1, W=0, L=1
constexpr uint32_t ARM_LDR_IMM(int rt, int rn, uint16_t offset, bool byte = false) {
    return (0x1 << 26) | (1 << 24) | (1 << 23) | (byte ? (1 << 22) : 0) |
           (0 << 21) | (1 << 20) | (rn << 16) | (rt << 12) | (offset & 0xFFF);
}

// STR with pre-indexed, up, immediate offset: P=1, U=1, W=0, L=0
constexpr uint32_t ARM_STR_IMM(int rt, int rn, uint16_t offset, bool byte = false) {
    return (0x1 << 26) | (1 << 24) | (1 << 23) | (byte ? (1 << 22) : 0) |
           (0 << 21) | (0 << 20) | (rn << 16) | (rt << 12) | (offset & 0xFFF);
}

// Branch: cond already included in the offset word
constexpr uint32_t ARM_B(int32_t offset) {
    return (0xA << 24) | ((offset >> 2) & 0x00FFFFFF);
}

} // namespace Dynarec
