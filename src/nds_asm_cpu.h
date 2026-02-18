#pragma once
#include <cstdint>

// Interface struct for ARM assembly 6502 dispatch loop.
// Layout must match the assembly code in mos6502_hot_arm.s exactly.
struct AsmCpuState {
    uint8_t A;              // offset 0
    uint8_t X;              // offset 1
    uint8_t Y;              // offset 2
    uint8_t sp;             // offset 3
    uint8_t status;         // offset 4
    uint8_t exit_reason;    // offset 5: 0=done, 1=unhandled, 2=io
    uint8_t exit_opcode;    // offset 6
    uint8_t pad;            // offset 7
    uint16_t pc;            // offset 8
    uint16_t exit_addr;     // offset 10
    int32_t cycles_remaining; // offset 12
    uint8_t exit_value;     // offset 16 (for io write value)
    uint8_t exit_is_write;  // offset 17
    uint8_t pad2[2];        // offset 18-19
};

extern "C" void mos6502_run_asm(
    AsmCpuState* state,      // r0
    uint8_t* ram_ptr,         // r1
    uint8_t* rom_lo_ptr,      // r2
    uint8_t* rom_hi_ptr,      // r3
    // on stack:
    bool* ram_init_ptr,
    uint8_t* via_regs_ptr
);
