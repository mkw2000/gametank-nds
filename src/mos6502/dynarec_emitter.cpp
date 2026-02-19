#include "dynarec_emitter.h"
#include "dynarec.h"

namespace Dynarec {

// MOV Rd, Rm
void Emitter::Emit_MOV(int rd, int rm, bool set_flags) {
    Emit(ARM_COND(COND_AL) | ARM_DP(DP_MOV, rd, 0, rm, set_flags));
}

// MOV Rd, #imm
void Emitter::Emit_MOV_IMM(int rd, uint8_t imm, bool set_flags) {
    Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_MOV, rd, 0, imm, 0, set_flags));
}

// MOV Rd, #imm, ROR #(rotate*2)
void Emitter::Emit_MOV_IMM_ROT(int rd, uint8_t imm, uint8_t rotate, bool set_flags) {
    Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_MOV, rd, 0, imm, rotate, set_flags));
}

// Load 16-bit immediate into rd using MOV + ORR (ARMv5TE compatible)
// val = (hi8 << 8) | lo8
// MOV rd, #lo8
// ORR rd, rd, #hi8, ROR #24  (rotate right 24 = shift left 8)
void Emitter::LoadImm16(int rd, uint16_t val) {
    uint8_t lo = val & 0xFF;
    uint8_t hi = (val >> 8) & 0xFF;
    Emit_MOV_IMM(rd, lo);
    if (hi != 0) {
        // ORR rd, rd, #hi, ROR #24 (rotate field = 12, means ROR by 24)
        Emit_ORR_IMM(rd, rd, hi, 12);
    }
}

// LDR Rt, [Rn, #offset]
void Emitter::Emit_LDR_IMM(int rt, int rn, uint16_t offset) {
    Emit(ARM_COND(COND_AL) | ARM_LDR_IMM(rt, rn, offset, false));
}

// LDRB Rt, [Rn, #offset]
void Emitter::Emit_LDRB_IMM(int rt, int rn, uint16_t offset) {
    Emit(ARM_COND(COND_AL) | ARM_LDR_IMM(rt, rn, offset, true));
}

// STR Rt, [Rn, #offset]
void Emitter::Emit_STR_IMM(int rt, int rn, uint16_t offset) {
    Emit(ARM_COND(COND_AL) | ARM_STR_IMM(rt, rn, offset, false));
}

// STRB Rt, [Rn, #offset]
void Emitter::Emit_STRB_IMM(int rt, int rn, uint16_t offset) {
    Emit(ARM_COND(COND_AL) | ARM_STR_IMM(rt, rn, offset, true));
}

// LDRB Rd, [Rn, Rm] - register offset
void Emitter::Emit_LDRB_REG(int rd, int rn, int rm) {
    Emit(ARM_COND(COND_AL) | (0x1 << 26) | (1 << 25) | (1 << 24) | (1 << 23) |
         (1 << 22) | (0 << 21) | (1 << 20) | (rn << 16) | (rd << 12) | rm);
}

// STRB Rd, [Rn, Rm] - register offset
void Emitter::Emit_STRB_REG(int rd, int rn, int rm) {
    Emit(ARM_COND(COND_AL) | (0x1 << 26) | (1 << 25) | (1 << 24) | (1 << 23) |
         (1 << 22) | (0 << 21) | (0 << 20) | (rn << 16) | (rd << 12) | rm);
}

// ADD Rd, Rn, #imm
void Emitter::Emit_ADD_IMM(int rd, int rn, uint8_t imm, bool set_flags) {
    Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_ADD, rd, rn, imm, 0, set_flags));
}

// SUB Rd, Rn, #imm
void Emitter::Emit_SUB_IMM(int rd, int rn, uint8_t imm, bool set_flags) {
    Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_SUB, rd, rn, imm, 0, set_flags));
}

// CMP Rn, #imm (sets flags)
void Emitter::Emit_CMP_IMM(int rn, uint8_t imm) {
    Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_CMP, 0, rn, imm, 0, true));
}

// TST Rn, #imm (sets flags)
void Emitter::Emit_TST_IMM(int rn, uint8_t imm) {
    Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_TST, 0, rn, imm, 0, true));
}

// AND Rd, Rn, #imm, ROR #(rotate*2)
void Emitter::Emit_AND_IMM(int rd, int rn, uint8_t imm, uint8_t rotate, bool set_flags) {
    Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_AND, rd, rn, imm, rotate, set_flags));
}

// ORR Rd, Rn, #imm, ROR #(rotate*2)
void Emitter::Emit_ORR_IMM(int rd, int rn, uint8_t imm, uint8_t rotate, bool set_flags) {
    Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_ORR, rd, rn, imm, rotate, set_flags));
}

// EOR Rd, Rn, #imm, ROR #(rotate*2)
void Emitter::Emit_EOR_IMM(int rd, int rn, uint8_t imm, uint8_t rotate, bool set_flags) {
    Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_EOR, rd, rn, imm, rotate, set_flags));
}

// B <offset> with condition code
// offset is relative to current PC (which is +8 ahead due to pipeline)
void Emitter::Emit_B(int32_t offset, Cond cond) {
    Emit(ARM_COND(cond) | ARM_B(offset));
}

// BX Rm
void Emitter::Emit_BX(int rm) {
    // 1110 0001 0010 1111 1111 1111 0001 Rm
    Emit(0xE12FFF10 | rm);
}

// PUSH {reg_list} = STMDB SP!, {reg_list}
// Encoding: cond 100 1 0 0 1 0 Rn=SP reg_list
// bits [27:25] = 100, P=1(24), U=0(23), S=0(22), W=1(21), L=0(20)
void Emitter::Emit_PUSH(uint16_t reg_list) {
    uint32_t insn = (0xE << 28) | (1 << 27) | (0 << 26) | (0 << 25) |
                    (1 << 24) | (0 << 23) | (0 << 22) | (1 << 21) | (0 << 20) |
                    (13 << 16) | reg_list;
    Emit(insn);
}

// POP {reg_list} = LDMIA SP!, {reg_list}
// Encoding: cond 100 0 1 0 1 1 Rn=SP reg_list
// bits [27:25] = 100, P=0(24), U=1(23), S=0(22), W=1(21), L=1(20)
void Emitter::Emit_POP(uint16_t reg_list) {
    uint32_t insn = (0xE << 28) | (1 << 27) | (0 << 26) | (0 << 25) |
                    (0 << 24) | (1 << 23) | (0 << 22) | (1 << 21) | (1 << 20) |
                    (13 << 16) | reg_list;
    Emit(insn);
}

// Block prologue: save callee-saved regs, load 6502 state from DynarecState*
// r0 = DynarecState* on entry (C calling convention)
void Emitter::Emit_Prologue() {
    // PUSH {r4-r12, lr}
    // reg_list: r4(bit4)..r12(bit12), lr(bit14) = 0x5FF0
    Emit_PUSH(0x5FF0);

    // Save DynarecState* to r12 (REG_STATE)
    Emit_MOV(REG_STATE, 0);  // MOV r12, r0

    // Load pointers from DynarecState
    Emit_LDR_IMM(REG_RAM,    REG_STATE, DS_RAM);      // r9  = state->ram
    Emit_LDR_IMM(REG_ROM_LO, REG_STATE, DS_ROM_LO);   // r10 = state->rom_lo
    Emit_LDR_IMM(REG_ROM_HI, REG_STATE, DS_ROM_HI);   // r11 = state->rom_hi

    // Load 6502 registers from DynarecState
    Emit_LDRB_IMM(REG_A, REG_STATE, DS_A);   // r4 = state->A
    Emit_LDRB_IMM(REG_X, REG_STATE, DS_X);   // r5 = state->X
    Emit_LDRB_IMM(REG_Y, REG_STATE, DS_Y);   // r6 = state->Y

    // Unpack lazy NZ from status register:
    // Default: r7 = 0x01 (non-zero, non-negative)
    // If N flag set: r7 = 0x80
    // If Z flag set: r7 = 0x00 (overrides N)
    Emit_LDRB_IMM(REG_SCRATCH0, REG_STATE, DS_STATUS);  // r0 = status

    // Start with default value
    Emit_MOV_IMM(REG_NZ, 0x01);
    // If N set (bit 7): MOV r7, #0x80
    Emit_TST_IMM(REG_SCRATCH0, 0x80);
    Emit(ARM_COND(COND_NE) | ARM_DP_IMM(DP_MOV, REG_NZ, 0, 0x80, 0, false));
    // If Z set (bit 1): MOV r7, #0 — this overrides N, which is correct
    // (6502 can have both N and Z set, but Z takes priority for our lazy scheme)
    Emit_TST_IMM(REG_SCRATCH0, 0x02);
    Emit(ARM_COND(COND_NE) | ARM_DP_IMM(DP_MOV, REG_NZ, 0, 0x00, 0, false));

    // Unpack carry flag (bit 0 of status)
    Emit_AND_IMM(REG_CARRY, REG_SCRATCH0, 0x01);  // r8 = status & 1
}

// Block epilogue: store 6502 state back to DynarecState, return
// exit_pc = the 6502 PC to store as the exit point
void Emitter::Emit_Epilogue(uint16_t exit_pc) {
    // Store A, X, Y back to DynarecState
    Emit_STRB_IMM(REG_A, REG_STATE, DS_A);
    Emit_STRB_IMM(REG_X, REG_STATE, DS_X);
    Emit_STRB_IMM(REG_Y, REG_STATE, DS_Y);

    // Repack lazy NZ + carry into 6502 status byte
    // Load current status to preserve I/D/V/B bits
    Emit_LDRB_IMM(REG_SCRATCH0, REG_STATE, DS_STATUS);  // r0 = old status

    // Clear N, Z, C bits: BIC r0, r0, #0x83
    Emit_AND_IMM(REG_SCRATCH0, REG_SCRATCH0, 0x7C);  // keep bits 6-2 (V,B,D,I)

    // Set C from r8 (carry is 0 or 1): ORR r0, r0, r8
    Emit(ARM_COND(COND_AL) | ARM_DP(DP_ORR, REG_SCRATCH0, REG_SCRATCH0, REG_CARRY, false));

    // Set Z: if r7 == 0, set Z bit (0x02)
    Emit_TST_IMM(REG_NZ, 0xFF);
    // If zero: ORR r0, r0, #0x02
    Emit(ARM_COND(COND_EQ) | ARM_DP_IMM(DP_ORR, REG_SCRATCH0, REG_SCRATCH0, 0x02, 0, false));

    // Set N: if r7 bit 7 set, set N bit (0x80)
    // We already did TST r7, #0xFF above - check N flag from that
    // Actually need a separate test for bit 7
    Emit_TST_IMM(REG_NZ, 0x80);
    Emit(ARM_COND(COND_NE) | ARM_DP_IMM(DP_ORR, REG_SCRATCH0, REG_SCRATCH0, 0x80, 0, false));

    // Store repacked status
    Emit_STRB_IMM(REG_SCRATCH0, REG_STATE, DS_STATUS);

    // Store exit PC
    LoadImm16(REG_SCRATCH0, exit_pc);
    // STRH r0, [r12, #DS_PC] - store halfword
    // ARM encoding: cond 000 P U 1 W 0 Rn Rt imm4H 1011 imm4L
    // P=1, U=1, W=0, L=0 (store)
    {
        uint8_t offset_hi = (DS_PC >> 4) & 0xF;
        uint8_t offset_lo = DS_PC & 0xF;
        uint32_t insn = (0xE << 28) | (0x1 << 24) | (1 << 23) | (1 << 22) |
                        (0 << 21) | (0 << 20) |
                        (REG_STATE << 16) | (REG_SCRATCH0 << 12) |
                        (offset_hi << 8) | (0xB << 4) | offset_lo;
        Emit(insn);
    }

    // Store cycles_executed
    if (cycles <= 255) {
        Emit_MOV_IMM(REG_SCRATCH0, (uint8_t)cycles);
    } else {
        LoadImm16(REG_SCRATCH0, (uint16_t)cycles);
    }
    Emit_STR_IMM(REG_SCRATCH0, REG_STATE, DS_CYCLES_EXEC);

    // POP {r4-r12, pc} - return
    Emit_POP(0x9FF0);  // r4-r12(bits 4-12) + pc(bit 15) = 0x9FF0
}

// Epilogue variant: exit PC is in a register (for RTS etc)
// IMPORTANT: pc_reg must NOT be REG_SCRATCH0 — flag repacking clobbers it
void Emitter::Emit_Epilogue_DynamicPC(int pc_reg) {
    // Store A, X, Y back to DynarecState
    Emit_STRB_IMM(REG_A, REG_STATE, DS_A);
    Emit_STRB_IMM(REG_X, REG_STATE, DS_X);
    Emit_STRB_IMM(REG_Y, REG_STATE, DS_Y);

    // Repack lazy NZ + carry into 6502 status byte
    Emit_LDRB_IMM(REG_SCRATCH0, REG_STATE, DS_STATUS);
    Emit_AND_IMM(REG_SCRATCH0, REG_SCRATCH0, 0x7C);  // keep V,B,D,I
    Emit(ARM_COND(COND_AL) | ARM_DP(DP_ORR, REG_SCRATCH0, REG_SCRATCH0, REG_CARRY, false));
    Emit_TST_IMM(REG_NZ, 0xFF);
    Emit(ARM_COND(COND_EQ) | ARM_DP_IMM(DP_ORR, REG_SCRATCH0, REG_SCRATCH0, 0x02, 0, false));
    Emit_TST_IMM(REG_NZ, 0x80);
    Emit(ARM_COND(COND_NE) | ARM_DP_IMM(DP_ORR, REG_SCRATCH0, REG_SCRATCH0, 0x80, 0, false));
    Emit_STRB_IMM(REG_SCRATCH0, REG_STATE, DS_STATUS);

    // Store exit PC from register (STRH pc_reg, [r12, #DS_PC])
    {
        uint8_t offset_hi = (DS_PC >> 4) & 0xF;
        uint8_t offset_lo = DS_PC & 0xF;
        uint32_t insn = (0xE << 28) | (0x1 << 24) | (1 << 23) | (1 << 22) |
                        (0 << 21) | (0 << 20) |
                        (REG_STATE << 16) | (pc_reg << 12) |
                        (offset_hi << 8) | (0xB << 4) | offset_lo;
        Emit(insn);
    }

    // Store cycles_executed
    if (cycles <= 255) {
        Emit_MOV_IMM(REG_SCRATCH0, (uint8_t)cycles);
    } else {
        LoadImm16(REG_SCRATCH0, (uint16_t)cycles);
    }
    Emit_STR_IMM(REG_SCRATCH0, REG_STATE, DS_CYCLES_EXEC);

    // POP {r4-r12, pc} - return
    Emit_POP(0x9FF0);
}

// Copy result byte to NZ register for lazy flag evaluation
// After this, N = (r7 & 0x80), Z = (r7 == 0)
void Emitter::Emit_UpdateNZ(int reg) {
    if (reg != REG_NZ) {
        Emit_AND_IMM(REG_NZ, reg, 0xFF);
    }
    // If reg is already REG_NZ, it's already there
}

} // namespace Dynarec
