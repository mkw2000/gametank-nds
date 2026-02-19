#include "dynarec_cpu.h"
#include "dynarec.h"
#include "mos6502.h"
#include "../system_state.h"
#include "../nds_platform.h"

// External 6502 state from mos6502.cpp
extern uint8_t* cached_ram_ptr;
extern uint8_t* cached_rom_lo_ptr;
extern uint8_t* cached_rom_hi_ptr;

// Global active CPU pointer from mos6502.cpp
extern mos6502* g_activeCPU;

namespace Dynarec {

static bool system_initialized = false;

// DynarecState in DTCM for fast access
#if defined(NDS_BUILD) && defined(ARM9)
DTCM_BSS static DynarecState dynarec_state;
#else
static DynarecState dynarec_state;
#endif

void InitSystem() {
    if (!system_initialized) {
        Init();
        system_initialized = true;
    }
}

void ShutdownSystem() {
    if (system_initialized) {
        Shutdown();
        system_initialized = false;
    }
}

bool CanUseDynarec() {
    if (!system_initialized) {
        InitSystem();
    }

    if (!g_activeCPU) return false;

    // PC must be in ROM for block compilation
    uint16_t pc = g_activeCPU->pc;
    if (pc < 0x8000) return false;

    // Decimal mode is complex - skip dynarec
    if (g_activeCPU->status & 0x08) return false;

    return true;
}

int RunDynarec(int cycles) {
    if (!g_activeCPU) return 0;

    // Pack CPU state into DynarecState
    dynarec_state.A = g_activeCPU->A;
    dynarec_state.X = g_activeCPU->X;
    dynarec_state.Y = g_activeCPU->Y;
    dynarec_state.SP = g_activeCPU->sp;
    dynarec_state.status = g_activeCPU->status;
    dynarec_state.PC = g_activeCPU->pc;
    dynarec_state.cycles_remaining = cycles;
    dynarec_state.cycles_executed = 0;
    dynarec_state.ram = cached_ram_ptr;
    dynarec_state.rom_lo = cached_rom_lo_ptr;
    dynarec_state.rom_hi = cached_rom_hi_ptr;
    dynarec_state.exit_reason = 0;

    uint16_t pc = g_activeCPU->pc;

    // Find or compile block
    void* code = GetBlock(pc);
    if (!code) {
        code = CompileBlock(pc);
        if (!code) {
            return 0;  // Fallback to interpreter
        }
    }

    // Execute compiled block
    int executed = RunBlock(code, &dynarec_state);

    if (executed > 0) {
        // Unpack DynarecState back to CPU
        g_activeCPU->A = dynarec_state.A;
        g_activeCPU->X = dynarec_state.X;
        g_activeCPU->Y = dynarec_state.Y;
        g_activeCPU->sp = dynarec_state.SP;
        g_activeCPU->status = dynarec_state.status;
        g_activeCPU->pc = dynarec_state.PC;
    }

    return executed;
}

} // namespace Dynarec
