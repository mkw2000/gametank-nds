#include "dynarec_cpu.h"
#include "dynarec.h"
#include "mos6502.h"
#include "../system_state.h"
#include "../nds_platform.h"
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

// External 6502 state from mos6502.cpp
extern uint8_t* cached_ram_ptr;
extern uint8_t* cached_rom_lo_ptr;
extern uint8_t* cached_rom_hi_ptr;

// Global active CPU pointer from mos6502.cpp
extern mos6502* g_activeCPU;

// ROM type from gte.cpp
extern uint8_t loadedRomType;
constexpr uint8_t ROM_TYPE_EEPROM8K = 1;
constexpr uint8_t ROM_TYPE_EEPROM32K = 2;
constexpr uint8_t ROM_TYPE_FLASH2M = 3;
constexpr uint8_t ROM_TYPE_FLASH2M_RAM32K = 4;

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

static uint32_t canuse_calls = 0;

bool CanUseDynarec() {
    canuse_calls++;

    if (!system_initialized) {
        InitSystem();
    }

    if (!g_activeCPU) {
        if ((canuse_calls & 0xFF) == 0) DebugLog("DR: CanUseDynarec false - no active CPU\n");
        return false;
    }

    // ROM pointers must be initialized
    if (!cached_ram_ptr || !cached_rom_lo_ptr || !cached_rom_hi_ptr) {
        if ((canuse_calls & 0xFF) == 0) DebugLog("DR: CanUseDynarec false - ROM ptrs not init ram=%p lo=%p hi=%p\n",
            cached_ram_ptr, cached_rom_lo_ptr, cached_rom_hi_ptr);
        return false;
    }

    // PC must be in ROM for block compilation
    uint16_t pc = g_activeCPU->pc;
    if (pc < 0x8000) {
        if ((canuse_calls & 0xFF) == 0) DebugLog("DR: CanUseDynarec false - PC=%04X < 8000\n", pc);
        return false;
    }

    // Decimal mode is complex - skip dynarec
    if (g_activeCPU->status & 0x08) {
        if ((canuse_calls & 0xFF) == 0) DebugLog("DR: CanUseDynarec false - decimal mode\n");
        return false;
    }

    // Only allow ROM types with valid pointers
    if (loadedRomType != ROM_TYPE_EEPROM8K && loadedRomType != ROM_TYPE_EEPROM32K &&
        loadedRomType != ROM_TYPE_FLASH2M && loadedRomType != ROM_TYPE_FLASH2M_RAM32K) {
        if ((canuse_calls & 0xFF) == 0) DebugLog("DR: CanUseDynarec false - bad ROM type %d\n", loadedRomType);
        return false;
    }

    if ((canuse_calls & 0xFF) == 0) DebugLog("DR: CanUseDynarec TRUE PC=%04X\n", pc);
    return true;
}

static uint32_t total_dynarec_cycles = 0;
static uint32_t total_dynarec_invocations = 0;

int RunDynarec(int cycles) {
    if (!g_activeCPU) return 0;

    total_dynarec_invocations++;

    // Pack CPU state into DynarecState (once)
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

    int total_executed = 0;
    int remaining = cycles;

    // Multi-block execution loop: stay in dynarec as long as possible
    while (remaining > 0) {
        uint16_t pc = dynarec_state.PC;

        // Only continue if PC is in ROM space
        if (pc < 0x8000) break;

        // Find or compile block
        void* code = GetBlock(pc);
        if (!code) {
            code = CompileBlock(pc);
            if (!code) break;  // Unsupported opcode — fall back to interpreter
        }

        // Execute block directly via function pointer
        typedef int (*BlockFunc)(DynarecState*);
        BlockFunc func = (BlockFunc)code;
        func(&dynarec_state);

        int block_cycles = dynarec_state.cycles_executed;
        if (block_cycles <= 0) break;  // Safety: avoid infinite loop

        total_executed += block_cycles;
        remaining -= block_cycles;
    }

    if (total_executed > 0) {
        // Unpack DynarecState back to CPU (once)
        uint16_t exit_pc = dynarec_state.PC;
        bool valid_pc = (exit_pc < 0x2000) || (exit_pc >= 0x8000);
        if (valid_pc) {
            g_activeCPU->A = dynarec_state.A;
            g_activeCPU->X = dynarec_state.X;
            g_activeCPU->Y = dynarec_state.Y;
            g_activeCPU->sp = dynarec_state.SP;
            g_activeCPU->status = dynarec_state.status;
            g_activeCPU->pc = dynarec_state.PC;
        }
        total_dynarec_cycles += total_executed;
    }

    return total_executed;
}

} // namespace Dynarec
