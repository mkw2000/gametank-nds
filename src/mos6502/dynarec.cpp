#include "dynarec.h"
#include "dynarec_emitter.h"
#include "../nds_platform.h"
#include <cstring>
#include <cstdio>
#include <cstdarg>

// #define DYNAREC_DEBUG  // Uncomment to enable dynarec logging

#ifdef DYNAREC_DEBUG
static void DebugLog(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    FILE* f = fopen("fat:/gametank_perf.log", "a");
    if (!f) f = fopen("sd:/gametank_perf.log", "a");
    if (f) {
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fclose(f);
    }
}
#else
#define DebugLog(...) ((void)0)
#endif

// External 6502 state and memory
extern uint8_t* cached_ram_ptr;
extern uint8_t* cached_rom_lo_ptr;
extern uint8_t* cached_rom_hi_ptr;
extern uint16_t cached_rom_linear_mask;
extern uint8_t loadedRomType;

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

    // Log the actual address of the code buffer
    DebugLog("DR: ==========================================\n");
    DebugLog("DR: DYNAREC INIT code_buffer=%p size=%zu\n", dynarec_code_buffer, (size_t)CODE_BUFFER_SIZE);

    // Verify buffer is in ITCM range
    uint32_t buf_addr = (uint32_t)dynarec_code_buffer;
    DebugLog("DR: buffer address = %08X\n", buf_addr);
    if (buf_addr < 0x01FF8000 || buf_addr >= 0x01FFA000) {
        DebugLog("DR: ERROR: code buffer NOT in ITCM!\n");
    } else {
        DebugLog("DR: code buffer OK in ITCM\n");
    }
    DebugLog("DR: ==========================================\n");
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
// Must match NDSMainReadFast logic: FLASH2M uses 0x3FFF bank mask,
// EEPROM types use cached_rom_linear_mask for linear addressing.
static uint8_t FetchByteAt(uint16_t addr) {
    if (addr >= 0x8000) {
        // FLASH2M(3)/FLASH2M_RAM32K(4) use 16KB banks (& 0x3FFF)
        // EEPROM types use linear mask
        if (loadedRomType == 3 || loadedRomType == 4) {
            return (addr & 0x4000)
                ? cached_rom_hi_ptr[addr & 0x3FFF]
                : cached_rom_lo_ptr[addr & 0x3FFF];
        }
        return cached_rom_lo_ptr[addr & cached_rom_linear_mask];
    } else if (addr < 0x2000) {
        return cached_ram_ptr[addr];
    }
    return 0; // I/O or unmapped
}

static uint16_t FetchWordAt(uint16_t addr) {
    return FetchByteAt(addr) | ((uint16_t)FetchByteAt(addr + 1) << 8);
}

// Negative cache: PCs where compilation failed (don't retry)
static constexpr int FAIL_CACHE_SIZE = 64;
static uint16_t fail_cache[FAIL_CACHE_SIZE];
static int fail_cache_count = 0;

static bool IsInFailCache(uint16_t pc) {
    for (int i = 0; i < fail_cache_count; i++) {
        if (fail_cache[i] == pc) return true;
    }
    return false;
}

static void AddToFailCache(uint16_t pc) {
    if (fail_cache_count < FAIL_CACHE_SIZE) {
        fail_cache[fail_cache_count++] = pc;
    }
}

// Helper: emit code to load a byte from a compile-time known 6502 address into dest_reg.
// Uses REG_SCRATCH0 for address computation when offset > 4095.
// Returns false if address is in I/O range (0x2000-0x7FFF).
static bool EmitLoadAbs(Emitter& emit, int dest_reg, uint16_t addr) {
    if (addr < 0x2000) {
        if (addr < 0x1000) {
            emit.Emit_LDRB_IMM(dest_reg, REG_RAM, addr);
        } else {
            emit.LoadImm16(REG_SCRATCH0, addr);
            emit.Emit_LDRB_REG(dest_reg, REG_RAM, REG_SCRATCH0);
        }
    } else if (addr >= 0xC000) {
        uint16_t off = addr & 0x3FFF;
        if (off < 0x1000) {
            emit.Emit_LDRB_IMM(dest_reg, REG_ROM_HI, off);
        } else {
            emit.LoadImm16(REG_SCRATCH0, off);
            emit.Emit_LDRB_REG(dest_reg, REG_ROM_HI, REG_SCRATCH0);
        }
    } else if (addr >= 0x8000) {
        uint16_t off = addr & 0x3FFF;
        if (off < 0x1000) {
            emit.Emit_LDRB_IMM(dest_reg, REG_ROM_LO, off);
        } else {
            emit.LoadImm16(REG_SCRATCH0, off);
            emit.Emit_LDRB_REG(dest_reg, REG_ROM_LO, REG_SCRATCH0);
        }
    } else {
        return false;
    }
    return true;
}

// Helper: emit code to store a byte to a compile-time known RAM address.
// Returns false if address >= 0x2000 (ROM/I/O).
static bool EmitStoreAbs(Emitter& emit, int src_reg, uint16_t addr) {
    if (addr >= 0x2000) return false;
    if (addr < 0x1000) {
        emit.Emit_STRB_IMM(src_reg, REG_RAM, addr);
    } else {
        emit.LoadImm16(REG_SCRATCH0, addr);
        emit.Emit_STRB_REG(src_reg, REG_RAM, REG_SCRATCH0);
    }
    return true;
}

// Helper: emit CMP-style flag update (NZ + carry) for a register compare.
// Emits: SUBS scratch, reg, operand_reg; AND NZ, scratch, #0xFF; carry from ARM C flag
static void EmitCompareReg(Emitter& emit, int reg, int operand_reg) {
    // SUBS r0, reg, operand_reg
    emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_SUB, REG_SCRATCH0, reg, operand_reg, true));
    emit.Emit_AND_IMM(REG_NZ, REG_SCRATCH0, 0xFF);
    emit.Emit_MOV_IMM(REG_CARRY, 0);
    emit.Emit(ARM_COND(COND_CS) | ARM_DP_IMM(DP_MOV, REG_CARRY, 0, 1, 0, false));
}

// Helper: emit ADC logic. operand is already in operand_reg (8-bit value).
// Computes A = A + operand + carry, updates NZ, carry, and V flag in status byte.
static void EmitADC(Emitter& emit, int operand_reg) {
    // Save old A for overflow detection
    emit.Emit_MOV(REG_SCRATCH3, REG_A);
    // temp = A + operand + carry (16-bit result in r0)
    emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ADD, REG_SCRATCH0, REG_A, operand_reg, false));
    emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ADD, REG_SCRATCH0, REG_SCRATCH0, REG_CARRY, false));
    // New carry = (result >> 8) & 1; since max result is 255+255+1=511, bit 8 is the carry
    // MOV r8, r0, LSR #8
    emit.Emit(ARM_COND(COND_AL) | ((uint32_t)DP_MOV << 21) |
              (REG_CARRY << 12) | (8 << 7) | (1 << 5) | REG_SCRATCH0);
    // A = result & 0xFF
    emit.Emit_AND_IMM(REG_A, REG_SCRATCH0, 0xFF);
    // NZ from A
    emit.Emit_UpdateNZ(REG_A);
    // Overflow: V = ~(old_A ^ operand) & (old_A ^ result) & 0x80
    // EOR r0, r3, r4 → old_A ^ new_A (= old_A ^ result)
    emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_EOR, REG_SCRATCH0, REG_SCRATCH3, REG_A, false));
    // EOR r1, r3, operand_reg → old_A ^ operand
    emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_EOR, REG_SCRATCH1, REG_SCRATCH3, operand_reg, false));
    // BIC r0, r0, r1 → (old_A ^ result) & ~(old_A ^ operand)
    emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_BIC, REG_SCRATCH0, REG_SCRATCH0, REG_SCRATCH1, false));
    // TST r0, #0x80
    emit.Emit_TST_IMM(REG_SCRATCH0, 0x80);
    // Update V in status byte
    emit.Emit_LDRB_IMM(REG_SCRATCH1, REG_STATE, DS_STATUS);
    // BIC r1, r1, #0x40 (clear V)
    emit.Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_BIC, REG_SCRATCH1, REG_SCRATCH1, 0x40, 0, false));
    // ORRNE r1, r1, #0x40 (set V if overflow)
    emit.Emit(ARM_COND(COND_NE) | ARM_DP_IMM(DP_ORR, REG_SCRATCH1, REG_SCRATCH1, 0x40, 0, false));
    emit.Emit_STRB_IMM(REG_SCRATCH1, REG_STATE, DS_STATUS);
}

// Helper: emit SBC logic. SBC = ADC with complemented operand.
// operand_reg contains the original operand (not complemented).
static void EmitSBC(Emitter& emit, int operand_reg) {
    // Save old A and original operand for overflow
    emit.Emit_MOV(REG_SCRATCH3, REG_A);
    // Complement the operand: r2 = operand ^ 0xFF (= ~operand & 0xFF)
    emit.Emit_EOR_IMM(REG_SCRATCH2, operand_reg, 0xFF);
    // temp = A + ~operand + carry
    emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ADD, REG_SCRATCH0, REG_A, REG_SCRATCH2, false));
    emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ADD, REG_SCRATCH0, REG_SCRATCH0, REG_CARRY, false));
    // Carry
    emit.Emit(ARM_COND(COND_AL) | ((uint32_t)DP_MOV << 21) |
              (REG_CARRY << 12) | (8 << 7) | (1 << 5) | REG_SCRATCH0);
    // A = result & 0xFF
    emit.Emit_AND_IMM(REG_A, REG_SCRATCH0, 0xFF);
    emit.Emit_UpdateNZ(REG_A);
    // Overflow for SBC: V = (old_A ^ operand) & (old_A ^ result) & 0x80
    // (note: AND not BIC, because operand was complemented for ADC)
    emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_EOR, REG_SCRATCH0, REG_SCRATCH3, REG_A, false));
    emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_EOR, REG_SCRATCH1, REG_SCRATCH3, operand_reg, false));
    emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_AND, REG_SCRATCH0, REG_SCRATCH0, REG_SCRATCH1, false));
    emit.Emit_TST_IMM(REG_SCRATCH0, 0x80);
    emit.Emit_LDRB_IMM(REG_SCRATCH1, REG_STATE, DS_STATUS);
    emit.Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_BIC, REG_SCRATCH1, REG_SCRATCH1, 0x40, 0, false));
    emit.Emit(ARM_COND(COND_NE) | ARM_DP_IMM(DP_ORR, REG_SCRATCH1, REG_SCRATCH1, 0x40, 0, false));
    emit.Emit_STRB_IMM(REG_SCRATCH1, REG_STATE, DS_STATUS);
}

void* CompileBlock(uint16_t pc) {
    DebugLog("DR: CompileBlock(%04X) called\n", pc);

    // Check if already compiled
    Block* existing = FindBlock(pc);
    if (existing) {
        DebugLog("DR:  found existing block at %p\n", existing->code);
        return existing->code;
    }

    // Don't retry PCs that already failed
    if (IsInFailCache(pc)) {
        DebugLog("DR:  in fail cache\n");
        return nullptr;
    }

    // Check pool space
    if (block_pool_used >= BLOCK_CACHE_SIZE) {
        DebugLog("DR:  block pool full\n");
        stats.fallback_count++;
        return nullptr;
    }

    // Check code space
    size_t remaining = CODE_BUFFER_SIZE - (code_ptr - dynarec_code_buffer);
    DebugLog("DR:  remaining code space: %zu\n", remaining);
    if (remaining < 512) {
        DebugLog("DR:  invalidating all blocks\n");
        InvalidateAll();
        remaining = CODE_BUFFER_SIZE;
    }

    // Try compilation before allocating a block
    Emitter emit(code_ptr, remaining);
    void* code_start = code_ptr;

    emit.Emit_Prologue();

    uint16_t start_pc = pc;
    uint16_t current_pc = pc;
    int instructions = 0;
    bool block_ended = false;

    while (instructions < MAX_BLOCK_SIZE && emit.cycles < MAX_BLOCK_CYCLES && !block_ended) {
        if (current_pc >= 0x2000 && current_pc < 0x8000) break;

        emit.RecordPCMap(current_pc);

        uint8_t opcode = FetchByteAt(current_pc);
        uint16_t instr_pc = current_pc;
        current_pc++;

        if (!emit.CanEmit(256)) {
            DebugLog("DR: buffer full at %04X\n", instr_pc);
            break;
        }
        if (!CompileInstruction(emit, opcode, current_pc, block_ended)) {
            // Fallback: current_pc was incremented past the opcode byte.
            // Set it back to instr_pc so the epilogue stores the correct exit PC.
            current_pc = instr_pc;
            DebugLog("DR: fallback at %04X op=%02X\n", instr_pc, opcode);
            break;
        }
        DebugLog("DR: compiled %04X op=%02X ptr=%p\n", instr_pc, opcode, emit.ptr);
        instructions++;
    }

    // If we compiled nothing useful, record failure and bail
    if (instructions == 0) {
        uint8_t fail_op = FetchByteAt(pc);
        stats.last_fail_opcode = fail_op;
        stats.last_fail_pc = pc;
        stats.fallback_count++;
        AddToFailCache(pc);
        return nullptr;
    }

    // Compilation succeeded — now allocate the block
    Block* block = AllocateBlock();
    if (!block) {
        stats.fallback_count++;
        return nullptr;
    }

    // Emit epilogue with final PC (only if block hasn't already emitted one)
    if (!block_ended) {
        emit.Emit_Epilogue(current_pc);
    }

    code_ptr = emit.ptr;
    size_t code_size = (uint8_t*)code_ptr - (uint8_t*)code_start;

    // Fill in block metadata
    block->pc = start_pc;
    block->end_pc = current_pc;
    block->code = code_start;
    block->cycles = emit.cycles;
    block->exec_count = 0;

    InsertBlock(block);
    stats.blocks_compiled++;
    stats.compile_bytes_used = code_ptr - dynarec_code_buffer;

    DebugLog("DR: compiled block %04X-%04X (%d instr, %d cycles, %zu bytes) at %p\n",
           start_pc, current_pc, instructions, emit.cycles, code_size, code_start);

    // Flush caches - use libnds functions for targeted flush
#if defined(NDS_BUILD) && defined(ARM9)
    DC_FlushRange(code_start, code_size);
    IC_InvalidateRange(code_start, code_size);

    // Memory barrier to ensure cache operations complete
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" :: "r"(0) : "memory");
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

    // LDA zp,X (0xB5)
    case 0xB5: {
        uint8_t zp = FetchByteAt(pc++);
        // Address = (zp + X) & 0xFF
        emit.Emit_ADD_IMM(REG_SCRATCH0, REG_X, zp);
        emit.Emit_AND_IMM(REG_SCRATCH0, REG_SCRATCH0, 0xFF);
        // LDRB r4, [r9, r0]
        emit.Emit(ARM_COND(COND_AL) | (0x1 << 26) | (1 << 25) | (1 << 24) | (1 << 23) |
                  (1 << 22) | (0 << 21) | (1 << 20) |
                  (REG_RAM << 16) | (REG_A << 12) | REG_SCRATCH0);
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 4;
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

    // STY abs (0x8C)
    case 0x8C: {
        uint16_t addr = FetchWordAt(pc);
        pc += 2;
        if (addr < 0x2000) {
            if (addr < 0x100) {
                emit.Emit_STRB_IMM(REG_Y, REG_RAM, addr);
            } else {
                emit.LoadImm16(REG_SCRATCH0, addr);
                emit.Emit(ARM_COND(COND_AL) | (0x1 << 26) | (1 << 25) | (1 << 24) | (1 << 23) |
                          (1 << 22) | (0 << 21) | (0 << 20) |
                          (REG_RAM << 16) | (REG_Y << 12) | REG_SCRATCH0);
            }
        } else {
            pc -= 3;
            return false;
        }
        emit.cycles += 4;
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

    // ===== LOGIC (zero page + absolute) =====

    // AND zp (0x25)
    case 0x25: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_RAM, zp);
        emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_AND, REG_A, REG_A, REG_SCRATCH0, false));
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 3;
        return true;
    }

    // AND abs (0x2D)
    case 0x2D: {
        uint16_t addr = FetchWordAt(pc); pc += 2;
        if (!EmitLoadAbs(emit, REG_SCRATCH0, addr)) { pc -= 3; return false; }
        emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_AND, REG_A, REG_A, REG_SCRATCH0, false));
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 4;
        return true;
    }

    // ORA zp (0x05)
    case 0x05: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_RAM, zp);
        emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ORR, REG_A, REG_A, REG_SCRATCH0, false));
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 3;
        return true;
    }

    // ORA abs (0x0D)
    case 0x0D: {
        uint16_t addr = FetchWordAt(pc); pc += 2;
        if (!EmitLoadAbs(emit, REG_SCRATCH0, addr)) { pc -= 3; return false; }
        emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ORR, REG_A, REG_A, REG_SCRATCH0, false));
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 4;
        return true;
    }

    // EOR zp (0x45)
    case 0x45: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_RAM, zp);
        emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_EOR, REG_A, REG_A, REG_SCRATCH0, false));
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 3;
        return true;
    }

    // EOR abs (0x4D)
    case 0x4D: {
        uint16_t addr = FetchWordAt(pc); pc += 2;
        if (!EmitLoadAbs(emit, REG_SCRATCH0, addr)) { pc -= 3; return false; }
        emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_EOR, REG_A, REG_A, REG_SCRATCH0, false));
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 4;
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

    // CMP zp (0xC5)
    case 0xC5: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_LDRB_IMM(REG_SCRATCH1, REG_RAM, zp);
        EmitCompareReg(emit, REG_A, REG_SCRATCH1);
        emit.cycles += 3;
        return true;
    }

    // CMP abs (0xCD)
    case 0xCD: {
        uint16_t addr = FetchWordAt(pc); pc += 2;
        if (!EmitLoadAbs(emit, REG_SCRATCH1, addr)) { pc -= 3; return false; }
        EmitCompareReg(emit, REG_A, REG_SCRATCH1);
        emit.cycles += 4;
        return true;
    }

    // CPX zp (0xE4)
    case 0xE4: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_LDRB_IMM(REG_SCRATCH1, REG_RAM, zp);
        EmitCompareReg(emit, REG_X, REG_SCRATCH1);
        emit.cycles += 3;
        return true;
    }

    // CPY zp (0xC4)
    case 0xC4: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_LDRB_IMM(REG_SCRATCH1, REG_RAM, zp);
        EmitCompareReg(emit, REG_Y, REG_SCRATCH1);
        emit.cycles += 3;
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
            emit.cycles += 2;  // Add cycles BEFORE emitting epilogues

            // We need TWO epilogues since block_ended skips main epilogue.
            // Layout:
            //   TST r7, #0xFF
            //   BEQ not_taken  (placeholder)
            //   [taken epilogue: exit with target PC]
            //   B done         (placeholder)
            // not_taken:
            //   [not-taken epilogue: exit with current PC]
            // done:

            uint8_t* beq_patch = emit.ptr;
            emit.Emit(0); // placeholder for BEQ not_taken

            // Taken path: epilogue with target PC
            emit.Emit_Epilogue(target);

            // B done (skip not-taken epilogue)
            uint8_t* b_done_patch = emit.ptr;
            emit.Emit(0);

            // Not-taken path: epilogue with current PC
            emit.Emit_Epilogue(pc);

            // Patch BEQ to jump to not-taken epilogue
            // BEQ jumps over [taken epilogue] + [B done] to reach [not-taken epilogue]
            // Target = b_done_patch + 4 (start of not-taken epilogue)
            // PC+8 = beq_patch + 8
            // Offset = (b_done_patch + 4) - (beq_patch + 8) = (b_done_patch - beq_patch) - 4
            int32_t beq_offset = (int32_t)(b_done_patch - beq_patch) - 4;
            *(uint32_t*)beq_patch = ARM_COND(COND_EQ) | ARM_B(beq_offset);

            // Patch B done to skip over not-taken epilogue to done (current location)
            int32_t b_done_offset = (int32_t)(emit.ptr - b_done_patch) - 8;
            *(uint32_t*)b_done_patch = ARM_COND(COND_AL) | ARM_B(b_done_offset);
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
            emit.cycles += 2;  // Add cycles BEFORE emitting epilogues

            uint8_t* beq_patch = emit.ptr;
            emit.Emit(0); // placeholder for BEQ taken

            // Taken path: epilogue with target PC (BEQ jumps here when Z set)
            emit.Emit_Epilogue(target);

            // B done (skip not-taken epilogue)
            uint8_t* b_done_patch = emit.ptr;
            emit.Emit(0);

            // Not-taken path: epilogue with current PC (fall through when Z clear)
            emit.Emit_Epilogue(pc);

            // Patch BEQ to jump to taken epilogue (beq_patch + 4 = start of taken epilogue)
            // Offset = (beq_patch + 4) - (beq_patch + 8) = -4
            int32_t beq_offset = -4;
            *(uint32_t*)beq_patch = ARM_COND(COND_EQ) | ARM_B(beq_offset);

            // Patch B done to skip over not-taken epilogue
            int32_t b_done_offset = (int32_t)(emit.ptr - b_done_patch) - 8;
            *(uint32_t*)b_done_patch = ARM_COND(COND_AL) | ARM_B(b_done_offset);
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
            // BCS: branch if carry set (CMP r8, #1 sets Z if carry==1)
            // Taken when ARM Z set, not-taken when ARM Z clear
            block_ended = true;
            emit.cycles += 2;  // Add cycles BEFORE emitting epilogues

            uint8_t* beq_patch = emit.ptr;
            emit.Emit(0); // placeholder for BEQ taken

            // Taken path: epilogue with target PC
            emit.Emit_Epilogue(target);

            // B done (skip not-taken epilogue)
            uint8_t* b_done_patch = emit.ptr;
            emit.Emit(0);

            // Not-taken path: epilogue with current PC
            emit.Emit_Epilogue(pc);

            // Patch BEQ to jump to taken epilogue (beq_patch + 4 = start of taken epilogue)
            // Offset = (beq_patch + 4) - (beq_patch + 8) = -4
            int32_t beq_offset = -4;
            *(uint32_t*)beq_patch = ARM_COND(COND_EQ) | ARM_B(beq_offset);

            // Patch B done to skip over not-taken epilogue to done
            int32_t b_done_offset = (int32_t)(emit.ptr - b_done_patch) - 8;
            *(uint32_t*)b_done_patch = ARM_COND(COND_AL) | ARM_B(b_done_offset);
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
            // BCC: branch if carry clear (CMP r8, #1 sets Z if carry==1)
            // Taken when ARM Z clear, not-taken when ARM Z set
            block_ended = true;
            emit.cycles += 2;  // Add cycles BEFORE emitting epilogues

            uint8_t* bne_patch = emit.ptr;
            emit.Emit(0); // placeholder for BNE taken

            // Not-taken path: epilogue with current PC
            emit.Emit_Epilogue(pc);

            // B done (skip taken epilogue)
            uint8_t* b_done_patch = emit.ptr;
            emit.Emit(0);

            // Taken path: epilogue with target PC
            emit.Emit_Epilogue(target);

            // Patch BNE to jump to taken epilogue (b_done_patch + 4)
            // Offset = (b_done_patch + 4) - (bne_patch + 8) = (b_done_patch - bne_patch) - 4
            int32_t bne_offset = (int32_t)(b_done_patch - bne_patch) - 4;
            *(uint32_t*)bne_patch = ARM_COND(COND_NE) | ARM_B(bne_offset);

            // Patch B done to skip over taken epilogue to done
            int32_t b_done_offset = (int32_t)(emit.ptr - b_done_patch) - 8;
            *(uint32_t*)b_done_patch = ARM_COND(COND_AL) | ARM_B(b_done_offset);
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
            // BMI: branch if N set (TST r7, #0x80 sets Z if bit 7 clear)
            // Taken when ARM Z clear (N set), not-taken when ARM Z set (N clear)
            block_ended = true;
            emit.cycles += 2;  // Add cycles BEFORE emitting epilogues

            uint8_t* bne_patch = emit.ptr;
            emit.Emit(0); // placeholder for BNE taken

            // Not-taken path: epilogue with current PC
            emit.Emit_Epilogue(pc);

            // B done (skip taken epilogue)
            uint8_t* b_done_patch = emit.ptr;
            emit.Emit(0);

            // Taken path: epilogue with target PC
            emit.Emit_Epilogue(target);

            // Patch BNE to jump to taken epilogue (b_done_patch + 4)
            // Offset = (b_done_patch + 4) - (bne_patch + 8) = (b_done_patch - bne_patch) - 4
            int32_t bne_offset = (int32_t)(b_done_patch - bne_patch) - 4;
            *(uint32_t*)bne_patch = ARM_COND(COND_NE) | ARM_B(bne_offset);

            // Patch B done to skip over taken epilogue to done
            int32_t b_done_offset = (int32_t)(emit.ptr - b_done_patch) - 8;
            *(uint32_t*)b_done_patch = ARM_COND(COND_AL) | ARM_B(b_done_offset);
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
            // BPL: branch if N clear (TST r7, #0x80 sets Z if bit 7 clear)
            // Taken when ARM Z set (N clear), not-taken when ARM Z clear (N set)
            block_ended = true;
            emit.cycles += 2;  // Add cycles BEFORE emitting epilogues

            uint8_t* beq_patch = emit.ptr;
            emit.Emit(0); // placeholder for BEQ taken

            // Taken path: epilogue with target PC
            emit.Emit_Epilogue(target);

            // B done (skip not-taken epilogue)
            uint8_t* b_done_patch = emit.ptr;
            emit.Emit(0);

            // Not-taken path: epilogue with current PC
            emit.Emit_Epilogue(pc);

            // Patch BEQ to jump to taken epilogue (beq_patch + 4 = start of taken epilogue)
            // Offset = (beq_patch + 4) - (beq_patch + 8) = -4
            int32_t beq_offset = -4;
            *(uint32_t*)beq_patch = ARM_COND(COND_EQ) | ARM_B(beq_offset);

            // Patch B done to skip over not-taken epilogue to done
            int32_t b_done_offset = (int32_t)(emit.ptr - b_done_patch) - 8;
            *(uint32_t*)b_done_patch = ARM_COND(COND_AL) | ARM_B(b_done_offset);
        }
        return true;
    }

    // ===== CONTROL =====

    // JMP abs (0x4C)
    case 0x4C: {
        uint16_t target = FetchWordAt(pc);
        pc += 2;  // Consume the operand bytes
        block_ended = true;
        emit.cycles += 3;
        // Emit epilogue with the jump target as exit PC
        emit.Emit_Epilogue(target);
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

    // ===== STACK OPERATIONS =====

    // PHA (0x48) - Push A to stack
    case 0x48: {
        // Load SP from DynarecState
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_STATE, DS_SP);
        // Stack address = 0x100 + SP: ADD r1, r0, #1 ROR 24 (= #0x100)
        emit.Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_ADD, REG_SCRATCH1, REG_SCRATCH0, 1, 12, false));
        // STRB A, [RAM, r1]
        emit.Emit(ARM_COND(COND_AL) | (0x1 << 26) | (1 << 25) | (1 << 24) | (1 << 23) |
                  (1 << 22) | (0 << 21) | (0 << 20) |
                  (REG_RAM << 16) | (REG_A << 12) | REG_SCRATCH1);
        // SP-- with 8-bit wrap
        emit.Emit_SUB_IMM(REG_SCRATCH0, REG_SCRATCH0, 1);
        emit.Emit_AND_IMM(REG_SCRATCH0, REG_SCRATCH0, 0xFF);
        // Store SP back
        emit.Emit_STRB_IMM(REG_SCRATCH0, REG_STATE, DS_SP);
        emit.cycles += 3;
        return true;
    }

    // PLA (0x68) - Pull A from stack
    case 0x68: {
        // Load SP from DynarecState
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_STATE, DS_SP);
        // SP++ with 8-bit wrap
        emit.Emit_ADD_IMM(REG_SCRATCH0, REG_SCRATCH0, 1);
        emit.Emit_AND_IMM(REG_SCRATCH0, REG_SCRATCH0, 0xFF);
        // Store SP back
        emit.Emit_STRB_IMM(REG_SCRATCH0, REG_STATE, DS_SP);
        // Stack address = 0x100 + SP
        emit.Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_ADD, REG_SCRATCH1, REG_SCRATCH0, 1, 12, false));
        // LDRB A, [RAM, r1]
        emit.Emit(ARM_COND(COND_AL) | (0x1 << 26) | (1 << 25) | (1 << 24) | (1 << 23) |
                  (1 << 22) | (0 << 21) | (1 << 20) |
                  (REG_RAM << 16) | (REG_A << 12) | REG_SCRATCH1);
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 4;
        return true;
    }

    // PHP (0x08) - Push status to stack
    case 0x08: {
        // Repack lazy NZ + carry into status byte in r2
        emit.Emit_LDRB_IMM(REG_SCRATCH2, REG_STATE, DS_STATUS);
        emit.Emit_AND_IMM(REG_SCRATCH2, REG_SCRATCH2, 0x7C);  // keep V,B,D,I
        // Set C from carry register
        emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ORR, REG_SCRATCH2, REG_SCRATCH2, REG_CARRY, false));
        // Set Z if NZ==0
        emit.Emit_TST_IMM(REG_NZ, 0xFF);
        emit.Emit(ARM_COND(COND_EQ) | ARM_DP_IMM(DP_ORR, REG_SCRATCH2, REG_SCRATCH2, 0x02, 0, false));
        // Set N if NZ bit 7 set
        emit.Emit_TST_IMM(REG_NZ, 0x80);
        emit.Emit(ARM_COND(COND_NE) | ARM_DP_IMM(DP_ORR, REG_SCRATCH2, REG_SCRATCH2, 0x80, 0, false));
        // PHP always sets B flag (bit 4) and unused bit 5
        emit.Emit_ORR_IMM(REG_SCRATCH2, REG_SCRATCH2, 0x30);

        // Load SP, push status byte
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_STATE, DS_SP);
        emit.Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_ADD, REG_SCRATCH1, REG_SCRATCH0, 1, 12, false));
        // STRB r2, [RAM, r1]
        emit.Emit(ARM_COND(COND_AL) | (0x1 << 26) | (1 << 25) | (1 << 24) | (1 << 23) |
                  (1 << 22) | (0 << 21) | (0 << 20) |
                  (REG_RAM << 16) | (REG_SCRATCH2 << 12) | REG_SCRATCH1);
        // SP--
        emit.Emit_SUB_IMM(REG_SCRATCH0, REG_SCRATCH0, 1);
        emit.Emit_AND_IMM(REG_SCRATCH0, REG_SCRATCH0, 0xFF);
        emit.Emit_STRB_IMM(REG_SCRATCH0, REG_STATE, DS_SP);
        emit.cycles += 3;
        return true;
    }

    // PLP (0x28) - Pull status from stack
    case 0x28: {
        // Load SP, increment, pull byte
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_STATE, DS_SP);
        emit.Emit_ADD_IMM(REG_SCRATCH0, REG_SCRATCH0, 1);
        emit.Emit_AND_IMM(REG_SCRATCH0, REG_SCRATCH0, 0xFF);
        emit.Emit_STRB_IMM(REG_SCRATCH0, REG_STATE, DS_SP);
        emit.Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_ADD, REG_SCRATCH1, REG_SCRATCH0, 1, 12, false));
        // LDRB r2, [RAM, r1]
        emit.Emit(ARM_COND(COND_AL) | (0x1 << 26) | (1 << 25) | (1 << 24) | (1 << 23) |
                  (1 << 22) | (0 << 21) | (1 << 20) |
                  (REG_RAM << 16) | (REG_SCRATCH2 << 12) | REG_SCRATCH1);

        // Store full status to DynarecState (preserves I/D/V bits)
        emit.Emit_STRB_IMM(REG_SCRATCH2, REG_STATE, DS_STATUS);
        // Unpack carry: r8 = status & 1
        emit.Emit_AND_IMM(REG_CARRY, REG_SCRATCH2, 0x01);
        // Unpack lazy NZ: default r7=0x01, if N: r7=0x80, if Z: r7=0x00
        emit.Emit_MOV_IMM(REG_NZ, 0x01);
        emit.Emit_TST_IMM(REG_SCRATCH2, 0x80);
        emit.Emit(ARM_COND(COND_NE) | ARM_DP_IMM(DP_MOV, REG_NZ, 0, 0x80, 0, false));
        emit.Emit_TST_IMM(REG_SCRATCH2, 0x02);
        emit.Emit(ARM_COND(COND_NE) | ARM_DP_IMM(DP_MOV, REG_NZ, 0, 0x00, 0, false));
        emit.cycles += 4;
        return true;
    }

    // ===== SUBROUTINE OPERATIONS =====

    // JSR abs (0x20) - block-ending
    case 0x20: {
        uint16_t target = FetchWordAt(pc);
        pc += 2;
        // 6502 JSR pushes (PC-1) where PC points after the 3-byte JSR instruction
        // pc is already past the operand, so push_addr = pc - 1
        uint16_t push_addr = pc - 1;
        uint8_t push_hi = (push_addr >> 8) & 0xFF;
        uint8_t push_lo = push_addr & 0xFF;

        // Load SP from DynarecState
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_STATE, DS_SP);

        // Push high byte first: RAM[0x100 + SP] = push_hi
        emit.Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_ADD, REG_SCRATCH1, REG_SCRATCH0, 1, 12, false));
        emit.Emit_MOV_IMM(REG_SCRATCH2, push_hi);
        emit.Emit(ARM_COND(COND_AL) | (0x1 << 26) | (1 << 25) | (1 << 24) | (1 << 23) |
                  (1 << 22) | (0 << 21) | (0 << 20) |
                  (REG_RAM << 16) | (REG_SCRATCH2 << 12) | REG_SCRATCH1);
        // SP--
        emit.Emit_SUB_IMM(REG_SCRATCH0, REG_SCRATCH0, 1);
        emit.Emit_AND_IMM(REG_SCRATCH0, REG_SCRATCH0, 0xFF);

        // Push low byte: RAM[0x100 + SP] = push_lo
        emit.Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_ADD, REG_SCRATCH1, REG_SCRATCH0, 1, 12, false));
        emit.Emit_MOV_IMM(REG_SCRATCH2, push_lo);
        emit.Emit(ARM_COND(COND_AL) | (0x1 << 26) | (1 << 25) | (1 << 24) | (1 << 23) |
                  (1 << 22) | (0 << 21) | (0 << 20) |
                  (REG_RAM << 16) | (REG_SCRATCH2 << 12) | REG_SCRATCH1);
        // SP--
        emit.Emit_SUB_IMM(REG_SCRATCH0, REG_SCRATCH0, 1);
        emit.Emit_AND_IMM(REG_SCRATCH0, REG_SCRATCH0, 0xFF);

        // Store SP back
        emit.Emit_STRB_IMM(REG_SCRATCH0, REG_STATE, DS_SP);

        block_ended = true;
        emit.cycles += 6;
        emit.Emit_Epilogue(target);
        return true;
    }

    // RTS (0x60) - block-ending, dynamic exit PC
    case 0x60: {
        // Load SP from DynarecState
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_STATE, DS_SP);

        // SP++ (for low byte)
        emit.Emit_ADD_IMM(REG_SCRATCH0, REG_SCRATCH0, 1);
        emit.Emit_AND_IMM(REG_SCRATCH0, REG_SCRATCH0, 0xFF);
        // r1 = RAM[0x100 + SP] (low byte of return addr)
        emit.Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_ADD, REG_SCRATCH1, REG_SCRATCH0, 1, 12, false));
        emit.Emit(ARM_COND(COND_AL) | (0x1 << 26) | (1 << 25) | (1 << 24) | (1 << 23) |
                  (1 << 22) | (0 << 21) | (1 << 20) |
                  (REG_RAM << 16) | (REG_SCRATCH1 << 12) | REG_SCRATCH1);

        // SP++ (for high byte)
        emit.Emit_ADD_IMM(REG_SCRATCH0, REG_SCRATCH0, 1);
        emit.Emit_AND_IMM(REG_SCRATCH0, REG_SCRATCH0, 0xFF);
        // Store SP back to DynarecState
        emit.Emit_STRB_IMM(REG_SCRATCH0, REG_STATE, DS_SP);

        // r2 = RAM[0x100 + SP] (high byte of return addr)
        emit.Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_ADD, REG_SCRATCH2, REG_SCRATCH0, 1, 12, false));
        emit.Emit(ARM_COND(COND_AL) | (0x1 << 26) | (1 << 25) | (1 << 24) | (1 << 23) |
                  (1 << 22) | (0 << 21) | (1 << 20) |
                  (REG_RAM << 16) | (REG_SCRATCH2 << 12) | REG_SCRATCH2);

        // Combine: r1 = (r2 << 8) | r1
        // ORR r1, r1, r2, LSL #8
        emit.Emit(ARM_COND(COND_AL) | ((uint32_t)DP_ORR << 21) |
                  (REG_SCRATCH1 << 16) | (REG_SCRATCH1 << 12) |
                  (8 << 7) | REG_SCRATCH2);
        // RTS adds 1 to the popped address
        emit.Emit_ADD_IMM(REG_SCRATCH1, REG_SCRATCH1, 1);

        block_ended = true;
        emit.cycles += 6;
        // r1 has the return PC — use dynamic epilogue
        emit.Emit_Epilogue_DynamicPC(REG_SCRATCH1);
        return true;
    }

    // RTI (0x40)
    case 0x40:
        // RTI requires stack pop of status and PC - exit to interpreter
        return false;

    // ===== INCREMENT/DECREMENT MEMORY =====

    // INC zp (0xE6)
    case 0xE6: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_RAM, zp);
        emit.Emit_ADD_IMM(REG_SCRATCH0, REG_SCRATCH0, 1);
        emit.Emit_AND_IMM(REG_SCRATCH0, REG_SCRATCH0, 0xFF);
        emit.Emit_STRB_IMM(REG_SCRATCH0, REG_RAM, zp);
        emit.Emit_UpdateNZ(REG_SCRATCH0);
        emit.cycles += 5;
        return true;
    }

    // DEC zp (0xC6)
    case 0xC6: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_RAM, zp);
        emit.Emit_SUB_IMM(REG_SCRATCH0, REG_SCRATCH0, 1);
        emit.Emit_AND_IMM(REG_SCRATCH0, REG_SCRATCH0, 0xFF);
        emit.Emit_STRB_IMM(REG_SCRATCH0, REG_RAM, zp);
        emit.Emit_UpdateNZ(REG_SCRATCH0);
        emit.cycles += 5;
        return true;
    }

    // INC abs (0xEE)
    case 0xEE: {
        uint16_t addr = FetchWordAt(pc); pc += 2;
        if (addr >= 0x2000) { pc -= 3; return false; } // RAM only
        if (!EmitLoadAbs(emit, REG_SCRATCH0, addr)) { pc -= 3; return false; }
        emit.Emit_ADD_IMM(REG_SCRATCH0, REG_SCRATCH0, 1);
        emit.Emit_AND_IMM(REG_SCRATCH0, REG_SCRATCH0, 0xFF);
        if (!EmitStoreAbs(emit, REG_SCRATCH0, addr)) { pc -= 3; return false; }
        emit.Emit_UpdateNZ(REG_SCRATCH0);
        emit.cycles += 6;
        return true;
    }

    // DEC abs (0xCE)
    case 0xCE: {
        uint16_t addr = FetchWordAt(pc); pc += 2;
        if (addr >= 0x2000) { pc -= 3; return false; }
        if (!EmitLoadAbs(emit, REG_SCRATCH0, addr)) { pc -= 3; return false; }
        emit.Emit_SUB_IMM(REG_SCRATCH0, REG_SCRATCH0, 1);
        emit.Emit_AND_IMM(REG_SCRATCH0, REG_SCRATCH0, 0xFF);
        if (!EmitStoreAbs(emit, REG_SCRATCH0, addr)) { pc -= 3; return false; }
        emit.Emit_UpdateNZ(REG_SCRATCH0);
        emit.cycles += 6;
        return true;
    }

    // ===== ACCUMULATOR SHIFTS =====

    // ASL A (0x0A)
    case 0x0A: {
        // Carry = old bit 7: TST A, #0x80; set carry accordingly
        emit.Emit_TST_IMM(REG_A, 0x80);
        emit.Emit_MOV_IMM(REG_CARRY, 0);
        emit.Emit(ARM_COND(COND_NE) | ARM_DP_IMM(DP_MOV, REG_CARRY, 0, 1, 0, false));
        // A = (A << 1) & 0xFF: ADD A, A, A; AND A, A, #0xFF
        emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ADD, REG_A, REG_A, REG_A, false));
        emit.Emit_AND_IMM(REG_A, REG_A, 0xFF);
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 2;
        return true;
    }

    // LSR A (0x4A)
    case 0x4A: {
        // Carry = old bit 0
        emit.Emit_AND_IMM(REG_CARRY, REG_A, 0x01);
        // A = A >> 1: MOV A, A, LSR #1
        emit.Emit(ARM_COND(COND_AL) | ((uint32_t)DP_MOV << 21) |
                  (REG_A << 12) | (1 << 7) | (1 << 5) | REG_A);
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 2;
        return true;
    }

    // ROL A (0x2A)
    case 0x2A: {
        // temp = (A << 1) | carry
        emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ADD, REG_SCRATCH0, REG_A, REG_A, false));
        emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ORR, REG_SCRATCH0, REG_SCRATCH0, REG_CARRY, false));
        // New carry = old bit 7 (now in bit 8 of temp)
        emit.Emit_TST_IMM(REG_A, 0x80);
        emit.Emit_MOV_IMM(REG_CARRY, 0);
        emit.Emit(ARM_COND(COND_NE) | ARM_DP_IMM(DP_MOV, REG_CARRY, 0, 1, 0, false));
        // A = temp & 0xFF
        emit.Emit_AND_IMM(REG_A, REG_SCRATCH0, 0xFF);
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 2;
        return true;
    }

    // ROR A (0x6A)
    case 0x6A: {
        // Save old bit 0 for new carry
        emit.Emit_AND_IMM(REG_SCRATCH0, REG_A, 0x01);
        // A = (A >> 1) | (carry << 7)
        // MOV A, A, LSR #1
        emit.Emit(ARM_COND(COND_AL) | ((uint32_t)DP_MOV << 21) |
                  (REG_A << 12) | (1 << 7) | (1 << 5) | REG_A);
        // ORR A, A, carry, LSL #7
        emit.Emit(ARM_COND(COND_AL) | ((uint32_t)DP_ORR << 21) |
                  (REG_A << 16) | (REG_A << 12) | (7 << 7) | REG_CARRY);
        // Carry = old bit 0
        emit.Emit_MOV(REG_CARRY, REG_SCRATCH0);
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 2;
        return true;
    }

    // ===== ZERO-PAGE SHIFTS =====

    // ASL zp (0x06)
    case 0x06: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_RAM, zp);
        // Carry = bit 7
        emit.Emit_TST_IMM(REG_SCRATCH0, 0x80);
        emit.Emit_MOV_IMM(REG_CARRY, 0);
        emit.Emit(ARM_COND(COND_NE) | ARM_DP_IMM(DP_MOV, REG_CARRY, 0, 1, 0, false));
        // Shift left
        emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ADD, REG_SCRATCH0, REG_SCRATCH0, REG_SCRATCH0, false));
        emit.Emit_AND_IMM(REG_SCRATCH0, REG_SCRATCH0, 0xFF);
        emit.Emit_STRB_IMM(REG_SCRATCH0, REG_RAM, zp);
        emit.Emit_UpdateNZ(REG_SCRATCH0);
        emit.cycles += 5;
        return true;
    }

    // LSR zp (0x46)
    case 0x46: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_RAM, zp);
        emit.Emit_AND_IMM(REG_CARRY, REG_SCRATCH0, 0x01);
        emit.Emit(ARM_COND(COND_AL) | ((uint32_t)DP_MOV << 21) |
                  (REG_SCRATCH0 << 12) | (1 << 7) | (1 << 5) | REG_SCRATCH0);
        emit.Emit_STRB_IMM(REG_SCRATCH0, REG_RAM, zp);
        emit.Emit_UpdateNZ(REG_SCRATCH0);
        emit.cycles += 5;
        return true;
    }

    // ROL zp (0x26)
    case 0x26: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_RAM, zp);
        // temp = (val << 1) | carry
        emit.Emit_MOV(REG_SCRATCH1, REG_SCRATCH0); // save for carry check
        emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ADD, REG_SCRATCH0, REG_SCRATCH0, REG_SCRATCH0, false));
        emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ORR, REG_SCRATCH0, REG_SCRATCH0, REG_CARRY, false));
        // New carry = old bit 7
        emit.Emit_TST_IMM(REG_SCRATCH1, 0x80);
        emit.Emit_MOV_IMM(REG_CARRY, 0);
        emit.Emit(ARM_COND(COND_NE) | ARM_DP_IMM(DP_MOV, REG_CARRY, 0, 1, 0, false));
        emit.Emit_AND_IMM(REG_SCRATCH0, REG_SCRATCH0, 0xFF);
        emit.Emit_STRB_IMM(REG_SCRATCH0, REG_RAM, zp);
        emit.Emit_UpdateNZ(REG_SCRATCH0);
        emit.cycles += 5;
        return true;
    }

    // ROR zp (0x66)
    case 0x66: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_RAM, zp);
        // Save old bit 0
        emit.Emit_AND_IMM(REG_SCRATCH1, REG_SCRATCH0, 0x01);
        // val = (val >> 1) | (carry << 7)
        emit.Emit(ARM_COND(COND_AL) | ((uint32_t)DP_MOV << 21) |
                  (REG_SCRATCH0 << 12) | (1 << 7) | (1 << 5) | REG_SCRATCH0);
        emit.Emit(ARM_COND(COND_AL) | ((uint32_t)DP_ORR << 21) |
                  (REG_SCRATCH0 << 16) | (REG_SCRATCH0 << 12) | (7 << 7) | REG_CARRY);
        emit.Emit_MOV(REG_CARRY, REG_SCRATCH1);
        emit.Emit_STRB_IMM(REG_SCRATCH0, REG_RAM, zp);
        emit.Emit_UpdateNZ(REG_SCRATCH0);
        emit.cycles += 5;
        return true;
    }

    // ===== ADDITIONAL LOADS =====

    // LDX abs (0xAE)
    case 0xAE: {
        uint16_t addr = FetchWordAt(pc); pc += 2;
        if (!EmitLoadAbs(emit, REG_X, addr)) { pc -= 3; return false; }
        emit.Emit_UpdateNZ(REG_X);
        emit.cycles += 4;
        return true;
    }

    // LDY abs (0xAC)
    case 0xAC: {
        uint16_t addr = FetchWordAt(pc); pc += 2;
        if (!EmitLoadAbs(emit, REG_Y, addr)) { pc -= 3; return false; }
        emit.Emit_UpdateNZ(REG_Y);
        emit.cycles += 4;
        return true;
    }

    // LDY zp,X (0xB4)
    case 0xB4: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_ADD_IMM(REG_SCRATCH0, REG_X, zp);
        emit.Emit_AND_IMM(REG_SCRATCH0, REG_SCRATCH0, 0xFF);
        emit.Emit_LDRB_REG(REG_Y, REG_RAM, REG_SCRATCH0);
        emit.Emit_UpdateNZ(REG_Y);
        emit.cycles += 4;
        return true;
    }

    // LDX zp,Y (0xB6)
    case 0xB6: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_ADD_IMM(REG_SCRATCH0, REG_Y, zp);
        emit.Emit_AND_IMM(REG_SCRATCH0, REG_SCRATCH0, 0xFF);
        emit.Emit_LDRB_REG(REG_X, REG_RAM, REG_SCRATCH0);
        emit.Emit_UpdateNZ(REG_X);
        emit.cycles += 4;
        return true;
    }

    // LDA abs,X (0xBD)
    case 0xBD: {
        uint16_t base = FetchWordAt(pc); pc += 2;
        if (base < 0x2000 && (base + 0xFF) < 0x2000) {
            emit.LoadImm16(REG_SCRATCH0, base);
            emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ADD, REG_SCRATCH0, REG_SCRATCH0, REG_X, false));
            emit.Emit_LDRB_REG(REG_A, REG_RAM, REG_SCRATCH0);
        } else if (base >= 0x8000 && base < 0xC000 && ((base & 0x3FFF) + 0xFF) < 0x4000) {
            emit.LoadImm16(REG_SCRATCH0, base & 0x3FFF);
            emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ADD, REG_SCRATCH0, REG_SCRATCH0, REG_X, false));
            emit.Emit_LDRB_REG(REG_A, REG_ROM_LO, REG_SCRATCH0);
        } else if (base >= 0xC000 && ((base & 0x3FFF) + 0xFF) < 0x4000) {
            emit.LoadImm16(REG_SCRATCH0, base & 0x3FFF);
            emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ADD, REG_SCRATCH0, REG_SCRATCH0, REG_X, false));
            emit.Emit_LDRB_REG(REG_A, REG_ROM_HI, REG_SCRATCH0);
        } else {
            pc -= 3; return false;
        }
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 4;
        return true;
    }

    // LDA abs,Y (0xB9)
    case 0xB9: {
        uint16_t base = FetchWordAt(pc); pc += 2;
        if (base < 0x2000 && (base + 0xFF) < 0x2000) {
            emit.LoadImm16(REG_SCRATCH0, base);
            emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ADD, REG_SCRATCH0, REG_SCRATCH0, REG_Y, false));
            emit.Emit_LDRB_REG(REG_A, REG_RAM, REG_SCRATCH0);
        } else if (base >= 0x8000 && base < 0xC000 && ((base & 0x3FFF) + 0xFF) < 0x4000) {
            emit.LoadImm16(REG_SCRATCH0, base & 0x3FFF);
            emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ADD, REG_SCRATCH0, REG_SCRATCH0, REG_Y, false));
            emit.Emit_LDRB_REG(REG_A, REG_ROM_LO, REG_SCRATCH0);
        } else if (base >= 0xC000 && ((base & 0x3FFF) + 0xFF) < 0x4000) {
            emit.LoadImm16(REG_SCRATCH0, base & 0x3FFF);
            emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ADD, REG_SCRATCH0, REG_SCRATCH0, REG_Y, false));
            emit.Emit_LDRB_REG(REG_A, REG_ROM_HI, REG_SCRATCH0);
        } else {
            pc -= 3; return false;
        }
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 4;
        return true;
    }

    // LDA (zp),Y (0xB1)
    case 0xB1: {
        uint8_t zp = FetchByteAt(pc++);
        uint8_t zp1 = (zp + 1) & 0xFF;
        // Load 16-bit pointer from zero page
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_RAM, zp);
        emit.Emit_LDRB_IMM(REG_SCRATCH1, REG_RAM, zp1);
        // ORR r0, r0, r1, LSL #8
        emit.Emit(ARM_COND(COND_AL) | ((uint32_t)DP_ORR << 21) |
                  (REG_SCRATCH0 << 16) | (REG_SCRATCH0 << 12) |
                  (8 << 7) | REG_SCRATCH1);
        // ADD r0, r0, Y
        emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ADD, REG_SCRATCH0, REG_SCRATCH0, REG_Y, false));
        // Runtime dispatch: CMP r0, #0x2000 (imm8=2, rot=10)
        emit.Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_CMP, 0, REG_SCRATCH0, 0x02, 10, true));
        uint8_t* bcs_rom = emit.ptr;
        emit.Emit(0); // placeholder BCS (addr >= 0x2000)
        // RAM path
        emit.Emit_LDRB_REG(REG_A, REG_RAM, REG_SCRATCH0);
        uint8_t* b_done = emit.ptr;
        emit.Emit(0); // placeholder B done
        // ROM path
        uint8_t* rom_label = emit.ptr;
        // CMP r0, #0x8000 (imm8=0x80, rot=12)
        emit.Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_CMP, 0, REG_SCRATCH0, 0x80, 12, true));
        uint8_t* bcc_bail = emit.ptr;
        emit.Emit(0); // placeholder BCC bail (I/O range)
        // BIC r1, r0, #0xC000 (imm8=0xC0, rot=12) → offset = addr & 0x3FFF
        emit.Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_BIC, REG_SCRATCH1, REG_SCRATCH0, 0xC0, 12, false));
        // TST r0, #0x4000 (imm8=0x01, rot=9) — which bank?
        emit.Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_TST, 0, REG_SCRATCH0, 0x01, 9, true));
        // Conditional LDRB from ROM_LO or ROM_HI
        emit.Emit(ARM_COND(COND_EQ) | (0x1 << 26) | (1 << 25) | (1 << 24) | (1 << 23) |
                  (1 << 22) | (0 << 21) | (1 << 20) |
                  (REG_ROM_LO << 16) | (REG_A << 12) | REG_SCRATCH1);
        emit.Emit(ARM_COND(COND_NE) | (0x1 << 26) | (1 << 25) | (1 << 24) | (1 << 23) |
                  (1 << 22) | (0 << 21) | (1 << 20) |
                  (REG_ROM_HI << 16) | (REG_A << 12) | REG_SCRATCH1);
        uint8_t* b_done2 = emit.ptr;
        emit.Emit(0); // placeholder B done
        // Bail: exit to interpreter at this instruction
        uint8_t* bail_label = emit.ptr;
        emit.Emit_Epilogue(pc - 2);
        // Done label
        uint8_t* done_label = emit.ptr;
        // Patch branches
        *(uint32_t*)bcs_rom = ARM_COND(COND_CS) | ARM_B((int32_t)(rom_label - bcs_rom) - 8);
        *(uint32_t*)b_done = ARM_COND(COND_AL) | ARM_B((int32_t)(done_label - b_done) - 8);
        *(uint32_t*)bcc_bail = ARM_COND(COND_CC) | ARM_B((int32_t)(bail_label - bcc_bail) - 8);
        *(uint32_t*)b_done2 = ARM_COND(COND_AL) | ARM_B((int32_t)(done_label - b_done2) - 8);
        emit.Emit_UpdateNZ(REG_A);
        emit.cycles += 5;
        return true;
    }

    // STA (zp),Y (0x91) - RAM only, bail for I/O/ROM
    case 0x91: {
        uint8_t zp = FetchByteAt(pc++);
        uint8_t zp1 = (zp + 1) & 0xFF;
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_RAM, zp);
        emit.Emit_LDRB_IMM(REG_SCRATCH1, REG_RAM, zp1);
        emit.Emit(ARM_COND(COND_AL) | ((uint32_t)DP_ORR << 21) |
                  (REG_SCRATCH0 << 16) | (REG_SCRATCH0 << 12) |
                  (8 << 7) | REG_SCRATCH1);
        emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ADD, REG_SCRATCH0, REG_SCRATCH0, REG_Y, false));
        // CMP r0, #0x2000
        emit.Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_CMP, 0, REG_SCRATCH0, 0x02, 10, true));
        uint8_t* bcs_bail = emit.ptr;
        emit.Emit(0); // placeholder BCS bail
        // RAM store
        emit.Emit_STRB_REG(REG_A, REG_RAM, REG_SCRATCH0);
        uint8_t* b_done = emit.ptr;
        emit.Emit(0); // placeholder B done
        // Bail
        uint8_t* bail_label = emit.ptr;
        emit.Emit_Epilogue(pc - 2);
        uint8_t* done_label = emit.ptr;
        *(uint32_t*)bcs_bail = ARM_COND(COND_CS) | ARM_B((int32_t)(bail_label - bcs_bail) - 8);
        *(uint32_t*)b_done = ARM_COND(COND_AL) | ARM_B((int32_t)(done_label - b_done) - 8);
        emit.cycles += 6;
        return true;
    }

    // ===== ADDITIONAL STORES =====

    // STX abs (0x8E)
    case 0x8E: {
        uint16_t addr = FetchWordAt(pc); pc += 2;
        if (!EmitStoreAbs(emit, REG_X, addr)) { pc -= 3; return false; }
        emit.cycles += 4;
        return true;
    }

    // STA zp,X (0x95)
    case 0x95: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_ADD_IMM(REG_SCRATCH0, REG_X, zp);
        emit.Emit_AND_IMM(REG_SCRATCH0, REG_SCRATCH0, 0xFF);
        emit.Emit_STRB_REG(REG_A, REG_RAM, REG_SCRATCH0);
        emit.cycles += 4;
        return true;
    }

    // STA abs,X (0x9D) - RAM only
    case 0x9D: {
        uint16_t base = FetchWordAt(pc); pc += 2;
        if (base >= 0x2000 || (base + 0xFF) >= 0x2000) { pc -= 3; return false; }
        emit.LoadImm16(REG_SCRATCH0, base);
        emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ADD, REG_SCRATCH0, REG_SCRATCH0, REG_X, false));
        emit.Emit_STRB_REG(REG_A, REG_RAM, REG_SCRATCH0);
        emit.cycles += 5;
        return true;
    }

    // STA abs,Y (0x99) - RAM only
    case 0x99: {
        uint16_t base = FetchWordAt(pc); pc += 2;
        if (base >= 0x2000 || (base + 0xFF) >= 0x2000) { pc -= 3; return false; }
        emit.LoadImm16(REG_SCRATCH0, base);
        emit.Emit(ARM_COND(COND_AL) | ARM_DP(DP_ADD, REG_SCRATCH0, REG_SCRATCH0, REG_Y, false));
        emit.Emit_STRB_REG(REG_A, REG_RAM, REG_SCRATCH0);
        emit.cycles += 5;
        return true;
    }

    // ===== ARITHMETIC =====

    // ADC #imm (0x69)
    case 0x69: {
        uint8_t imm = FetchByteAt(pc++);
        emit.Emit_MOV_IMM(REG_SCRATCH1, imm);
        EmitADC(emit, REG_SCRATCH1);
        emit.cycles += 2;
        return true;
    }

    // ADC zp (0x65)
    case 0x65: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_LDRB_IMM(REG_SCRATCH1, REG_RAM, zp);
        EmitADC(emit, REG_SCRATCH1);
        emit.cycles += 3;
        return true;
    }

    // ADC abs (0x6D)
    case 0x6D: {
        uint16_t addr = FetchWordAt(pc); pc += 2;
        if (!EmitLoadAbs(emit, REG_SCRATCH1, addr)) { pc -= 3; return false; }
        EmitADC(emit, REG_SCRATCH1);
        emit.cycles += 4;
        return true;
    }

    // SBC #imm (0xE9)
    case 0xE9: {
        uint8_t imm = FetchByteAt(pc++);
        emit.Emit_MOV_IMM(REG_SCRATCH1, imm);
        EmitSBC(emit, REG_SCRATCH1);
        emit.cycles += 2;
        return true;
    }

    // SBC zp (0xE5)
    case 0xE5: {
        uint8_t zp = FetchByteAt(pc++);
        emit.Emit_LDRB_IMM(REG_SCRATCH1, REG_RAM, zp);
        EmitSBC(emit, REG_SCRATCH1);
        emit.cycles += 3;
        return true;
    }

    // SBC abs (0xED)
    case 0xED: {
        uint16_t addr = FetchWordAt(pc); pc += 2;
        if (!EmitLoadAbs(emit, REG_SCRATCH1, addr)) { pc -= 3; return false; }
        EmitSBC(emit, REG_SCRATCH1);
        emit.cycles += 4;
        return true;
    }

    // ===== STATUS FLAG OPERATIONS =====

    // CLD (0xD8)
    case 0xD8: {
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_STATE, DS_STATUS);
        // BIC r0, r0, #0x08 (clear D flag)
        emit.Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_BIC, REG_SCRATCH0, REG_SCRATCH0, 0x08, 0, false));
        emit.Emit_STRB_IMM(REG_SCRATCH0, REG_STATE, DS_STATUS);
        emit.cycles += 2;
        return true;
    }

    // SED (0xF8)
    case 0xF8: {
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_STATE, DS_STATUS);
        emit.Emit_ORR_IMM(REG_SCRATCH0, REG_SCRATCH0, 0x08);
        emit.Emit_STRB_IMM(REG_SCRATCH0, REG_STATE, DS_STATUS);
        emit.cycles += 2;
        return true;
    }

    // CLI (0x58)
    case 0x58: {
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_STATE, DS_STATUS);
        emit.Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_BIC, REG_SCRATCH0, REG_SCRATCH0, 0x04, 0, false));
        emit.Emit_STRB_IMM(REG_SCRATCH0, REG_STATE, DS_STATUS);
        emit.cycles += 2;
        return true;
    }

    // SEI (0x78)
    case 0x78: {
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_STATE, DS_STATUS);
        emit.Emit_ORR_IMM(REG_SCRATCH0, REG_SCRATCH0, 0x04);
        emit.Emit_STRB_IMM(REG_SCRATCH0, REG_STATE, DS_STATUS);
        emit.cycles += 2;
        return true;
    }

    // CLV (0xB8)
    case 0xB8: {
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_STATE, DS_STATUS);
        emit.Emit(ARM_COND(COND_AL) | ARM_DP_IMM(DP_BIC, REG_SCRATCH0, REG_SCRATCH0, 0x40, 0, false));
        emit.Emit_STRB_IMM(REG_SCRATCH0, REG_STATE, DS_STATUS);
        emit.cycles += 2;
        return true;
    }

    // ===== TRANSFERS (additional) =====

    // TXS (0x9A) - no flags affected
    case 0x9A: {
        emit.Emit_STRB_IMM(REG_X, REG_STATE, DS_SP);
        emit.cycles += 2;
        return true;
    }

    // TSX (0xBA)
    case 0xBA: {
        emit.Emit_LDRB_IMM(REG_X, REG_STATE, DS_SP);
        emit.Emit_UpdateNZ(REG_X);
        emit.cycles += 2;
        return true;
    }

    // ===== BVC/BVS (overflow branches) =====

    // BVC (0x50)
    case 0x50: {
        int8_t offset = (int8_t)FetchByteAt(pc++);
        uint16_t target = pc + offset;
        // Load status, test V bit (0x40)
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_STATE, DS_STATUS);
        emit.Emit_TST_IMM(REG_SCRATCH0, 0x40);
        // BVC: branch if V clear → taken when ARM Z set (TST result zero)
        int32_t arm_target = emit.GetARMOffset(target);
        if (arm_target >= 0 && offset < 0) {
            int32_t branch_offset = arm_target - (int32_t)emit.CurrentOffset() - 8;
            emit.Emit_B(branch_offset, COND_EQ);
            emit.cycles += 3;
        } else {
            block_ended = true;
            emit.cycles += 2;
            // EQ = taken (V clear), NE = not taken (V set)
            uint8_t* beq_patch = emit.ptr;
            emit.Emit(0); // placeholder BEQ taken
            // Not-taken epilogue
            emit.Emit_Epilogue(pc);
            // Taken epilogue
            uint8_t* taken_label = emit.ptr;
            emit.Emit_Epilogue(target);
            // Patch BEQ to taken
            *(uint32_t*)beq_patch = ARM_COND(COND_EQ) | ARM_B((int32_t)(taken_label - beq_patch) - 8);
        }
        return true;
    }

    // BVS (0x70)
    case 0x70: {
        int8_t offset = (int8_t)FetchByteAt(pc++);
        uint16_t target = pc + offset;
        emit.Emit_LDRB_IMM(REG_SCRATCH0, REG_STATE, DS_STATUS);
        emit.Emit_TST_IMM(REG_SCRATCH0, 0x40);
        // BVS: branch if V set → taken when ARM Z clear (NE)
        int32_t arm_target = emit.GetARMOffset(target);
        if (arm_target >= 0 && offset < 0) {
            int32_t branch_offset = arm_target - (int32_t)emit.CurrentOffset() - 8;
            emit.Emit_B(branch_offset, COND_NE);
            emit.cycles += 3;
        } else {
            block_ended = true;
            emit.cycles += 2;
            // NE = taken (V set), EQ = not taken (V clear)
            uint8_t* bne_patch = emit.ptr;
            emit.Emit(0); // placeholder BNE taken
            // Not-taken epilogue
            emit.Emit_Epilogue(pc);
            // Taken epilogue
            uint8_t* taken_label = emit.ptr;
            emit.Emit_Epilogue(target);
            *(uint32_t*)bne_patch = ARM_COND(COND_NE) | ARM_B((int32_t)(taken_label - bne_patch) - 8);
        }
        return true;
    }

    default:
        return false;
    }
}

void* GetBlock(uint16_t pc) {
    Block* b = FindBlock(pc);
    if (b) {
        // Validate code pointer is within buffer
        uint8_t* code = (uint8_t*)b->code;
        if (code < dynarec_code_buffer ||
            code >= dynarec_code_buffer + CODE_BUFFER_SIZE) {
            DebugLog("DR: ERROR: block %04X has bad code ptr %p\n", pc, code);
            return nullptr;
        }
        b->exec_count++;
        stats.blocks_executed++;
        DebugLog("DR: GetBlock(%04X) -> %p\n", pc, b->code);
        return b->code;
    }
    return nullptr;
}

void InvalidateAll() {
    std::memset(block_table, 0, sizeof(block_table));
    block_pool_used = 0;
    code_ptr = dynarec_code_buffer;
    fail_cache_count = 0;
    stats.blocks_invalidated += stats.blocks_compiled;
    stats.blocks_compiled = 0;
    stats.compile_bytes_used = 0;
}

int RunBlock(void* code, DynarecState* state) {
    // Validate code pointer is within our buffer
    uint8_t* code_bytes = (uint8_t*)code;
    if (code_bytes < dynarec_code_buffer ||
        code_bytes >= dynarec_code_buffer + CODE_BUFFER_SIZE) {
        DebugLog("DR: ERROR: code ptr %p outside buffer [%p, %p)\n",
               code, dynarec_code_buffer, dynarec_code_buffer + CODE_BUFFER_SIZE);
        return 0;
    }

    // Verify code looks valid before calling
    uint32_t first_insn = *(uint32_t*)code;
    if (first_insn != 0xE92D5FF0) {
        DebugLog("DR: ERROR: first insn %08X != expected PUSH %08X\n", first_insn, 0xE92D5FF0);
        return 0;
    }

    // Call compiled block: int block_func(DynarecState*)
    DebugLog("DR: calling block at %p (first insn %08X)\n", code, first_insn);
    typedef int (*BlockFunc)(DynarecState*);
    BlockFunc func = (BlockFunc)code;
    func(state);
    DebugLog("DR: block returned, cycles_executed=%d\n", state->cycles_executed);
    return state->cycles_executed;
}

Stats GetStats() {
    return stats;
}

void ResetStats() {
    stats = {};
}

} // namespace Dynarec
