#pragma once
#include <cstdint>
#include <cstddef>

// NDS Dynarec for MOS 6502
// Compiles 6502 code blocks to ARM9 code in ITCM

namespace Dynarec {

// Configuration
constexpr size_t CODE_BUFFER_SIZE = 4 * 1024;   // 4KB - placed in main RAM for now
constexpr int MAX_BLOCK_SIZE = 64;              // Max instructions per block
constexpr int MAX_BLOCK_CYCLES = 200;           // Max cycles per block

// State passed between interpreter and compiled blocks
// Lives in DTCM for fast access
struct DynarecState {
    uint8_t A;                  // 0: Accumulator
    uint8_t X;                  // 1: X index
    uint8_t Y;                  // 2: Y index
    uint8_t SP;                 // 3: Stack pointer
    uint8_t status;             // 4: Full 6502 status register
    uint8_t _pad[1];            // 5: padding
    uint16_t PC;                // 6: Program counter
    int32_t cycles_remaining;   // 8: Cycles left to execute
    int32_t cycles_executed;    // 12: Cycles consumed by block
    uint8_t* ram;               // 16: RAM base pointer
    const uint8_t* rom_lo;      // 20: ROM lo bank pointer
    const uint8_t* rom_hi;      // 24: ROM hi bank pointer
    uint8_t exit_reason;        // 28: 0=normal, 1=unsupported opcode, 2=I/O
    uint8_t _pad2[3];           // 29-31: padding
};

// Offsets for assembly access (must match struct layout above)
constexpr int DS_A              = 0;
constexpr int DS_X              = 1;
constexpr int DS_Y              = 2;
constexpr int DS_SP             = 3;
constexpr int DS_STATUS         = 4;
constexpr int DS_PC             = 6;
constexpr int DS_CYCLES_REM     = 8;
constexpr int DS_CYCLES_EXEC    = 12;
constexpr int DS_RAM            = 16;
constexpr int DS_ROM_LO         = 20;
constexpr int DS_ROM_HI         = 24;
constexpr int DS_EXIT_REASON    = 28;

// Compiled block metadata
struct Block {
    uint16_t pc;           // 6502 start address
    uint16_t end_pc;       // PC after last compiled instruction
    void* code;            // ARM code pointer (in ITCM)
    uint32_t cycles;       // Total cycles for this block
    uint32_t exec_count;   // For hotness tracking
    Block* next;           // Hash collision chain
};

// Initialize dynarec system
void Init();

// Shutdown and free resources
void Shutdown();

// Compile a block starting at given PC
// Returns pointer to compiled code, or nullptr if compilation failed
void* CompileBlock(uint16_t pc);

// Invalidate all blocks (call on ROM bank switch)
void InvalidateAll();

// Get compiled block for PC (returns nullptr if not compiled)
void* GetBlock(uint16_t pc);

// Run a compiled block with DynarecState
// Returns cycles consumed
int RunBlock(void* code, DynarecState* state);

// Statistics
struct Stats {
    uint32_t blocks_compiled;
    uint32_t blocks_executed;
    uint32_t blocks_invalidated;
    uint32_t compile_bytes_used;
    uint32_t compile_bytes_total;
    uint32_t fallback_count;
    uint8_t  last_fail_opcode;    // Opcode that caused most recent fallback
    uint16_t last_fail_pc;        // PC where most recent fallback happened
};

Stats GetStats();
void ResetStats();

} // namespace Dynarec
