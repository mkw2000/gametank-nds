
#include "mos6502.h"
#include "SDL_inc.h"
#if defined(NDS_BUILD) && defined(ARM9)
#include "system_state.h"
#endif

#ifndef ITCM_CODE
#define ITCM_CODE
#endif

#ifndef LIKELY
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#endif
#ifndef UNLIKELY
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

#define NDS_USE_THREADED_DISPATCH 0

#if defined(NDS_BUILD) && defined(ARM9)
extern SystemState system_state;
extern CartridgeState cartridge_state;
extern RomType loadedRomType;
extern uint16_t cached_ram_base;
extern uint8_t open_bus();
extern uint8_t VDMA_Read(uint16_t address);
extern void VDMA_Write(uint16_t address, uint8_t value);
extern void UpdateFlashShiftRegister(uint8_t nextVal);
extern "C" uint8_t GT_AudioRamRead(uint16_t address);
extern "C" void GT_AudioRamWrite(uint16_t address, uint8_t value);
extern "C" uint8_t GT_JoystickReadFast(uint8_t portNum);

#ifndef NDS_OPCODE_PROFILE_STRIDE
#define NDS_OPCODE_PROFILE_STRIDE 4u
#endif
#endif

mos6502::mos6502(BusRead r, BusWrite w, CPUEvent stp, BusRead sync)
{
	Write = (BusWrite)w;
	Read = (BusRead)r;
	Stopped = (CPUEvent)stp;
	Sync = (BusRead)sync;
	Instr instr;
	irq_timer = 0;
#if defined(NDS_BUILD) && defined(ARM9)
	for (int i = 0; i < 256; ++i) {
		opcode_exec_count[i] = 0;
		opcode_cycle_count[i] = 0;
	}
	opcode_profile_decim = 0;
#endif

	// fill jump table with ILLEGALs
	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_ILLEGAL;
	for(int i = 0; i < 256; i++)
	{
		InstrTable[i] = instr;
	}

	// insert opcodes

	instr.addr = &mos6502::Addr_IMM;
	instr.code = &mos6502::Op_ADC;
	instr.cycles = 2;
	InstrTable[0x69] = instr;
	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_ADC;
	instr.cycles = 4;
	InstrTable[0x6D] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_ADC;
	instr.cycles = 3;
	InstrTable[0x65] = instr;
	instr.addr = &mos6502::Addr_INX;
	instr.code = &mos6502::Op_ADC;
	instr.cycles = 6;
	InstrTable[0x61] = instr;
	instr.addr = &mos6502::Addr_INY;
	instr.code = &mos6502::Op_ADC;
	instr.cycles = 6;
	InstrTable[0x71] = instr;
	instr.addr = &mos6502::Addr_ZEX;
	instr.code = &mos6502::Op_ADC;
	instr.cycles = 4;
	InstrTable[0x75] = instr;
	instr.addr = &mos6502::Addr_ABX;
	instr.code = &mos6502::Op_ADC;
	instr.cycles = 4;
	InstrTable[0x7D] = instr;
	instr.addr = &mos6502::Addr_ABY;
	instr.code = &mos6502::Op_ADC;
	instr.cycles = 4;
	InstrTable[0x79] = instr;
	instr.addr = &mos6502::Addr_ZPI;
	instr.code = &mos6502::Op_ADC;
	instr.cycles = 6;
	InstrTable[0x72] = instr;

	instr.addr = &mos6502::Addr_IMM;
	instr.code = &mos6502::Op_AND;
	instr.cycles = 2;
	InstrTable[0x29] = instr;
	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_AND;
	instr.cycles = 4;
	InstrTable[0x2D] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_AND;
	instr.cycles = 3;
	InstrTable[0x25] = instr;
	instr.addr = &mos6502::Addr_INX;
	instr.code = &mos6502::Op_AND;
	instr.cycles = 6;
	InstrTable[0x21] = instr;
	instr.addr = &mos6502::Addr_INY;
	instr.code = &mos6502::Op_AND;
	instr.cycles = 5;
	InstrTable[0x31] = instr;
	instr.addr = &mos6502::Addr_ZEX;
	instr.code = &mos6502::Op_AND;
	instr.cycles = 4;
	InstrTable[0x35] = instr;
	instr.addr = &mos6502::Addr_ABX;
	instr.code = &mos6502::Op_AND;
	instr.cycles = 4;
	InstrTable[0x3D] = instr;
	instr.addr = &mos6502::Addr_ABY;
	instr.code = &mos6502::Op_AND;
	instr.cycles = 4;
	InstrTable[0x39] = instr;
	instr.addr = &mos6502::Addr_ZPI;
	instr.code = &mos6502::Op_AND;
	instr.cycles = 5;
	InstrTable[0x32] = instr;

	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_ASL;
	instr.cycles = 6;
	InstrTable[0x0E] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_ASL;
	instr.cycles = 5;
	InstrTable[0x06] = instr;
	instr.addr = &mos6502::Addr_ACC;
	instr.code = &mos6502::Op_ASL_ACC;
	instr.cycles = 2;
	InstrTable[0x0A] = instr;
	instr.addr = &mos6502::Addr_ZEX;
	instr.code = &mos6502::Op_ASL;
	instr.cycles = 6;
	InstrTable[0x16] = instr;
	instr.addr = &mos6502::Addr_ABX;
	instr.code = &mos6502::Op_ASL;
	// 65c02 note: this instruction now takes 6+ cycles instead of 7 on the 6502
	instr.cycles = 6;
	InstrTable[0x1E] = instr;

	instr.addr = &mos6502::Addr_REL;
	instr.code = &mos6502::Op_BCC;
	instr.cycles = 2;
	InstrTable[0x90] = instr;

	instr.addr = &mos6502::Addr_REL;
	instr.code = &mos6502::Op_BCS;
	instr.cycles = 2;
	InstrTable[0xB0] = instr;

	instr.addr = &mos6502::Addr_REL;
	instr.code = &mos6502::Op_BEQ;
	instr.cycles = 2;
	InstrTable[0xF0] = instr;

	instr.addr = &mos6502::Addr_REL;
	instr.code = &mos6502::Op_BRA;
	instr.cycles = 3;
	InstrTable[0x80] = instr;

	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_BIT;
	instr.cycles = 4;
	InstrTable[0x2C] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_BIT;
	instr.cycles = 3;
	InstrTable[0x24] = instr;

	instr.addr = &mos6502::Addr_REL;
	instr.code = &mos6502::Op_BMI;
	instr.cycles = 2;
	InstrTable[0x30] = instr;

	instr.addr = &mos6502::Addr_REL;
	instr.code = &mos6502::Op_BNE;
	instr.cycles = 2;
	InstrTable[0xD0] = instr;

	instr.addr = &mos6502::Addr_REL;
	instr.code = &mos6502::Op_BPL;
	instr.cycles = 2;
	InstrTable[0x10] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_BRK;
	instr.cycles = 7;
	InstrTable[0x00] = instr;

	instr.addr = &mos6502::Addr_REL;
	instr.code = &mos6502::Op_BVC;
	instr.cycles = 2;
	InstrTable[0x50] = instr;

	instr.addr = &mos6502::Addr_REL;
	instr.code = &mos6502::Op_BVS;
	instr.cycles = 2;
	InstrTable[0x70] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_CLC;
	instr.cycles = 2;
	InstrTable[0x18] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_CLD;
	instr.cycles = 2;
	InstrTable[0xD8] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_CLI;
	instr.cycles = 2;
	InstrTable[0x58] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_CLV;
	instr.cycles = 2;
	InstrTable[0xB8] = instr;

	instr.addr = &mos6502::Addr_IMM;
	instr.code = &mos6502::Op_CMP;
	instr.cycles = 2;
	InstrTable[0xC9] = instr;
	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_WAI;
	instr.cycles = 3;
	InstrTable[0xCB] = instr;
	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_STP;
	instr.cycles = 3;
	InstrTable[0xDB] = instr;
	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_CMP;
	instr.cycles = 4;
	InstrTable[0xCD] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_CMP;
	instr.cycles = 3;
	InstrTable[0xC5] = instr;
	instr.addr = &mos6502::Addr_INX;
	instr.code = &mos6502::Op_CMP;
	instr.cycles = 6;
	InstrTable[0xC1] = instr;
	instr.addr = &mos6502::Addr_INY;
	instr.code = &mos6502::Op_CMP;
	instr.cycles = 3;
	InstrTable[0xD1] = instr;
	instr.addr = &mos6502::Addr_ZEX;
	instr.code = &mos6502::Op_CMP;
	instr.cycles = 4;
	InstrTable[0xD5] = instr;
	instr.addr = &mos6502::Addr_ABX;
	instr.code = &mos6502::Op_CMP;
	instr.cycles = 4;
	InstrTable[0xDD] = instr;
	instr.addr = &mos6502::Addr_ABY;
	instr.code = &mos6502::Op_CMP;
	instr.cycles = 4;
	InstrTable[0xD9] = instr;
	instr.addr = &mos6502::Addr_ZPI;
	instr.code = &mos6502::Op_CMP;
	instr.cycles = 5;
	InstrTable[0xD2] = instr;

	instr.addr = &mos6502::Addr_IMM;
	instr.code = &mos6502::Op_CPX;
	instr.cycles = 2;
	InstrTable[0xE0] = instr;
	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_CPX;
	instr.cycles = 4;
	InstrTable[0xEC] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_CPX;
	instr.cycles = 3;
	InstrTable[0xE4] = instr;

	instr.addr = &mos6502::Addr_IMM;
	instr.code = &mos6502::Op_CPY;
	instr.cycles = 2;
	InstrTable[0xC0] = instr;
	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_CPY;
	instr.cycles = 4;
	InstrTable[0xCC] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_CPY;
	instr.cycles = 3;
	InstrTable[0xC4] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_DEC_ACC;
	instr.cycles = 2;
	InstrTable[0x3A] = instr;
	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_DEC;
	instr.cycles = 6;
	InstrTable[0xCE] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_DEC;
	instr.cycles = 5;
	InstrTable[0xC6] = instr;
	instr.addr = &mos6502::Addr_ZEX;
	instr.code = &mos6502::Op_DEC;
	instr.cycles = 6;
	InstrTable[0xD6] = instr;
	instr.addr = &mos6502::Addr_ABX;
	instr.code = &mos6502::Op_DEC;
	instr.cycles = 7;
	InstrTable[0xDE] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_DEX;
	instr.cycles = 2;
	InstrTable[0xCA] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_DEY;
	instr.cycles = 2;
	InstrTable[0x88] = instr;

	instr.addr = &mos6502::Addr_IMM;
	instr.code = &mos6502::Op_EOR;
	instr.cycles = 2;
	InstrTable[0x49] = instr;
	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_EOR;
	instr.cycles = 4;
	InstrTable[0x4D] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_EOR;
	instr.cycles = 3;
	InstrTable[0x45] = instr;
	instr.addr = &mos6502::Addr_INX;
	instr.code = &mos6502::Op_EOR;
	instr.cycles = 6;
	InstrTable[0x41] = instr;
	instr.addr = &mos6502::Addr_INY;
	instr.code = &mos6502::Op_EOR;
	instr.cycles = 5;
	InstrTable[0x51] = instr;
	instr.addr = &mos6502::Addr_ZEX;
	instr.code = &mos6502::Op_EOR;
	instr.cycles = 4;
	InstrTable[0x55] = instr;
	instr.addr = &mos6502::Addr_ABX;
	instr.code = &mos6502::Op_EOR;
	instr.cycles = 4;
	InstrTable[0x5D] = instr;
	instr.addr = &mos6502::Addr_ABY;
	instr.code = &mos6502::Op_EOR;
	instr.cycles = 4;
	InstrTable[0x59] = instr;
	instr.addr = &mos6502::Addr_ZPI;
	instr.code = &mos6502::Op_EOR;
	instr.cycles = 5;
	InstrTable[0x52] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_INC_ACC;
	instr.cycles = 2;
	InstrTable[0x1A] = instr;
	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_INC;
	instr.cycles = 6;
	InstrTable[0xEE] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_INC;
	instr.cycles = 5;
	InstrTable[0xE6] = instr;
	instr.addr = &mos6502::Addr_ZEX;
	instr.code = &mos6502::Op_INC;
	instr.cycles = 6;
	InstrTable[0xF6] = instr;
	instr.addr = &mos6502::Addr_ABX;
	instr.code = &mos6502::Op_INC;
	instr.cycles = 7;
	InstrTable[0xFE] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_INX;
	instr.cycles = 2;
	InstrTable[0xE8] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_INY;
	instr.cycles = 2;
	InstrTable[0xC8] = instr;

	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_JMP;
	instr.cycles = 3;
	InstrTable[0x4C] = instr;
	instr.addr = &mos6502::Addr_ABI;
	instr.code = &mos6502::Op_JMP;
	instr.cycles = 5;
	InstrTable[0x6C] = instr;

	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_JSR;
	instr.cycles = 6;
	InstrTable[0x20] = instr;

	instr.addr = &mos6502::Addr_IMM;
	instr.code = &mos6502::Op_LDA;
	instr.cycles = 2;
	InstrTable[0xA9] = instr;
	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_LDA;
	instr.cycles = 4;
	InstrTable[0xAD] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_LDA;
	instr.cycles = 3;
	InstrTable[0xA5] = instr;
	instr.addr = &mos6502::Addr_INX;
	instr.code = &mos6502::Op_LDA;
	instr.cycles = 6;
	InstrTable[0xA1] = instr;
	instr.addr = &mos6502::Addr_INY;
	instr.code = &mos6502::Op_LDA;
	instr.cycles = 5;
	InstrTable[0xB1] = instr;
	instr.addr = &mos6502::Addr_ZEX;
	instr.code = &mos6502::Op_LDA;
	instr.cycles = 4;
	InstrTable[0xB5] = instr;
	instr.addr = &mos6502::Addr_ABX;
	instr.code = &mos6502::Op_LDA;
	instr.cycles = 4;
	InstrTable[0xBD] = instr;
	instr.addr = &mos6502::Addr_ABY;
	instr.code = &mos6502::Op_LDA;
	instr.cycles = 4;
	InstrTable[0xB9] = instr;
	instr.addr = &mos6502::Addr_ZPI;
	instr.code = &mos6502::Op_LDA;
	instr.cycles = 5;
	InstrTable[0xB2] = instr;

	instr.addr = &mos6502::Addr_IMM;
	instr.code = &mos6502::Op_LDX;
	instr.cycles = 2;
	InstrTable[0xA2] = instr;
	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_LDX;
	instr.cycles = 4;
	InstrTable[0xAE] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_LDX;
	instr.cycles = 3;
	InstrTable[0xA6] = instr;
	instr.addr = &mos6502::Addr_ABY;
	instr.code = &mos6502::Op_LDX;
	instr.cycles = 4;
	InstrTable[0xBE] = instr;
	instr.addr = &mos6502::Addr_ZEY;
	instr.code = &mos6502::Op_LDX;
	instr.cycles = 4;
	InstrTable[0xB6] = instr;

	instr.addr = &mos6502::Addr_IMM;
	instr.code = &mos6502::Op_LDY;
	instr.cycles = 2;
	InstrTable[0xA0] = instr;
	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_LDY;
	instr.cycles = 4;
	InstrTable[0xAC] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_LDY;
	instr.cycles = 3;
	InstrTable[0xA4] = instr;
	instr.addr = &mos6502::Addr_ZEX;
	instr.code = &mos6502::Op_LDY;
	instr.cycles = 4;
	InstrTable[0xB4] = instr;
	instr.addr = &mos6502::Addr_ABX;
	instr.code = &mos6502::Op_LDY;
	instr.cycles = 4;
	InstrTable[0xBC] = instr;

	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_LSR;
	instr.cycles = 6;
	InstrTable[0x4E] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_LSR;
	instr.cycles = 5;
	InstrTable[0x46] = instr;
	instr.addr = &mos6502::Addr_ACC;
	instr.code = &mos6502::Op_LSR_ACC;
	instr.cycles = 2;
	InstrTable[0x4A] = instr;
	instr.addr = &mos6502::Addr_ZEX;
	instr.code = &mos6502::Op_LSR;
	instr.cycles = 6;
	InstrTable[0x56] = instr;
	instr.addr = &mos6502::Addr_ABX;
	instr.code = &mos6502::Op_LSR;
	// 65c02 note: this instruction now takes 6+ cycles instead of 7 on the 6502
	instr.cycles = 6;
	InstrTable[0x5E] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_NOP;
	instr.cycles = 2;
	InstrTable[0xEA] = instr;

	instr.addr = &mos6502::Addr_IMM;
	instr.code = &mos6502::Op_ORA;
	instr.cycles = 2;
	InstrTable[0x09] = instr;
	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_ORA;
	instr.cycles = 4;
	InstrTable[0x0D] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_ORA;
	instr.cycles = 3;
	InstrTable[0x05] = instr;
	instr.addr = &mos6502::Addr_INX;
	instr.code = &mos6502::Op_ORA;
	instr.cycles = 6;
	InstrTable[0x01] = instr;
	instr.addr = &mos6502::Addr_INY;
	instr.code = &mos6502::Op_ORA;
	instr.cycles = 5;
	InstrTable[0x11] = instr;
	instr.addr = &mos6502::Addr_ZEX;
	instr.code = &mos6502::Op_ORA;
	instr.cycles = 4;
	InstrTable[0x15] = instr;
	instr.addr = &mos6502::Addr_ABX;
	instr.code = &mos6502::Op_ORA;
	instr.cycles = 4;
	InstrTable[0x1D] = instr;
	instr.addr = &mos6502::Addr_ABY;
	instr.code = &mos6502::Op_ORA;
	instr.cycles = 4;
	InstrTable[0x19] = instr;
	instr.addr = &mos6502::Addr_ZPI;
	instr.code = &mos6502::Op_ORA;
	instr.cycles = 5;
	InstrTable[0x12] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_PHA;
	instr.cycles = 3;
	InstrTable[0x48] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_PHP;
	instr.cycles = 3;
	InstrTable[0x08] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_PHX;
	instr.cycles = 3;
	InstrTable[0xDA] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_PHY;
	instr.cycles = 3;
	InstrTable[0x5A] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_PLA;
	instr.cycles = 4;
	InstrTable[0x68] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_PLP;
	instr.cycles = 4;
	InstrTable[0x28] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_PLX;
	instr.cycles = 4;
	InstrTable[0xFA] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_PLY;
	instr.cycles = 4;
	InstrTable[0x7A] = instr;

	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_ROL;
	instr.cycles = 6;
	InstrTable[0x2E] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_ROL;
	instr.cycles = 5;
	InstrTable[0x26] = instr;
	instr.addr = &mos6502::Addr_ACC;
	instr.code = &mos6502::Op_ROL_ACC;
	instr.cycles = 2;
	InstrTable[0x2A] = instr;
	instr.addr = &mos6502::Addr_ZEX;
	instr.code = &mos6502::Op_ROL;
	instr.cycles = 6;
	InstrTable[0x36] = instr;
	instr.addr = &mos6502::Addr_ABX;
	instr.code = &mos6502::Op_ROL;
	// 65c02 note: this instruction now takes 6+ cycles instead of 7 on the 6502
	instr.cycles = 6;
	InstrTable[0x3E] = instr;

	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_ROR;
	instr.cycles = 6;
	InstrTable[0x6E] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_ROR;
	instr.cycles = 5;
	InstrTable[0x66] = instr;
	instr.addr = &mos6502::Addr_ACC;
	instr.code = &mos6502::Op_ROR_ACC;
	instr.cycles = 2;
	InstrTable[0x6A] = instr;
	instr.addr = &mos6502::Addr_ZEX;
	instr.code = &mos6502::Op_ROR;
	instr.cycles = 6;
	InstrTable[0x76] = instr;
	instr.addr = &mos6502::Addr_ABX;
	instr.code = &mos6502::Op_ROR;
	// 65c02 note: this instruction now takes 6+ cycles instead of 7 on the 6502
	instr.cycles = 6;
	InstrTable[0x7E] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_RTI;
	instr.cycles = 6;
	InstrTable[0x40] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_RTS;
	instr.cycles = 6;
	InstrTable[0x60] = instr;

	instr.addr = &mos6502::Addr_IMM;
	instr.code = &mos6502::Op_SBC;
	instr.cycles = 2;
	InstrTable[0xE9] = instr;
	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_SBC;
	instr.cycles = 4;
	InstrTable[0xED] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_SBC;
	instr.cycles = 3;
	InstrTable[0xE5] = instr;
	instr.addr = &mos6502::Addr_INX;
	instr.code = &mos6502::Op_SBC;
	instr.cycles = 6;
	InstrTable[0xE1] = instr;
	instr.addr = &mos6502::Addr_INY;
	instr.code = &mos6502::Op_SBC;
	instr.cycles = 5;
	InstrTable[0xF1] = instr;
	instr.addr = &mos6502::Addr_ZEX;
	instr.code = &mos6502::Op_SBC;
	instr.cycles = 4;
	InstrTable[0xF5] = instr;
	instr.addr = &mos6502::Addr_ABX;
	instr.code = &mos6502::Op_SBC;
	instr.cycles = 4;
	InstrTable[0xFD] = instr;
	instr.addr = &mos6502::Addr_ABY;
	instr.code = &mos6502::Op_SBC;
	instr.cycles = 4;
	InstrTable[0xF9] = instr;
	instr.addr = &mos6502::Addr_ZPI;
	instr.code = &mos6502::Op_SBC;
	instr.cycles = 5;
	InstrTable[0xF2] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_SEC;
	instr.cycles = 2;
	InstrTable[0x38] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_SED;
	instr.cycles = 2;
	InstrTable[0xF8] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_SEI;
	instr.cycles = 2;
	InstrTable[0x78] = instr;

	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_STA;
	instr.cycles = 4;
	InstrTable[0x8D] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_STA;
	instr.cycles = 3;
	InstrTable[0x85] = instr;
	instr.addr = &mos6502::Addr_INX;
	instr.code = &mos6502::Op_STA;
	instr.cycles = 6;
	InstrTable[0x81] = instr;
	instr.addr = &mos6502::Addr_INY;
	instr.code = &mos6502::Op_STA;
	instr.cycles = 6;
	InstrTable[0x91] = instr;
	instr.addr = &mos6502::Addr_ZEX;
	instr.code = &mos6502::Op_STA;
	instr.cycles = 4;
	InstrTable[0x95] = instr;
	instr.addr = &mos6502::Addr_ABX;
	instr.code = &mos6502::Op_STA;
	instr.cycles = 5;
	InstrTable[0x9D] = instr;
	instr.addr = &mos6502::Addr_ABY;
	instr.code = &mos6502::Op_STA;
	instr.cycles = 5;
	InstrTable[0x99] = instr;
	instr.addr = &mos6502::Addr_ZPI;
	instr.code = &mos6502::Op_STA;
	instr.cycles = 5;
	InstrTable[0x92] = instr;

	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_STZ;
	instr.cycles = 3;
	InstrTable[0x64] = instr;
	instr.addr = &mos6502::Addr_ZEX;
	instr.code = &mos6502::Op_STZ;
	instr.cycles = 4;
	InstrTable[0x74] = instr;
	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_STZ;
	instr.cycles = 4;
	InstrTable[0x9C] = instr;
	instr.addr = &mos6502::Addr_ABX;
	instr.code = &mos6502::Op_STZ;
	instr.cycles = 5;
	InstrTable[0x9E] = instr;

	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_STX;
	instr.cycles = 4;
	InstrTable[0x8E] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_STX;
	instr.cycles = 3;
	InstrTable[0x86] = instr;
	instr.addr = &mos6502::Addr_ZEY;
	instr.code = &mos6502::Op_STX;
	instr.cycles = 4;
	InstrTable[0x96] = instr;

	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_STY;
	instr.cycles = 4;
	InstrTable[0x8C] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_STY;
	instr.cycles = 3;
	InstrTable[0x84] = instr;
	instr.addr = &mos6502::Addr_ZEX;
	instr.code = &mos6502::Op_STY;
	instr.cycles = 4;
	InstrTable[0x94] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_TAX;
	instr.cycles = 2;
	InstrTable[0xAA] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_TAY;
	instr.cycles = 2;
	InstrTable[0xA8] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_TSX;
	instr.cycles = 2;
	InstrTable[0xBA] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_TXA;
	instr.cycles = 2;
	InstrTable[0x8A] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_TXS;
	instr.cycles = 2;
	InstrTable[0x9A] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_TYA;
	instr.cycles = 2;
	InstrTable[0x98] = instr;

	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_TRB;
	instr.cycles = 6;
	InstrTable[0x1C] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_TRB;
	instr.cycles = 5;
	InstrTable[0x14] = instr;

	instr.addr = &mos6502::Addr_ABS;
	instr.code = &mos6502::Op_TSB;
	instr.cycles = 6;
	InstrTable[0x0C] = instr;
	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_TSB;
	instr.cycles = 5;
	InstrTable[0x04] = instr;

	// New addressing modes for BIT
	instr.addr = &mos6502::Addr_IMM;
	instr.code = &mos6502::Op_BIT;
	instr.cycles = 2;
	InstrTable[0x89] = instr;

	instr.addr = &mos6502::Addr_ZEX;
	instr.code = &mos6502::Op_BIT;
	instr.cycles = 4;
	InstrTable[0x34] = instr;

	instr.addr = &mos6502::Addr_ABX;
	instr.code = &mos6502::Op_BIT;
	instr.cycles = 4;
	InstrTable[0x3C] = instr;

	// BBRx and BBSx
	// NOTE these instructions are weird and use their own addressing mode
	// Instead I'll opt for implied and handle within the codes
	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_BBR0;
	instr.cycles = 5;
	InstrTable[0x0F] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_BBR1;
	instr.cycles = 5;
	InstrTable[0x1F] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_BBR2;
	instr.cycles = 5;
	InstrTable[0x2F] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_BBR3;
	instr.cycles = 5;
	InstrTable[0x3F] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_BBR4;
	instr.cycles = 5;
	InstrTable[0x4F] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_BBR5;
	instr.cycles = 5;
	InstrTable[0x5F] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_BBR6;
	instr.cycles = 5;
	InstrTable[0x6F] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_BBR7;
	instr.cycles = 5;
	InstrTable[0x7F] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_BBS0;
	instr.cycles = 5;
	InstrTable[0x8F] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_BBS1;
	instr.cycles = 5;
	InstrTable[0x9F] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_BBS2;
	instr.cycles = 5;
	InstrTable[0xAF] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_BBS3;
	instr.cycles = 5;
	InstrTable[0xBF] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_BBS4;
	instr.cycles = 5;
	InstrTable[0xCF] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_BBS5;
	instr.cycles = 5;
	InstrTable[0xDF] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_BBS6;
	instr.cycles = 5;
	InstrTable[0xEF] = instr;

	instr.addr = &mos6502::Addr_IMP;
	instr.code = &mos6502::Op_BBS7;
	instr.cycles = 5;
	InstrTable[0xFF] = instr;

	// JMP (abs, x): new addressing mode for the 65c02
	instr.addr = &mos6502::Addr_AIX;
	instr.code = &mos6502::Op_JMP;
	instr.cycles = 6;
	InstrTable[0x7C] = instr;

	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_RMB0;
	instr.cycles = 5;
	InstrTable[0x07] = instr;

	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_RMB1;
	instr.cycles = 5;
	InstrTable[0x17] = instr;

	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_RMB2;
	instr.cycles = 5;
	InstrTable[0x27] = instr;

	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_RMB3;
	instr.cycles = 5;
	InstrTable[0x37] = instr;

	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_RMB4;
	instr.cycles = 5;
	InstrTable[0x47] = instr;

	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_RMB5;
	instr.cycles = 5;
	InstrTable[0x57] = instr;

	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_RMB6;
	instr.cycles = 5;
	InstrTable[0x67] = instr;

	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_RMB7;
	instr.cycles = 5;
	InstrTable[0x77] = instr;

	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_SMB0;
	instr.cycles = 5;
	InstrTable[0x87] = instr;

	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_SMB1;
	instr.cycles = 5;
	InstrTable[0x97] = instr;

	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_SMB2;
	instr.cycles = 5;
	InstrTable[0xA7] = instr;

	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_SMB3;
	instr.cycles = 5;
	InstrTable[0xB7] = instr;

	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_SMB4;
	instr.cycles = 5;
	InstrTable[0xC7] = instr;

	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_SMB5;
	instr.cycles = 5;
	InstrTable[0xD7] = instr;

	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_SMB6;
	instr.cycles = 5;
	InstrTable[0xE7] = instr;

	instr.addr = &mos6502::Addr_ZER;
	instr.code = &mos6502::Op_SMB7;
	instr.cycles = 5;
	InstrTable[0xF7] = instr;

	Reset();

	return;
}

inline uint8_t mos6502::ReadBus(uint16_t address)
{
#if defined(NDS_BUILD) && defined(ARM9)
	// GameTank map fast path:
	// 0000-1FFF: RAM, 2008/2009: controller, 2800-2FFF: VIA mirror,
	// 3000-3FFF: audio RAM, 4000-7FFF: VDMA, 8000-FFFF: cart ROM/flash.
	if(address & 0x8000) {
		switch(loadedRomType) {
			case RomType::FLASH2M:
				if(address & 0x4000) return cartridge_state.rom[0x1FC000 | (address & 0x3FFF)];
				return cartridge_state.rom[((cartridge_state.bank_mask & 0x7F) << 14) | (address & 0x3FFF)];
			case RomType::FLASH2M_RAM32K:
				if(address & 0x4000) return cartridge_state.rom[0x1FC000 | (address & 0x3FFF)];
				if(!(cartridge_state.bank_mask & 0x80)) {
					return cartridge_state.save_ram[(address & 0x3FFF) | ((cartridge_state.bank_mask & 0x40) << 8)];
				}
				return cartridge_state.rom[((cartridge_state.bank_mask & 0x7F) << 14) | (address & 0x3FFF)];
			case RomType::EEPROM32K:
				return cartridge_state.rom[address & 0x7FFF];
			case RomType::EEPROM8K:
				return cartridge_state.rom[address & 0x1FFF];
			default:
				break;
		}
	}
	if(address < 0x2000) {
		return system_state.ram[cached_ram_base | address];
	}
	if(address & 0x4000) {
		// VDMA reads are time-coupled with blitter state.
		FlushRunCycles();
		return VDMA_Read(address);
	}
	if((address >= 0x3000) && (address <= 0x3FFF)) {
		return GT_AudioRamRead(address);
	}
	if((address >= 0x2800) && (address <= 0x2FFF)) {
		return system_state.VIA_regs[address & 0xF];
	}
	if((address == 0x2008) || (address == 0x2009)) {
		return GT_JoystickReadFast((uint8_t)address);
	}
	return open_bus();
#endif
	FlushRunCycles();
	return (*Read)(address);
}

inline void mos6502::WriteBus(uint16_t address, uint8_t value)
{
#if defined(NDS_BUILD) && defined(ARM9)
	if(address < 0x2000) {
		const uint16_t idx = (uint16_t)(cached_ram_base | address);
		system_state.ram_initialized[idx] = true;
		system_state.ram[idx] = value;
		return;
	}
	if(address & 0x4000) {
		// VDMA writes are time-coupled with blitter state.
		FlushRunCycles();
		VDMA_Write(address, value);
		return;
	}
	if((address >= 0x3000) && (address <= 0x3FFF)) {
		GT_AudioRamWrite(address, value);
		return;
	}
	if((address >= 0x2800) && (address <= 0x2FFF)) {
		const uint8_t viaReg = (uint8_t)(address & 0xF);
		// VIA_ORA drives flash serial signals on FLASH2M hardware.
		if((loadedRomType == RomType::FLASH2M) && (viaReg == 0x1)) {
			UpdateFlashShiftRegister(value);
		}
		system_state.VIA_regs[viaReg] = value;
		return;
	}
#endif
	FlushRunCycles();
	(*Write)(address, value);
}

inline uint8_t mos6502::FetchByte()
{
	const uint16_t address = pc++;
#if defined(NDS_BUILD) && defined(ARM9)
	// Instruction stream is usually ROM; keep this path as lean as possible.
	if (address & 0x8000) {
		if (loadedRomType == RomType::FLASH2M) {
			if (address & 0x4000) return cartridge_state.rom[0x1FC000 | (address & 0x3FFF)];
			return cartridge_state.rom[((cartridge_state.bank_mask & 0x7F) << 14) | (address & 0x3FFF)];
		}
		if (loadedRomType == RomType::FLASH2M_RAM32K) {
			if (address & 0x4000) return cartridge_state.rom[0x1FC000 | (address & 0x3FFF)];
			if(!(cartridge_state.bank_mask & 0x80)) {
				return cartridge_state.save_ram[(address & 0x3FFF) | ((cartridge_state.bank_mask & 0x40) << 8)];
			}
			return cartridge_state.rom[((cartridge_state.bank_mask & 0x7F) << 14) | (address & 0x3FFF)];
		}
		if (loadedRomType == RomType::EEPROM32K) return cartridge_state.rom[address & 0x7FFF];
		if (loadedRomType == RomType::EEPROM8K) return cartridge_state.rom[address & 0x1FFF];
	}

	if (address < 0x2000) {
		return system_state.ram[cached_ram_base | address];
	}
#endif
	return ReadBus(address);
}

inline void mos6502::SetNZFast(uint8_t value)
{
	status = (status & (uint8_t)~(NEGATIVE | ZERO)) |
		(uint8_t)(value & NEGATIVE) |
		(uint8_t)((value == 0) ? ZERO : 0);
}

inline void mos6502::FlushRunCycles()
{
	if(run_cycle_target && run_pending_cycles) {
		*run_cycle_target += run_pending_cycles;
		run_pending_cycles = 0;
	}
}

// Small helper function to test if addresses belong to the same page
// This is useful for conditional timing as some addressing modes take additional cycles if calculated
// addresses cross page boundaries
inline bool mos6502::addressesSamePage(uint16_t a, uint16_t b)
{
	return ((a & 0xFF00) == (b & 0xFF00));
}

uint16_t ITCM_CODE mos6502::Addr_ACC()
{
	return 0; // not used
}

uint16_t ITCM_CODE mos6502::Addr_IMM()
{
	return pc++;
}

uint16_t ITCM_CODE mos6502::Addr_ABS()
{
	uint16_t addrL;
	uint16_t addrH;
	uint16_t addr;

	addrL = ReadBus(pc++);
	addrH = ReadBus(pc++);

	addr = addrL + (addrH << 8);

	return addr;
}

uint16_t ITCM_CODE mos6502::Addr_ZER()
{
	return ReadBus(pc++);
}

uint16_t ITCM_CODE mos6502::Addr_IMP()
{
	return 0; // not used
}

uint16_t ITCM_CODE mos6502::Addr_REL()
{
	uint16_t offset;
	uint16_t addr;

	offset = (uint16_t)ReadBus(pc++);
	if (offset & 0x80) offset |= 0xFF00;
	addr = pc + (int16_t)offset;

	return addr;
}

uint16_t ITCM_CODE mos6502::Addr_ABI()
{
	uint16_t addrL;
	uint16_t addrH;
	uint16_t effL;
	uint16_t effH;
	uint16_t abs;
	uint16_t addr;

	addrL = ReadBus(pc++);
	addrH = ReadBus(pc++);

	abs = (addrH << 8) | addrL;

	effL = ReadBus(abs);

#ifndef CMOS_INDIRECT_JMP_FIX
	effH = ReadBus((abs & 0xFF00) + ((abs + 1) & 0x00FF) );
#else
	effH = ReadBus(abs + 1);
#endif

	addr = effL + 0x100 * effH;

	return addr;
}

uint16_t ITCM_CODE mos6502::Addr_AIX()
{
	uint16_t addrL;
	uint16_t addrH;
	uint16_t effL;
	uint16_t effH;
	uint16_t abs;
	uint16_t addr;

	addrL = ReadBus(pc++);
	addrH = ReadBus(pc++);

	// Offset the calculated absolute address by X
	abs = ((addrH << 8) | addrL) + X;

	effL = ReadBus(abs);

#ifndef CMOS_INDIRECT_JMP_FIX
	effH = ReadBus((abs & 0xFF00) + ((abs + 1) & 0x00FF) );
#else
	effH = ReadBus(abs + 1);
#endif

	addr = effL + 0x100 * effH;

	return addr;
}


uint16_t ITCM_CODE mos6502::Addr_ZEX()
{
	uint16_t addr = (ReadBus(pc++) + X) % 256;
	return addr;
}

uint16_t ITCM_CODE mos6502::Addr_ZEY()
{
	uint16_t addr = (ReadBus(pc++) + Y) % 256;
	return addr;
}

uint16_t ITCM_CODE mos6502::Addr_ABX()
{
	uint16_t addr;
	uint16_t addrBase;
	uint16_t addrL;
	uint16_t addrH;

	addrL = ReadBus(pc++);
	addrH = ReadBus(pc++);

	addrBase = addrL + (addrH << 8);
	addr = addrBase + X;

	// An extra cycle is required if a page boundary is crossed
	if (!addressesSamePage(addr, addrBase)) opExtraCycles++;

	return addr;
}

uint16_t ITCM_CODE mos6502::Addr_ABY()
{
	uint16_t addr;
	uint16_t addrBase;
	uint16_t addrL;
	uint16_t addrH;

	addrL = ReadBus(pc++);
	addrH = ReadBus(pc++);

	addrBase = addrL + (addrH << 8);
	addr = addrBase + Y;

	// An extra cycle is required if a page boundary is crossed
	if (!addressesSamePage(addr, addrBase)) opExtraCycles++;

	return addr;
}


uint16_t ITCM_CODE mos6502::Addr_INX()
{
	uint16_t zeroL;
	uint16_t zeroH;
	uint16_t addr;

	zeroL = (ReadBus(pc++) + X) % 256;
	zeroH = (zeroL + 1) % 256;
	addr = ReadBus(zeroL) + (ReadBus(zeroH) << 8);

	return addr;
}

uint16_t ITCM_CODE mos6502::Addr_INY()
{
	uint16_t zeroL;
	uint16_t zeroH;
	uint16_t addr;
	uint16_t addrBase;

	zeroL = ReadBus(pc++);
	zeroH = (zeroL + 1) % 256;
	addrBase = ReadBus(zeroL) + (ReadBus(zeroH) << 8);
	addr = addrBase + Y;

	// An extra cycle is required if a page boundary is crossed
	if (!addressesSamePage(addr, addrBase)) opExtraCycles++;

	return addr;
}

uint16_t ITCM_CODE mos6502::Addr_ZPI()
{
	uint16_t zeroL;
	uint16_t zeroH;
	uint16_t addr;

	zeroL = ReadBus(pc++);
	zeroH = (zeroL + 1) % 256;
	addr = ReadBus(zeroL) + (ReadBus(zeroH) << 8);

	return addr;
}

void mos6502::Reset()
{
	A = 0x00;
	Y = 0x00;
	X = 0x00;

	pc = (ReadBus(rstVectorH) << 8) + ReadBus(rstVectorL); // load PC from reset vector

	sp = 0xFD;

	status |= CONSTANT;

	illegalOpcode = false;
	waiting = false;

	return;
}

void ITCM_CODE mos6502::StackPush(uint8_t byte)
{
	WriteBus(0x0100 + sp, byte);
	if(sp == 0x00) sp = 0xFF;
	else sp--;
}

uint8_t ITCM_CODE mos6502::StackPop()
{
	if(sp == 0xFF) sp = 0x00;
	else sp++;
	return ReadBus(0x0100 + sp);
}

void mos6502::IRQ()
{
	irq_line = true;
	waiting = false;
	if(!IF_INTERRUPT())
	{
		SET_BREAK(0);
		StackPush((pc >> 8) & 0xFF);
		StackPush(pc & 0xFF);
		StackPush(status);
		SET_INTERRUPT(1);
		pc = (ReadBus(irqVectorH) << 8) + ReadBus(irqVectorL);
	}
	return;
}

void mos6502::ScheduleIRQ(uint32_t cycles, bool *gate) {
	irq_timer = cycles;
	irq_gate = gate;
	if(cycles == 0) {
		if((irq_gate == NULL) || (*irq_gate)) 
			IRQ();
	}
}

void mos6502::ClearIRQ() {
	irq_line = false;
	irq_timer = 0;
}

void mos6502::NMI()
{
	waiting = false;
	SET_BREAK(0);
	StackPush((pc >> 8) & 0xFF);
	StackPush(pc & 0xFF);
	StackPush(status);
	SET_INTERRUPT(1);
	pc = (ReadBus(nmiVectorH) << 8) + ReadBus(nmiVectorL);
	return;
}

void mos6502::Freeze()
{
	freeze = true;
}

#if defined(NDS_BUILD) && defined(ARM9)
void mos6502::GetOpcodeProfileSnapshot(uint32_t outExec[256], uint64_t outCycles[256]) const
{
	for (int i = 0; i < 256; ++i) {
		outExec[i] = opcode_exec_count[i];
		outCycles[i] = opcode_cycle_count[i];
	}
}

void mos6502::ResetOpcodeProfile()
{
	for (int i = 0; i < 256; ++i) {
		opcode_exec_count[i] = 0;
		opcode_cycle_count[i] = 0;
	}
	opcode_profile_decim = 0;
}
#endif

void mos6502::Run(
	int32_t cyclesRemaining,
	uint64_t& cycleCount,
	CycleMethod cycleMethod
) {
	run_cycle_target = &cycleCount;
	run_pending_cycles = 0;

	uint8_t opcode;
	uint8_t elapsedCycles;
#if defined(NDS_BUILD) && defined(ARM9)
	uint16_t src = 0;
#else
	Instr instr;
#endif

	if (UNLIKELY(freeze)) return;

	while((cyclesRemaining > 0) && !illegalOpcode)
	{
		if (UNLIKELY(waiting)) {
			if (UNLIKELY(irq_line)) {
				waiting = false;
				IRQ();
			} else if(irq_timer > 0) {
				if(cyclesRemaining >= (int32_t)irq_timer) {
					run_pending_cycles += irq_timer;
					cyclesRemaining -= irq_timer;
					irq_timer = 0;
					if((irq_gate == NULL) || (*irq_gate)) {
						irq_line = true;
						IRQ();
					}
				} else {
					irq_timer -= cyclesRemaining;
					run_pending_cycles += (uint32_t)cyclesRemaining;
					cyclesRemaining = 0;
					break;

				}
			} else {
				break;
			}
		} else if (UNLIKELY(irq_line)) {
			IRQ();
		}
		// fetch
		if(Sync == NULL) {
			opcode = FetchByte();
		} else {
			opcode = Sync(pc++);
		}
		if (UNLIKELY(freeze)) {
			--pc;
			cyclesRemaining = 0;
			break;
		}

		// Direct opcode dispatch on ARM9 avoids per-instruction member function pointer indirection.
#if defined(NDS_BUILD) && defined(ARM9)
#if NDS_USE_THREADED_DISPATCH
		static void* threadedDispatch[256] = {};
		static uint8_t threadedDispatchInit = 0;
		if (UNLIKELY(threadedDispatchInit == 0)) {
			for (int i = 0; i < 256; ++i) threadedDispatch[i] = &&td_op_slow;
			threadedDispatch[0xAD] = &&td_op_AD; // LDA ABS
			threadedDispatch[0xD0] = &&td_op_D0; // BNE REL
			threadedDispatch[0x20] = &&td_op_20; // JSR ABS
			threadedDispatchInit = 1;
		}
		goto *threadedDispatch[opcode];

td_op_AD: {
			const uint16_t lo = FetchByte();
			const uint16_t hi = FetchByte();
			A = ReadBus((uint16_t)(lo | (hi << 8)));
			SetNZFast(A);
			elapsedCycles = 4;
			goto td_op_done;
		}
td_op_D0: {
			uint16_t off = (uint16_t)FetchByte();
			if (off & 0x80) off |= 0xFF00;
			const uint16_t target = pc + (int16_t)off;
			if (!IF_ZERO()) {
				if (!addressesSamePage(pc, target)) opExtraCycles++;
				pc = target;
				opExtraCycles++;
			}
			elapsedCycles = 2;
			goto td_op_done;
		}
td_op_20: {
			const uint16_t lo = FetchByte();
			const uint16_t hi = FetchByte();
			const uint16_t target = (uint16_t)(lo | (hi << 8));
			pc--;
			StackPush((pc >> 8) & 0xFF);
			StackPush(pc & 0xFF);
			pc = target;
			elapsedCycles = 6;
			goto td_op_done;
		}
td_op_slow:
#endif
		switch(opcode) {
			case 0xAD: { // LDA ABS
				const uint16_t lo = FetchByte();
				const uint16_t hi = FetchByte();
				A = ReadBus((uint16_t)(lo | (hi << 8)));
				SetNZFast(A);
				elapsedCycles = 4;
				break;
			}
			case 0xA5: { // LDA ZER
				const uint16_t addr = FetchByte();
				A = ReadBus(addr);
				SetNZFast(A);
				elapsedCycles = 3;
				break;
			}
			case 0xA9: { // LDA IMM
				A = FetchByte();
				SetNZFast(A);
				elapsedCycles = 2;
				break;
			}
			case 0x8D: { // STA ABS
				const uint16_t lo = FetchByte();
				const uint16_t hi = FetchByte();
				WriteBus((uint16_t)(lo | (hi << 8)), A);
				elapsedCycles = 4;
				break;
			}
			case 0x85: { // STA ZER
				const uint16_t addr = FetchByte();
				WriteBus(addr, A);
				elapsedCycles = 3;
				break;
			}
			case 0xB2: { // LDA ZPI
				const uint8_t zp = FetchByte();
				const uint16_t addr = (uint16_t)(ReadBus(zp) | (ReadBus((uint8_t)(zp + 1)) << 8));
				A = ReadBus(addr);
				SetNZFast(A);
				elapsedCycles = 5;
				break;
			}
			case 0x49: { // EOR IMM
				A ^= FetchByte();
				SetNZFast(A);
				elapsedCycles = 2;
				break;
			}
			case 0xB0: { // BCS REL
				uint16_t off = (uint16_t)FetchByte();
				if (off & 0x80) off |= 0xFF00;
				const uint16_t target = pc + (int16_t)off;
				if (IF_CARRY()) {
					if (!addressesSamePage(pc, target)) opExtraCycles++;
					pc = target;
					opExtraCycles++;
				}
				elapsedCycles = 2;
				break;
			}
			case 0x90: { // BCC REL
				uint16_t off = (uint16_t)FetchByte();
				if (off & 0x80) off |= 0xFF00;
				const uint16_t target = pc + (int16_t)off;
				if (!IF_CARRY()) {
					if (!addressesSamePage(pc, target)) opExtraCycles++;
					pc = target;
					opExtraCycles++;
				}
				elapsedCycles = 2;
				break;
			}
			case 0xF0: { // BEQ REL
				uint16_t off = (uint16_t)FetchByte();
				if (off & 0x80) off |= 0xFF00;
				const uint16_t target = pc + (int16_t)off;
				if (IF_ZERO()) {
					if (!addressesSamePage(pc, target)) opExtraCycles++;
					pc = target;
					opExtraCycles++;
				}
				elapsedCycles = 2;
				break;
			}
			case 0x10: { // BPL REL
				uint16_t off = (uint16_t)FetchByte();
				if (off & 0x80) off |= 0xFF00;
				const uint16_t target = pc + (int16_t)off;
				if (!IF_NEGATIVE()) {
					if (!addressesSamePage(pc, target)) opExtraCycles++;
					pc = target;
					opExtraCycles++;
				}
				elapsedCycles = 2;
				break;
			}
			case 0x30: { // BMI REL
				uint16_t off = (uint16_t)FetchByte();
				if (off & 0x80) off |= 0xFF00;
				const uint16_t target = pc + (int16_t)off;
				if (IF_NEGATIVE()) {
					if (!addressesSamePage(pc, target)) opExtraCycles++;
					pc = target;
					opExtraCycles++;
				}
				elapsedCycles = 2;
				break;
			}
			case 0x50: { // BVC REL
				uint16_t off = (uint16_t)FetchByte();
				if (off & 0x80) off |= 0xFF00;
				const uint16_t target = pc + (int16_t)off;
				if (!IF_OVERFLOW()) {
					if (!addressesSamePage(pc, target)) opExtraCycles++;
					pc = target;
					opExtraCycles++;
				}
				elapsedCycles = 2;
				break;
			}
			case 0x70: { // BVS REL
				uint16_t off = (uint16_t)FetchByte();
				if (off & 0x80) off |= 0xFF00;
				const uint16_t target = pc + (int16_t)off;
				if (IF_OVERFLOW()) {
					if (!addressesSamePage(pc, target)) opExtraCycles++;
					pc = target;
					opExtraCycles++;
				}
				elapsedCycles = 2;
				break;
			}
			case 0x80: { // BRA REL
				uint16_t off = (uint16_t)FetchByte();
				if (off & 0x80) off |= 0xFF00;
				const uint16_t target = pc + (int16_t)off;
				if (!addressesSamePage(pc, target)) opExtraCycles++;
				pc = target;
				opExtraCycles++;
				elapsedCycles = 3;
				break;
			}
			case 0xD0: { // BNE REL
				uint16_t off = (uint16_t)FetchByte();
				if (off & 0x80) off |= 0xFF00;
				const uint16_t target = pc + (int16_t)off;
				if (!IF_ZERO()) {
					if (!addressesSamePage(pc, target)) opExtraCycles++;
					pc = target;
					opExtraCycles++;
				}
				elapsedCycles = 2;
				break;
			}
			case 0x20: { // JSR ABS
				const uint16_t lo = FetchByte();
				const uint16_t hi = FetchByte();
				const uint16_t target = (uint16_t)(lo | (hi << 8));
				pc--;
				StackPush((pc >> 8) & 0xFF);
				StackPush(pc & 0xFF);
				pc = target;
				elapsedCycles = 6;
				break;
			}
			case 0x4C: { // JMP ABS
				const uint16_t lo = FetchByte();
				const uint16_t hi = FetchByte();
				pc = (uint16_t)(lo | (hi << 8));
				elapsedCycles = 3;
				break;
			}
			case 0x60: { // RTS
				const uint16_t lo = StackPop();
				const uint16_t hi = StackPop();
				pc = (uint16_t)((hi << 8) | lo);
				pc++;
				elapsedCycles = 6;
				break;
			}
			case 0xEE: { // INC ABS
				const uint16_t lo = FetchByte();
				const uint16_t hi = FetchByte();
				const uint16_t addr = (uint16_t)(lo | (hi << 8));
				uint8_t m = ReadBus(addr);
				m = (uint8_t)(m + 1);
				SetNZFast(m);
				WriteBus(addr, m);
				elapsedCycles = 6;
				break;
			}
			case 0x28: { // PLP
				status = StackPop();
				status |= CONSTANT;
				status &= ~BREAK;
				elapsedCycles = 4;
				break;
			}
#include "mos6502_dispatch_cases.inc"
			default: {
				src = Addr_IMP();
				Op_ILLEGAL(src);
				elapsedCycles = 0;
				break;
			}
		}
#if NDS_USE_THREADED_DISPATCH
td_op_done:
#endif
#else
		instr = InstrTable[opcode];
		Exec(instr);
		elapsedCycles = instr.cycles;
#endif
		if(illegalOpcode) {
			illegalOpcodeSrc = opcode;
		}

		elapsedCycles += opExtraCycles;
		// The ops extra cycles have been accounted for, it must now be reset
		opExtraCycles = 0;
#if defined(NDS_BUILD) && defined(ARM9)
		opcode_profile_decim++;
		if ((opcode_profile_decim & (NDS_OPCODE_PROFILE_STRIDE - 1u)) == 0u) {
			opcode_exec_count[opcode] += NDS_OPCODE_PROFILE_STRIDE;
			opcode_cycle_count[opcode] += (uint32_t)elapsedCycles * NDS_OPCODE_PROFILE_STRIDE;
		}
#endif

		run_pending_cycles += elapsedCycles;
		cyclesRemaining -=
			(cycleMethod == CYCLE_COUNT )       ? elapsedCycles
			/* cycleMethod == INST_COUNT */   : 1;
		if(irq_timer > 0) {
			if(irq_timer < elapsedCycles) {
				irq_timer = 0;
			} else {
				irq_timer -= elapsedCycles;
			}
			if(irq_timer == 0) {
				if((irq_gate == NULL) || (*irq_gate)) {
					IRQ();
					irq_line = true;
				}
			}
		}
	}
	FlushRunCycles();
	run_cycle_target = nullptr;
}

void mos6502::RunOptimized(
	int32_t cyclesRemaining,
	uint64_t& cycleCount
) {
	Run(cyclesRemaining, cycleCount, CYCLE_COUNT);
}

void mos6502::Exec(Instr i)
{
	uint16_t src = (this->*i.addr)();
	(this->*i.code)(src);
}

void mos6502::Op_ILLEGAL(uint16_t src)
{
	illegalOpcode = true;
}


void mos6502::Op_ADC(uint16_t src)
{
	uint8_t m = ReadBus(src);
	unsigned int tmp = m + A + (IF_CARRY() ? 1 : 0);

	SET_ZERO(!(tmp & 0xFF));
	if (IF_DECIMAL())
	{
		// An extra cycle is required if in decimal mode
		opExtraCycles += 1;

		if (((A & 0xF) + (m & 0xF) + (IF_CARRY() ? 1 : 0)) > 9) tmp += 6;
		SET_NEGATIVE(tmp & 0x80);
		SET_OVERFLOW(!((A ^ m) & 0x80) && ((A ^ tmp) & 0x80));
		if (tmp > 0x99)
		{
			tmp += 96;
		}
		SET_CARRY(tmp > 0x99);
	}
	else
	{
		SET_NEGATIVE(tmp & 0x80);
		SET_OVERFLOW(!((A ^ m) & 0x80) && ((A ^ tmp) & 0x80));
		SET_CARRY(tmp > 0xFF);
	}

	A = tmp & 0xFF;
	return;
}



void mos6502::Op_AND(uint16_t src)
{
	uint8_t m = ReadBus(src);
	uint8_t res = m & A;
	SET_NEGATIVE(res & 0x80);
	SET_ZERO(!res);
	A = res;
	return;
}


void mos6502::Op_ASL(uint16_t src)
{
	uint8_t m = ReadBus(src);
	SET_CARRY(m & 0x80);
	m <<= 1;
	m &= 0xFF;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	WriteBus(src, m);
	return;
}

void mos6502::Op_ASL_ACC(uint16_t src)
{
	uint8_t m = A;
	SET_CARRY(m & 0x80);
	m <<= 1;
	m &= 0xFF;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	A = m;
	return;
}

void mos6502::Op_BCC(uint16_t src)
{
	if (!IF_CARRY())
	{
		// An extra cycle is required if a page boundary is crossed
		if (!addressesSamePage(pc, src)) opExtraCycles++;

		pc = src;
		opExtraCycles++;
	}
	return;
}


void mos6502::Op_BCS(uint16_t src)
{
	if (IF_CARRY())
	{
		// An extra cycle is required if a page boundary is crossed
		if (!addressesSamePage(pc, src)) opExtraCycles++;

		pc = src;
		opExtraCycles++;
	}
	return;
}

void mos6502::Op_BEQ(uint16_t src)
{
	if (IF_ZERO())
	{
		// An extra cycle is required if a page boundary is crossed
		if (!addressesSamePage(pc, src)) opExtraCycles++;

		pc = src;
		opExtraCycles++;
	}
	return;
}

void mos6502::Op_BIT(uint16_t src)
{
	uint8_t m = ReadBus(src);
	uint8_t res = m & A;
	SET_NEGATIVE(res & 0x80);
	status = (status & 0x3F) | (uint8_t)(m & 0xC0);
	SET_ZERO(!res);
	return;
}

void mos6502::Op_BMI(uint16_t src)
{
	if (IF_NEGATIVE())
	{
		// An extra cycle is required if a page boundary is crossed
		if (!addressesSamePage(pc, src)) opExtraCycles++;

		pc = src;
		opExtraCycles++;
	}
	return;
}

void mos6502::Op_BNE(uint16_t src)
{
	if (!IF_ZERO())
	{
		// An extra cycle is required if a page boundary is crossed
		if (!addressesSamePage(pc, src)) opExtraCycles++;

		pc = src;
		opExtraCycles++;
	}
	return;
}

void mos6502::Op_BPL(uint16_t src)
{
	if (!IF_NEGATIVE())
	{
		// An extra cycle is required if a page boundary is crossed
		if (!addressesSamePage(pc, src)) opExtraCycles++;

		pc = src;
		opExtraCycles++;
	}
	return;
}

void mos6502::Op_BRK(uint16_t src)
{
	pc++;
	StackPush((pc >> 8) & 0xFF);
	StackPush(pc & 0xFF);
	StackPush(status | BREAK);
	SET_INTERRUPT(1);
	pc = (ReadBus(irqVectorH) << 8) + ReadBus(irqVectorL);
	return;
}

void mos6502::Op_WAI(uint16_t src)
{
	waiting = true;
}

void mos6502::Op_STP(uint16_t src)
{
	illegalOpcode = true;
	Stopped();
}

void mos6502::Op_BVC(uint16_t src)
{
	if (!IF_OVERFLOW())
	{
		// An extra cycle is required if a page boundary is crossed
		if (!addressesSamePage(pc, src)) opExtraCycles++;

		pc = src;
		opExtraCycles++;
	}
	return;
}

void mos6502::Op_BVS(uint16_t src)
{
	if (IF_OVERFLOW())
	{
		// An extra cycle is required if a page boundary is crossed
		if (!addressesSamePage(pc, src)) opExtraCycles++;

		pc = src;
		opExtraCycles++;
	}
	return;
}

void mos6502::Op_CLC(uint16_t src)
{
	SET_CARRY(0);
	return;
}

void mos6502::Op_CLD(uint16_t src)
{
	SET_DECIMAL(0);
	return;
}

void mos6502::Op_CLI(uint16_t src)
{
	SET_INTERRUPT(0);
	return;
}

void mos6502::Op_CLV(uint16_t src)
{
	SET_OVERFLOW(0);
	return;
}

void mos6502::Op_CMP(uint16_t src)
{
	unsigned int tmp = A - ReadBus(src);
	SET_CARRY(tmp < 0x100);
	SET_NEGATIVE(tmp & 0x80);
	SET_ZERO(!(tmp & 0xFF));
	return;
}

void mos6502::Op_CPX(uint16_t src)
{
	unsigned int tmp = X - ReadBus(src);
	SET_CARRY(tmp < 0x100);
	SET_NEGATIVE(tmp & 0x80);
	SET_ZERO(!(tmp & 0xFF));
	return;
}

void mos6502::Op_CPY(uint16_t src)
{
	unsigned int tmp = Y - ReadBus(src);
	SET_CARRY(tmp < 0x100);
	SET_NEGATIVE(tmp & 0x80);
	SET_ZERO(!(tmp & 0xFF));
	return;
}

void mos6502::Op_DEC(uint16_t src)
{
	uint8_t m = ReadBus(src);
	m = (m - 1) % 256;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	WriteBus(src, m);
	return;
}

void mos6502::Op_DEC_ACC(uint16_t src)
{
	uint8_t m = A;
	m = (m - 1) % 256;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	A = m;
	return;
}

void mos6502::Op_DEX(uint16_t src)
{
	uint8_t m = X;
	m = (m - 1) % 256;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	X = m;
	return;
}

void mos6502::Op_DEY(uint16_t src)
{
	uint8_t m = Y;
	m = (m - 1) % 256;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	Y = m;
	return;
}

void mos6502::Op_EOR(uint16_t src)
{
	uint8_t m = ReadBus(src);
	m = A ^ m;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	A = m;
}

void mos6502::Op_INC(uint16_t src)
{
	uint8_t m = ReadBus(src);
	m = (m + 1) % 256;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	WriteBus(src, m);
}

void mos6502::Op_INC_ACC(uint16_t src)
{
	uint8_t m = A;
	m = (m + 1) % 256;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	A = m;
}

void mos6502::Op_INX(uint16_t src)
{
	uint8_t m = X;
	m = (m + 1) % 256;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	X = m;
}

void mos6502::Op_INY(uint16_t src)
{
	uint8_t m = Y;
	m = (m + 1) % 256;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	Y = m;
}

void mos6502::Op_JMP(uint16_t src)
{
	pc = src;
}

void mos6502::Op_JSR(uint16_t src)
{
	pc--;
	StackPush((pc >> 8) & 0xFF);
	StackPush(pc & 0xFF);
	pc = src;
}

void mos6502::Op_LDA(uint16_t src)
{
	uint8_t m = ReadBus(src);
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	A = m;
}

void mos6502::Op_LDX(uint16_t src)
{
	uint8_t m = ReadBus(src);
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	X = m;
}

void mos6502::Op_LDY(uint16_t src)
{
	uint8_t m = ReadBus(src);
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	Y = m;
}

void mos6502::Op_LSR(uint16_t src)
{
	uint8_t m = ReadBus(src);
	SET_CARRY(m & 0x01);
	m >>= 1;
	SET_NEGATIVE(0);
	SET_ZERO(!m);
	WriteBus(src, m);
}

void mos6502::Op_LSR_ACC(uint16_t src)
{
	uint8_t m = A;
	SET_CARRY(m & 0x01);
	m >>= 1;
	SET_NEGATIVE(0);
	SET_ZERO(!m);
	A = m;
}

void mos6502::Op_NOP(uint16_t src)
{
	return;
}

void mos6502::Op_ORA(uint16_t src)
{
	uint8_t m = ReadBus(src);
	m = A | m;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	A = m;
}

void mos6502::Op_PHA(uint16_t src)
{
	StackPush(A);
	return;
}

void mos6502::Op_PHP(uint16_t src)
{
	StackPush(status | BREAK);
	return;
}

void mos6502::Op_PHX(uint16_t src)
{
	StackPush(X);
	return;
}

void mos6502::Op_PHY(uint16_t src)
{
	StackPush(Y);
	return;
}

void mos6502::Op_PLA(uint16_t src)
{
	A = StackPop();
	SET_NEGATIVE(A & 0x80);
	SET_ZERO(!A);
	return;
}

void mos6502::Op_PLP(uint16_t src)
{
	status = StackPop();
	SET_CONSTANT(1);
	return;
}

void mos6502::Op_PLX(uint16_t src)
{
	X = StackPop();
	SET_NEGATIVE(X & 0x80);
	SET_ZERO(!X);
	return;
}

void mos6502::Op_PLY(uint16_t src)
{
	Y = StackPop();
	SET_NEGATIVE(Y & 0x80);
	SET_ZERO(!Y);
	return;
}

void mos6502::Op_ROL(uint16_t src)
{
	uint16_t m = ReadBus(src);
	m <<= 1;
	if (IF_CARRY()) m |= 0x01;
	SET_CARRY(m > 0xFF);
	m &= 0xFF;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	WriteBus(src, m);
	return;
}

void mos6502::Op_ROL_ACC(uint16_t src)
{
	uint16_t m = A;
	m <<= 1;
	if (IF_CARRY()) m |= 0x01;
	SET_CARRY(m > 0xFF);
	m &= 0xFF;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	A = m;
	return;
}

void mos6502::Op_ROR(uint16_t src)
{
	uint16_t m = ReadBus(src);
	if (IF_CARRY()) m |= 0x100;
	SET_CARRY(m & 0x01);
	m >>= 1;
	m &= 0xFF;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	WriteBus(src, m);
	return;
}

void mos6502::Op_ROR_ACC(uint16_t src)
{
	uint16_t m = A;
	if (IF_CARRY()) m |= 0x100;
	SET_CARRY(m & 0x01);
	m >>= 1;
	m &= 0xFF;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	A = m;
	return;
}

void mos6502::Op_RTI(uint16_t src)
{
	uint8_t lo, hi;

	status = StackPop();

	lo = StackPop();
	hi = StackPop();

	pc = (hi << 8) | lo;
	return;
}

void mos6502::Op_RTS(uint16_t src)
{
	uint8_t lo, hi;

	lo = StackPop();
	hi = StackPop();

	pc = ((hi << 8) | lo) + 1;
	return;
}

void mos6502::Op_SBC(uint16_t src)
{
	uint8_t m = ReadBus(src);
	unsigned int tmp = A - m - (IF_CARRY() ? 0 : 1);
	SET_NEGATIVE(tmp & 0x80);
	SET_ZERO(!(tmp & 0xFF));
	SET_OVERFLOW(((A ^ tmp) & 0x80) && ((A ^ m) & 0x80));

	if (IF_DECIMAL())
	{
		// An extra cycle is required if in decimal mode
		opExtraCycles += 1;

		if ( ((A & 0x0F) - (IF_CARRY() ? 0 : 1)) < (m & 0x0F)) tmp -= 6;
		if (tmp > 0x99)
		{
			tmp -= 0x60;
		}
	}
	SET_CARRY(tmp < 0x100);
	A = (tmp & 0xFF);
	return;
}

void mos6502::Op_SEC(uint16_t src)
{
	SET_CARRY(1);
	return;
}

void mos6502::Op_SED(uint16_t src)
{
	SET_DECIMAL(1);
	return;
}

void mos6502::Op_SEI(uint16_t src)
{
	SET_INTERRUPT(1);
	return;
}

void mos6502::Op_STA(uint16_t src)
{
	WriteBus(src, A);
	return;
}

void mos6502::Op_STZ(uint16_t src)
{
	WriteBus(src, 0);
	return;
}

void mos6502::Op_STX(uint16_t src)
{
	WriteBus(src, X);
	return;
}

void mos6502::Op_STY(uint16_t src)
{
	WriteBus(src, Y);
	return;
}

void mos6502::Op_TAX(uint16_t src)
{
	uint8_t m = A;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	X = m;
	return;
}

void mos6502::Op_TAY(uint16_t src)
{
	uint8_t m = A;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	Y = m;
	return;
}

void mos6502::Op_TSX(uint16_t src)
{
	uint8_t m = sp;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	X = m;
	return;
}

void mos6502::Op_TXA(uint16_t src)
{
	uint8_t m = X;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	A = m;
	return;
}

void mos6502::Op_TXS(uint16_t src)
{
	sp = X;
	return;
}

void mos6502::Op_TYA(uint16_t src)
{
	uint8_t m = Y;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	A = m;
	return;
}

void mos6502::Op_BRA(uint16_t src)
{
	// An extra cycle is required if a page boundary is crossed
	if (!addressesSamePage(pc, src)) opExtraCycles++;

	pc = src;
	return;
}

void mos6502::Op_TRB(uint16_t src)
{
	uint8_t m = ReadBus(src);
	SET_ZERO(m & A);
	m = m & ~A;
	WriteBus(src, m);
}

void mos6502::Op_TSB(uint16_t src)
{
	uint8_t m = ReadBus(src);
	SET_ZERO(m & A);
	m = m | A;
	WriteBus(src, m);
}

void mos6502::Op_BBRx(uint8_t mask, uint8_t val, uint16_t offset)
{
	uint16_t addr;

	if ((val & mask) == 0) {
		// Taking the branch incurs an additional cycle
		opExtraCycles++;

		if (offset & 0x80) offset |= 0xFF00;
		addr = pc + (int16_t)offset;

		// Crossing page boundary incurs another additional cycle
		if (addressesSamePage(addr, pc)) opExtraCycles++;

		pc = addr;
	}
}

void mos6502::Op_BBR0(uint16_t src)
{
	auto val = ReadBus(ReadBus(pc++));
	uint16_t offset = (uint16_t) ReadBus(pc++);

	Op_BBRx(0x01, val, offset);
}

void mos6502::Op_BBR1(uint16_t src)
{
	auto val = ReadBus(ReadBus(pc++));
	uint16_t offset = (uint16_t) ReadBus(pc++);

	Op_BBRx(0x02, val, offset);
}

void mos6502::Op_BBR2(uint16_t src)
{
	auto val = ReadBus(ReadBus(pc++));
	uint16_t offset = (uint16_t) ReadBus(pc++);

	Op_BBRx(0x04, val, offset);
}

void mos6502::Op_BBR3(uint16_t src)
{
	auto val = ReadBus(ReadBus(pc++));
	uint16_t offset = (uint16_t) ReadBus(pc++);

	Op_BBRx(0x08, val, offset);
}

void mos6502::Op_BBR4(uint16_t src)
{
	auto val = ReadBus(ReadBus(pc++));
	uint16_t offset = (uint16_t) ReadBus(pc++);

	Op_BBRx(0x10, val, offset);
}

void mos6502::Op_BBR5(uint16_t src)
{
	auto val = ReadBus(ReadBus(pc++));
	uint16_t offset = (uint16_t) ReadBus(pc++);

	Op_BBRx(0x20, val, offset);
}

void mos6502::Op_BBR6(uint16_t src)
{
	auto val = ReadBus(ReadBus(pc++));
	uint16_t offset = (uint16_t) ReadBus(pc++);

	Op_BBRx(0x40, val, offset);
}

void mos6502::Op_BBR7(uint16_t src)
{
	auto val = ReadBus(ReadBus(pc++));
	uint16_t offset = (uint16_t) ReadBus(pc++);

	Op_BBRx(0x80, val, offset);
}

void mos6502::Op_BBSx(uint8_t mask, uint8_t val, uint16_t offset)
{
	uint16_t addr;

	if ((val & mask) != 0) {
		// Taking the branch, additional cycle
		opExtraCycles++;

		if (offset & 0x80) offset |= 0xFF00;
		addr = pc + (int16_t)offset;

		// Crossing page boundary incurs an additional cycle
		if (addressesSamePage(addr, pc)) opExtraCycles++;

		pc = addr;
	}
}

void mos6502::Op_BBS0(uint16_t src)
{
	auto val = ReadBus(ReadBus(pc++));
	uint16_t offset = (uint16_t) ReadBus(pc++);

	Op_BBSx(0x01, val, offset);
}

void mos6502::Op_BBS1(uint16_t src)
{
	auto val = ReadBus(ReadBus(pc++));
	uint16_t offset = (uint16_t) ReadBus(pc++);

	Op_BBSx(0x02, val, offset);
}

void mos6502::Op_BBS2(uint16_t src)
{
	auto val = ReadBus(ReadBus(pc++));
	uint16_t offset = (uint16_t) ReadBus(pc++);

	Op_BBSx(0x04, val, offset);
}

void mos6502::Op_BBS3(uint16_t src)
{
	auto val = ReadBus(ReadBus(pc++));
	uint16_t offset = (uint16_t) ReadBus(pc++);

	Op_BBSx(0x08, val, offset);
}

void mos6502::Op_BBS4(uint16_t src)
{
	auto val = ReadBus(ReadBus(pc++));
	uint16_t offset = (uint16_t) ReadBus(pc++);

	Op_BBSx(0x10, val, offset);
}

void mos6502::Op_BBS5(uint16_t src)
{
	auto val = ReadBus(ReadBus(pc++));
	uint16_t offset = (uint16_t) ReadBus(pc++);

	Op_BBSx(0x20, val, offset);
}

void mos6502::Op_BBS6(uint16_t src)
{
	auto val = ReadBus(ReadBus(pc++));
	uint16_t offset = (uint16_t) ReadBus(pc++);

	Op_BBSx(0x40, val, offset);
}

void mos6502::Op_BBS7(uint16_t src)
{
	auto val = ReadBus(ReadBus(pc++));
	uint16_t offset = (uint16_t) ReadBus(pc++);

	Op_BBSx(0x80, val, offset);
}

void mos6502::Op_RMBx(uint8_t mask, uint16_t location)
{
	uint8_t m = ReadBus(location);
	m = m & ~mask;
	WriteBus(location, m);
}

void mos6502::Op_SMBx(uint8_t mask, uint16_t location)
{
	uint8_t m = ReadBus(location);
	m = m | mask;
	WriteBus(location, m);
}

void mos6502::Op_RMB0(uint16_t src)
{
	Op_RMBx(1, src);
}

void mos6502::Op_RMB1(uint16_t src)
{
	Op_RMBx(2, src);
}

void mos6502::Op_RMB2(uint16_t src)
{
	Op_RMBx(4, src);
}

void mos6502::Op_RMB3(uint16_t src)
{
	Op_RMBx(8, src);
}

void mos6502::Op_RMB4(uint16_t src)
{
	Op_RMBx(16, src);
}

void mos6502::Op_RMB5(uint16_t src)
{
	Op_RMBx(32, src);
}

void mos6502::Op_RMB6(uint16_t src)
{
	Op_RMBx(64, src);
}

void mos6502::Op_RMB7(uint16_t src)
{
	Op_RMBx(128, src);
}

void mos6502::Op_SMB0(uint16_t src)
{
	Op_SMBx(1, src);
}

void mos6502::Op_SMB1(uint16_t src)
{
	Op_SMBx(2, src);
}

void mos6502::Op_SMB2(uint16_t src)
{
	Op_SMBx(4, src);
}

void mos6502::Op_SMB3(uint16_t src)
{
	Op_SMBx(8, src);
}

void mos6502::Op_SMB4(uint16_t src)
{
	Op_SMBx(16, src);
}

void mos6502::Op_SMB5(uint16_t src)
{
	Op_SMBx(32, src);
}

void mos6502::Op_SMB6(uint16_t src)
{
	Op_SMBx(64, src);
}

void mos6502::Op_SMB7(uint16_t src)
{
	Op_SMBx(128, src);
}
