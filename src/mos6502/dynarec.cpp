#include "dynarec.h"
#include "dynarec_emitter.h"
#include "../nds_platform.h"
#include <cstring>

// External 6502 state and memory
extern uint8_t* cached_ram_ptr;
extern uint8_t* cached_rom_lo_ptr;
extern uint8_t* cached_rom_hi_ptr;

namespace Dynarec {

// Block cache - simple hash table
static constexpr int BLOCK_CACHE_SIZE = 256;
static Block* block_table[BLOCK_CACHE_SIZE];
static Block block_pool[BLOCK_CACHE_SIZE];
static int block_pool_used = 0;

// Code buffer in ITCM - linker places this in .itcm section
#if defined(NDS_BUILD) && defined(ARM9)
__attribute__((section(".itcm"), aligned(4)))
static uint8_t dynarec_code_buffer[CODE_BUFFER_SIZE];
#else
static uint8_t dynarec_code_buffer[CODE_BUFFER_SIZE];
#endif

static uint8_t* code_ptr = nullptr;

// Statistics
static Stats stats = {};

// Forward declarations
static bool CompileInstruction(Emitter& emit, uint8_t opcode, uint16_t& pc, bool& block_ended);
static uint8_t FetchByteAt(uint16_t addr);
static uint16_t FetchWordAt(uint16_t addr);

void Init() {
    std::memset(block_table, 0, sizeof(block_table));
    std::memset(block_pool, 0, sizeof(block_pool));
    block_pool_used = 0;
    code_ptr = dynarec_code_buffer;
    stats = {};
    stats.compile_bytes_total = CODE_BUFFER_SIZE;
}

void Shutdown() {
    // Nothing to free - buffer is static
}

static uint32_t ComputeHash(uint16_t pc) {
    return ((pc * 0x9E3779B9u) >> 24) & (BLOCK_CACHE_SIZE - 1);
}

static Block* FindBlock(uint16_t pc) {
    uint32_t hash = ComputeHash(pc);
    Block* b = block_table[hash];
    while (b) {
        if (b->pc == pc) return b;
        b = b->next;
    }
    return nullptr;
}

static Block* AllocateBlock() {
    if (block_pool_used >= BLOCK_CACHE_SIZE) return nullptr;
    Block* b = &block_pool[block_pool_used++];
    b->next = nullptr;
    return b;
}

static void InsertBlock(Block* block) {
    uint32_t hash = ComputeHash(block->pc);
    block->next = block_table[hash];
    block_table[hash] = block;
}

// Fetch a byte from ROM/RAM at compile time
static uint8_t FetchByteAt(uint16_t addr) {
    if (addr >= 0xC000) {
        return cached_rom_hi_ptr[addr & 0x3FFF];
    } else if (addr >= 0x8000) {
        return cached_rom_lo_ptr[addr & 0x3FFF];
    } else if (addr < 0x2000) {
        return cached_ram_ptr[addr];
    }
    return 0; // I/O or unmapped
}

static uint16_t FetchWordAt(uint16_t addr) {
    return FetchByteAt(addr) | ((uint16_t)FetchByteAt(addr + 1) << 8);
}

void* CompileBlock(uint16_t pc) {
    // Check if already compiled
    Block* existing = FindBlock(pc);
    if (existing) return existing->code;

    Block* block = AllocateBlock();
    if (!block) {
        stats.fallback_count++;
        return nullptr;
    }

    // Check space
    size_t remaining = CODE_BUFFER_SIZE - (code_ptr - dynarec_code_buffer);
    if (remaining < 512) {
        // Not enough space, invalidate all and start over
        InvalidateAll();
        remaining = CODE_BUFFER_SIZE;
    }

    Emitter emit(code_ptr, remaining);
    void* code_start = code_ptr;

    emit.Emit_Prologue();

    uint16_t start_pc = pc;
    uint16_t current_pc = pc;
    int instructions = 0;
    bool block_ended = false;

    while (instructions < MAX_BLOCK_SIZE && emit.cycles < MAX_BLOCK_CYCLES && !block_ended) {
        // Can't compile I/O region
        if (current_pc >= 0x2000 && current_pc < 0x8000) break;

        // Record PC mapping for backward branches
        emit.RecordPCMap(current_pc);

        uint8_t opcode = FetchByteAt(current_pc);
        current_pc++;

        if (!CompileInstruction(emit, opcode, current_pc, block_ended)) {
            // Unsupported opcode - back up and exit
            current_pc--;
            break;
        }
        instructions++;
    }

    // If we compiled nothing useful, bail
    if (instructions == 0) {
        stats.fallback_count++;
        return nullptr;
    }

    // Emit epilogue with final PC
    emit.Emit_Epilogue(current_pc);

    code_ptr = emit.ptr;

    // Fill in block metadata
    block->pc = start_pc;
    block->end_pc = current_pc;
    block->code = code_start;
    block->cycles = emit.cycles;
    block->exec_count = 0;

    InsertBlock(block);
    stats.blocks_compiled++;
    stats.compile_bytes_used = code_ptr - dynarec_code_buffer;

    // Flush caches - use libnds functions for targeted flush
#if defined(NDS_BUILD) && defined(ARM9)
    DC_FlushRange(code_start, emit.Size());
    IC_InvalidateRange(code_start, emit.Size());
#endif

    return code_start;
}

// Compile a single 6502 instruction. Returns false if unsupported.
// pc points past the opcode byte on entry.
static bool CompileInstruction(Emitter& emit, uint8_t opcode, uint16_t& pc, bool& block_ended) {
    switch (opcode) {

    // ===== LOADS =====

    // LDA #imm (0xA9)
    case 0xA9: {
        uint8_t imm = FetchByteAt(pc++);
        emit.Emit_MOV_IMM(REG_A, imm);
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 2;
        return true;
    }

    // LDA zp (0xA5)
    case 0xA5: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_LDRB_IMM(REG_A, REG_RAM, zp);
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 3;
        return true;
    }

    // LDA abs (0xAD)
    case 0xAD: {
        uint16_t addr = FetchWordAt(pc);
        pc += 2;
        if (addr < 0x2000) {
            // RAM
            if (addr < 0x100) {
                emit.Emit_LDRB_IMM(REG_A, REG_RAM, addr);
            } else {
                // Need to load address into scratch
                emit.LoadImm16(REG_SCRATCH0, addr);
                // LDRB r4, [r9, r0]
                emit.Emit(ARM_COND(COND_AL) | (0x1 << 26) | (1 << 25) | (1 << 24) | (1 << 23) |
                          (1 << 22) | (0 << 21) | (1 << 20) |
                          (REG_RAM << 16) | (REG_A << 12) | REG_SCRATCH0);
            }
        } else if (addr >= 0xC000) {
            uint16_t off = addr & 0x3FFF;
            if (off < 0x1000) {
                emit.Emit_LDRB_IMM(REG_A, REG_ROM_HI, off);
            } else {
                emit.LoadImm16(REG_SCRATCH0, off);
                emit.Emit(ARM_COND(COND_AL) | (0x1 << 26) | (1 << 25) | (1 << 24) | (1 << 23) |
                          (1 << 22) | (0 << 21) | (1 << 20) |
                          (REG_ROM_HI << 16) | (REG_A << 12) | REG_SCRATCH0);
            }
        } else if (addr >= 0x8000) {
            uint16_t off = addr & 0x3FFF;
            if (off < 0x1000) {
                emit.Emit_LDRB_IMM(REG_A, REG_ROM_LO, off);
            } else {
                emit.LoadImm16(REG_SCRATCH0, off);
                emit.Emit(ARM_COND(COND_AL) | (0x1 << 26) | (1 << 25) | (1 << 24) | (1 << 23) |
                          (1 << 22) | (0 << 21) | (1 << 20) |
                          (REG_ROM_LO << 16) | (REG_A << 12) | REG_SCRATCH0);
            }
        } else {
            // I/O - can't compile
            pc -= 3; // back up
            return false;
        }
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 4;
        return true;
    }

    // LDX #imm (0xA2)
    case 0xA2: {
        uint8_t imm = FetchByteAt(pc++);
        emit.Emit_MOV_IMM(REG_X, imm);
        emit.Emit_UpdateNZ(REG_X);
        emit.cycles += 2;
        return true;
    }

    // LDY #imm (0xA0)
    case 0xA0: {
        uint8_t imm = FetchByteAt(pc++);
        emit.Emit_MOV_IMM(REG_Y, imm);
        emit.Emit_UpdateNZ(REG_Y);
        emit.cycles += 2;
        return true;
    }

    // LDX zp (0xA6)
    case 0xA6: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_LDRB_IMM(REG_X, REG_RAM, zp);
        emit.Emit_UpdateNZ(REG_X);
        emit.cycles += 3;
        return true;
    }

    // LDY zp (0xA4)
    case 0xA4: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_LDRB_IMM(REG_Y, REG_RAM, zp);
        emit.Emit_UpdateNZ(REG_Y);
        emit.cycles += 3;
        return true;
    }

    // ===== STORES =====

    // STA zp (0x85)
    case 0x85: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_STRB_IMM(REG_A, REG_RAM, zp);
        emit.cycles += 3;
        return true;
    }

    // STA abs (0x8D)
    case 0x8D: {
        uint16_t addr = FetchWordAt(pc);
        pc += 2;
        if (addr < 0x2000) {
            if (addr < 0x100) {
                emit.Emit_STRB_IMM(REG_A, REG_RAM, addr);
            } else {
                emit.LoadImm16(REG_SCRATCH0, addr);
                // STRB r4, [r9, r0]
                emit.Emit(ARM_COND(COND_AL) | (0x1 << 26) | (1 << 25) | (1 << 24) | (1 << 23) |
                          (1 << 22) | (0 << 21) | (0 << 20) |
                          (REG_RAM << 16) | (REG_A << 12) | REG_SCRATCH0);
            }
        } else {
            // I/O write or ROM write - exit block
            pc -= 3;
            return false;
        }
        emit.cycles += 4;
        return true;
    }

    // STX zp (0x86)
    case 0x86: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_STRB_IMM(REG_X, REG_RAM, zp);
        emit.cycles += 3;
        return true;
    }

    // STY zp (0x84)
    case 0x84: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_STRB_IMM(REG_Y, REG_RAM, zp);
        emit.cycles += 3;
        return true;
    }

    // ===== REGISTER TRANSFERS =====

    // TAX (0xAA)
    case 0xAA:
        emit.Emit_MOV(REG_X, REG_A);
        emit.Emit_UpdateNZ(REG_X);
        emit.cycles += 2;
        return true;

    // TAY (0xA8)
    case 0xA8:
        emit.Emit_MOV(REG_Y, REG_A);
        emit.Emit_UpdateNZ(REG_Y);
        emit.cycles += 2;
        return true;

    // TXA (0x8A)
    case 0x8A:
        emit.Emit_MOV(REG_A, REG_X);
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 2;
        return true;

    // TYA (0x98)
    case 0x98:
        emit.Emit_MOV(REG_A, REG_Y);
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 2;
        return true;

    // ===== INCREMENT/DECREMENT =====

    // INX (0xE8)
    case 0xE8:
        emit.Emit_ADD_IMM(REG_X, REG_X, 1);
        emit.Emit_AND_IMM(REG_X, REG_X, 0xFF);  // 8-bit wrap
        emit.Emit_UpdateNZ(REG_X);
        emit.cycles += 2;
        return true;

    // INY (0xC8)
    case 0xC8:
        emit.Emit_ADD_IMM(REG_Y, REG_Y, 1);
        emit.Emit_AND_IMM(REG_Y, REG_Y, 0xFF);
        emit.Emit_UpdateNZ(REG_Y);
        emit.cycles += 2;
        return true;

    // DEX (0xCA)
    case 0xCA:
        emit.Emit_SUB_IMM(REG_X, REG_X, 1);
        emit.Emit_AND_IMM(REG_X, REG_X, 0xFF);
        emit.Emit_UpdateNZ(REG_X);
        emit.cycles += 2;
        return true;

    // DEY (0x88)
    case 0x88:
        emit.Emit_SUB_IMM(REG_Y, REG_Y, 1);
        emit.Emit_AND_IMM(REG_Y, REG_Y, 0xFF);
        emit.Emit_UpdateNZ(REG_Y);
        emit.cycles += 2;
        return true;

    // ===== LOGIC (immediate) =====

    // AND #imm (0x29)
    case 0x29: {
        uint8_t imm = FetchByteAt(pc++);
        emit.Emit_AND_IMM(REG_A, REG_A, imm);
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 2;
        return true;
    }

    // ORA #imm (0x09)
    case 0x09: {
        uint8_t imm = FetchByteAt(pc++);
        emit.Emit_ORR_IMM(REG_A, REG_A, imm);
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 2;
        return true;
    }

    // EOR #imm (0x49)
    case 0x49: {
        uint8_t imm = FetchByteAt(pc++);
        emit.Emit_EOR_IMM(REG_A, REG_A, imm);
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 2;
        return true;
    }

    // ===== COMPARE =====

    // CMP #imm (0xC9)
    case 0xC9: {
        uint8_t imm = FetchByteAt(pc++);
        // SUBS r0, r4, #imm (sets ARM flags)
        emit.Emit_SUB_IMM(REG_SCRATCH0, REG_A, imm, true);
        // NZ from low byte of result
        emit.Emit_AND_IMM(REG_NZ, REG_SCRATCH0, 0xFF);
        // Carry: 6502 carry = ARM carry (A >= imm → carry set)
        // After SUBS, ARM C flag = borrow inverted = (A >= imm)
        // MOV r8, #0; MOVCS r8, #1
        emit.Emit_MOV_IMM(REG_CARRY, 0);
        emit.Emit(ARM_COND(COND_CS) | ARM_DP_IMM(DP_MOV, REG_CARRY, 0, 1, 0, false));
        emit.cycles += 2;
        return true;
    }

    // CPX #imm (0xE0)
    case 0xE0: {
        uint8_t imm = FetchByteAt(pc++);
        emit.Emit_SUB_IMM(REG_SCRATCH0, REG_X, imm, true);
        emit.Emit_AND_IMM(REG_NZ, REG_SCRATCH0, 0xFF);
        emit.Emit_MOV_IMM(REG_CARRY, 0);
        emit.Emit(ARM_COND(COND_CS) | ARM_DP_IMM(DP_MOV, REG_CARRY, 0, 1, 0, false));
        emit.cycles += 2;
        return true;
    }

    // CPY #imm (0xC0)
    case 0xC0: {
        uint8_t imm = FetchByteAt(pc++);
        emit.Emit_SUB_IMM(REG_SCRATCH0, REG_Y, imm, true);
        emit.Emit_AND_IMM(REG_NZ, REG_SCRATCH0, 0xFF);
        emit.Emit_MOV_IMM(REG_CARRY, 0);
        emit.Emit(ARM_COND(COND_CS) | ARM_DP_IMM(DP_MOV, REG_CARRY, 0, 1, 0, false));
        emit.cycles += 2;
        return true;
    }

    // ===== BRANCHES =====

    // BNE (0xD0)
    case 0xD0: {
        int8_t offset = (int8_t)FetchByteAt(pc++);
        uint16_t target = pc + offset;

        // TST r7, #0xFF — sets ARM Z if r7==0 (6502 Z set)
        emit.Emit_TST_IMM(REG_NZ, 0xFF);

        // Check if backward branch to a compiled address
        int32_t arm_target = emit.GetARMOffset(target);
        if (arm_target >= 0 && offset < 0) {
            // Backward branch within block — emit ARM branch
            // Branch offset = target_addr - (current_addr + 8)
            int32_t branch_offset = arm_target - (int32_t)emit.CurrentOffset() - 8;
            emit.Emit_B(branch_offset, COND_NE);
            emit.cycles += 3;  // taken branch
        } else {
            // Forward or out-of-block: exit block with target PC if taken
            // BEQ skip (if Z set = 6502 Z set = branch NOT taken)
            // Emit epilogue for taken branch
            // ... but that's complex. Simpler: always end block at branch.
            block_ended = true;

            // If branch not taken, we fall through to epilogue at pc
            // If branch taken, we need epilogue at target
            // Simple approach: emit conditional exit
            // BEQ +skip_to_after_taken_epilogue
            uint8_t* patch_loc = emit.ptr;
            emit.Emit(0); // placeholder for BEQ skip

            // Taken path: epilogue with target PC
            emit.Emit_Epilogue(target);

            // Patch BEQ to skip over the taken epilogue
            int32_t skip_offset = (int32_t)(emit.ptr - patch_loc) - 8;
            *(uint32_t*)patch_loc = ARM_COND(COND_EQ) | ARM_B(skip_offset);

            // Not-taken path falls through to the block's normal epilogue
            emit.cycles += 2;
        }
        return true;
    }

    // BEQ (0xF0)
    case 0xF0: {
        int8_t offset = (int8_t)FetchByteAt(pc++);
        uint16_t target = pc + offset;

        emit.Emit_TST_IMM(REG_NZ, 0xFF);

        int32_t arm_target = emit.GetARMOffset(target);
        if (arm_target >= 0 && offset < 0) {
            int32_t branch_offset = arm_target - (int32_t)emit.CurrentOffset() - 8;
            emit.Emit_B(branch_offset, COND_EQ);
            emit.cycles += 3;
        } else {
            block_ended = true;
            uint8_t* patch_loc = emit.ptr;
            emit.Emit(0);
            emit.Emit_Epilogue(target);
            int32_t skip_offset = (int32_t)(emit.ptr - patch_loc) - 8;
            *(uint32_t*)patch_loc = ARM_COND(COND_NE) | ARM_B(skip_offset);
            emit.cycles += 2;
        }
        return true;
    }

    // BCS (0xB0)
    case 0xB0: {
        int8_t offset = (int8_t)FetchByteAt(pc++);
        uint16_t target = pc + offset;

        // CMP r8, #1 — if carry==1, ARM Z set
        emit.Emit_CMP_IMM(REG_CARRY, 1);

        int32_t arm_target = emit.GetARMOffset(target);
        if (arm_target >= 0 && offset < 0) {
            int32_t branch_offset = arm_target - (int32_t)emit.CurrentOffset() - 8;
            emit.Emit_B(branch_offset, COND_EQ);  // branch if carry==1
            emit.cycles += 3;
        } else {
            block_ended = true;
            uint8_t* patch_loc = emit.ptr;
            emit.Emit(0);
            emit.Emit_Epilogue(target);
            int32_t skip_offset = (int32_t)(emit.ptr - patch_loc) - 8;
            *(uint32_t*)patch_loc = ARM_COND(COND_NE) | ARM_B(skip_offset);
            emit.cycles += 2;
        }
        return true;
    }

    // BCC (0x90)
    case 0x90: {
        int8_t offset = (int8_t)FetchByteAt(pc++);
        uint16_t target = pc + offset;

        emit.Emit_CMP_IMM(REG_CARRY, 1);

        int32_t arm_target = emit.GetARMOffset(target);
        if (arm_target >= 0 && offset < 0) {
            int32_t branch_offset = arm_target - (int32_t)emit.CurrentOffset() - 8;
            emit.Emit_B(branch_offset, COND_NE);  // branch if carry!=1 (carry clear)
            emit.cycles += 3;
        } else {
            block_ended = true;
            uint8_t* patch_loc = emit.ptr;
            emit.Emit(0);
            emit.Emit_Epilogue(target);
            int32_t skip_offset = (int32_t)(emit.ptr - patch_loc) - 8;
            *(uint32_t*)patch_loc = ARM_COND(COND_EQ) | ARM_B(skip_offset);
            emit.cycles += 2;
        }
        return true;
    }

    // BMI (0x30)
    case 0x30: {
        int8_t offset = (int8_t)FetchByteAt(pc++);
        uint16_t target = pc + offset;

        // TST r7, #0x80 — sets ARM Z if bit 7 clear (6502 N clear)
        emit.Emit_TST_IMM(REG_NZ, 0x80);

        int32_t arm_target = emit.GetARMOffset(target);
        if (arm_target >= 0 && offset < 0) {
            int32_t branch_offset = arm_target - (int32_t)emit.CurrentOffset() - 8;
            emit.Emit_B(branch_offset, COND_NE);  // branch if N set
            emit.cycles += 3;
        } else {
            block_ended = true;
            uint8_t* patch_loc = emit.ptr;
            emit.Emit(0);
            emit.Emit_Epilogue(target);
            int32_t skip_offset = (int32_t)(emit.ptr - patch_loc) - 8;
            *(uint32_t*)patch_loc = ARM_COND(COND_EQ) | ARM_B(skip_offset);
            emit.cycles += 2;
        }
        return true;
    }

    // BPL (0x10)
    case 0x10: {
        int8_t offset = (int8_t)FetchByteAt(pc++);
        uint16_t target = pc + offset;

        emit.Emit_TST_IMM(REG_NZ, 0x80);

        int32_t arm_target = emit.GetARMOffset(target);
        if (arm_target >= 0 && offset < 0) {
            int32_t branch_offset = arm_target - (int32_t)emit.CurrentOffset() - 8;
            emit.Emit_B(branch_offset, COND_EQ);  // branch if N clear
            emit.cycles += 3;
        } else {
            block_ended = true;
            uint8_t* patch_loc = emit.ptr;
            emit.Emit(0);
            emit.Emit_Epilogue(target);
            int32_t skip_offset = (int32_t)(emit.ptr - patch_loc) - 8;
            *(uint32_t*)patch_loc = ARM_COND(COND_NE) | ARM_B(skip_offset);
            emit.cycles += 2;
        }
        return true;
    }

    // ===== CONTROL =====

    // JMP abs (0x4C)
    case 0x4C: {
        uint16_t target = FetchWordAt(pc);
        pc = target;  // Update PC to jump target
        block_ended = true;
        emit.cycles += 3;
        return true;
    }

    // NOP (0xEA)
    case 0xEA:
        emit.cycles += 2;
        return true;

    // CLC (0x18)
    case 0x18:
        emit.Emit_MOV_IMM(REG_CARRY, 0);
        emit.cycles += 2;
        return true;

    // SEC (0x38)
    case 0x38:
        emit.Emit_MOV_IMM(REG_CARRY, 1);
        emit.cycles += 2;
        return true;

    default:
        return false;
    }
}

void* GetBlock(uint16_t pc) {
    Block* b = FindBlock(pc);
    if (b) {
        b->exec_count++;
        stats.blocks_executed++;
        return b->code;
    }
    return nullptr;
}

void InvalidateAll() {
    std::memset(block_table, 0, sizeof(block_table));
    block_pool_used = 0;
    code_ptr = dynarec_code_buffer;
    stats.blocks_invalidated += stats.blocks_compiled;
    stats.blocks_compiled = 0;
    stats.compile_bytes_used = 0;
}

int RunBlock(void* code, DynarecState* state) {
    // Call compiled block: int block_func(DynarecState*)
    typedef int (*BlockFunc)(DynarecState*);
    BlockFunc func = (BlockFunc)code;
    func(state);
    return state->cycles_executed;
}

Stats GetStats() {
    return stats;
}

void ResetStats() {
    stats = {};
}

} // namespace Dynarec
