#pragma once
#include <cstdint>

// Integration interface for MOS6502 to use Dynarec
// This wraps the dynarec system for the CPU class

namespace Dynarec {

// Run cycles using dynarec, fallback to interpreter when needed
// Returns actual cycles executed
int RunDynarec(int cycles);

// Check if we should use dynarec for current state
bool CanUseDynarec();

// Initialize dynarec system (call once at startup)
void InitSystem();

// Shutdown dynarec system
void ShutdownSystem();

} // namespace Dynarec
