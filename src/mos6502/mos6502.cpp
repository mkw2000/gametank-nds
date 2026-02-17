
#include "mos6502.h"
#include "SDL_inc.h"

#ifndef ITCM_CODE
#define ITCM_CODE
#endif

mos6502::mos6502(BusRead r, BusWrite w, CPUEvent stp, BusRead sync)
{
	Write = (BusWrite)w;
	Read = (BusRead)r;
	Stopped = (CPUEvent)stp;
	Sync = (BusRead)sync;
	Instr instr;
	irq_timer = 0;

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

// Small helper function to test if addresses belong to the same page
// This is useful for conditional timing as some addressing modes take additional cycles if calculated
// addresses cross page boundaries
inline bool mos6502::addressesSamePage(uint16_t a, uint16_t b)
{
	return ((a & 0xFF00) == (b & 0xFF00));
}

ITCM_CODE uint16_t mos6502::Addr_ACC()
{
	return 0; // not used
}

ITCM_CODE uint16_t mos6502::Addr_IMM()
{
	return pc++;
}

ITCM_CODE uint16_t mos6502::Addr_ABS()
{
	uint16_t addrL;
	uint16_t addrH;
	uint16_t addr;

	addrL = Read(pc++);
	addrH = Read(pc++);

	addr = addrL + (addrH << 8);

	return addr;
}

ITCM_CODE uint16_t mos6502::Addr_ZER()
{
	return Read(pc++);
}

ITCM_CODE uint16_t mos6502::Addr_IMP()
{
	return 0; // not used
}

ITCM_CODE uint16_t mos6502::Addr_REL()
{
	uint16_t offset;
	uint16_t addr;

	offset = (uint16_t)Read(pc++);
	if (offset & 0x80) offset |= 0xFF00;
	addr = pc + (int16_t)offset;

	return addr;
}

ITCM_CODE uint16_t mos6502::Addr_ABI()
{
	uint16_t addrL;
	uint16_t addrH;
	uint16_t effL;
	uint16_t effH;
	uint16_t abs;
	uint16_t addr;

	addrL = Read(pc++);
	addrH = Read(pc++);

	abs = (addrH << 8) | addrL;

	effL = Read(abs);

#ifndef CMOS_INDIRECT_JMP_FIX
	effH = Read((abs & 0xFF00) + ((abs + 1) & 0x00FF) );
#else
	effH = Read(abs + 1);
#endif

	addr = effL + 0x100 * effH;

	return addr;
}

ITCM_CODE uint16_t mos6502::Addr_AIX()
{
	uint16_t addrL;
	uint16_t addrH;
	uint16_t effL;
	uint16_t effH;
	uint16_t abs;
	uint16_t addr;

	addrL = Read(pc++);
	addrH = Read(pc++);

	// Offset the calculated absolute address by X
	abs = ((addrH << 8) | addrL) + X;

	effL = Read(abs);

#ifndef CMOS_INDIRECT_JMP_FIX
	effH = Read((abs & 0xFF00) + ((abs + 1) & 0x00FF) );
#else
	effH = Read(abs + 1);
#endif

	addr = effL + 0x100 * effH;

	return addr;
}


ITCM_CODE uint16_t mos6502::Addr_ZEX()
{
	uint16_t addr = (Read(pc++) + X) % 256;
	return addr;
}

ITCM_CODE uint16_t mos6502::Addr_ZEY()
{
	uint16_t addr = (Read(pc++) + Y) % 256;
	return addr;
}

ITCM_CODE uint16_t mos6502::Addr_ABX()
{
	uint16_t addr;
	uint16_t addrBase;
	uint16_t addrL;
	uint16_t addrH;

	addrL = Read(pc++);
	addrH = Read(pc++);

	addrBase = addrL + (addrH << 8);
	addr = addrBase + X;

	// An extra cycle is required if a page boundary is crossed
	if (!addressesSamePage(addr, addrBase)) opExtraCycles++;

	return addr;
}

ITCM_CODE uint16_t mos6502::Addr_ABY()
{
	uint16_t addr;
	uint16_t addrBase;
	uint16_t addrL;
	uint16_t addrH;

	addrL = Read(pc++);
	addrH = Read(pc++);

	addrBase = addrL + (addrH << 8);
	addr = addrBase + Y;

	// An extra cycle is required if a page boundary is crossed
	if (!addressesSamePage(addr, addrBase)) opExtraCycles++;

	return addr;
}


ITCM_CODE uint16_t mos6502::Addr_INX()
{
	uint16_t zeroL;
	uint16_t zeroH;
	uint16_t addr;

	zeroL = (Read(pc++) + X) % 256;
	zeroH = (zeroL + 1) % 256;
	addr = Read(zeroL) + (Read(zeroH) << 8);

	return addr;
}

ITCM_CODE uint16_t mos6502::Addr_INY()
{
	uint16_t zeroL;
	uint16_t zeroH;
	uint16_t addr;
	uint16_t addrBase;

	zeroL = Read(pc++);
	zeroH = (zeroL + 1) % 256;
	addrBase = Read(zeroL) + (Read(zeroH) << 8);
	addr = addrBase + Y;

	// An extra cycle is required if a page boundary is crossed
	if (!addressesSamePage(addr, addrBase)) opExtraCycles++;

	return addr;
}

ITCM_CODE uint16_t mos6502::Addr_ZPI()
{
	uint16_t zeroL;
	uint16_t zeroH;
	uint16_t addr;

	zeroL = Read(pc++);
	zeroH = (zeroL + 1) % 256;
	addr = Read(zeroL) + (Read(zeroH) << 8);

	return addr;
}

void mos6502::Reset()
{
	A = 0x00;
	Y = 0x00;
	X = 0x00;

	pc = (Read(rstVectorH) << 8) + Read(rstVectorL); // load PC from reset vector

	sp = 0xFD;

	status |= CONSTANT;

	illegalOpcode = false;
	waiting = false;

	return;
}

ITCM_CODE void mos6502::StackPush(uint8_t byte)
{
	Write(0x0100 + sp, byte);
	if(sp == 0x00) sp = 0xFF;
	else sp--;
}

ITCM_CODE uint8_t mos6502::StackPop()
{
	if(sp == 0xFF) sp = 0x00;
	else sp++;
	return Read(0x0100 + sp);
}

ITCM_CODE void mos6502::IRQ()
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
		pc = (Read(irqVectorH) << 8) + Read(irqVectorL);
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

ITCM_CODE void mos6502::ClearIRQ() {
	irq_line = false;
	irq_timer = 0;
}

ITCM_CODE void mos6502::NMI()
{
	waiting = false;
	SET_BREAK(0);
	StackPush((pc >> 8) & 0xFF);
	StackPush(pc & 0xFF);
	StackPush(status);
	SET_INTERRUPT(1);
	pc = (Read(nmiVectorH) << 8) + Read(nmiVectorL);
	return;
}

void mos6502::Freeze()
{
	freeze = true;
}

__attribute__((always_inline)) inline void mos6502::Run(
	int32_t cyclesRemaining,
	uint64_t& cycleCount,
	CycleMethod cycleMethod
) {
	uint8_t opcode;
	uint8_t elapsedCycles;
	Instr instr;

	if(freeze) return;

	while((cyclesRemaining > 0) && !illegalOpcode)
	{
		if(waiting) {
			if(irq_line) {
				waiting = false;
				IRQ();
			} else if(irq_timer > 0) {
				if(cyclesRemaining >= irq_timer) {
					cycleCount += irq_timer;
					cyclesRemaining -= irq_timer;
					irq_timer = 0;
					if((irq_gate == NULL) || (*irq_gate)) {
						irq_line = true;
						IRQ();
					}
				} else {
					irq_timer -= cyclesRemaining;
					cycleCount += cyclesRemaining;
					cyclesRemaining = 0;
					break;

				}
			} else {
				break;
			}
		} else if(irq_line) {
			IRQ();
		}
		// fetch
		if(Sync == NULL) {
			opcode = Read(pc++);
		} else {
			opcode = Sync(pc++);
		}
		if(freeze) {
			--pc;
			cyclesRemaining = 0;
			break;
		}

		// decode
		instr = InstrTable[opcode];

		// execute
		Exec(instr);
		if(illegalOpcode) {
			illegalOpcodeSrc = opcode;
		}

		elapsedCycles = instr.cycles + opExtraCycles;
		// The ops extra cycles have been accounted for, it must now be reset
		opExtraCycles = 0;

		cycleCount += elapsedCycles;
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
}

ITCM_CODE void mos6502::Exec(Instr i)
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
	uint8_t m = Read(src);
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



ITCM_CODE void mos6502::Op_AND(uint16_t src)
{
	uint8_t m = Read(src);
	uint8_t res = m & A;
	SET_NEGATIVE(res & 0x80);
	SET_ZERO(!res);
	A = res;
	return;
}


ITCM_CODE void mos6502::Op_ASL(uint16_t src)
{
	uint8_t m = Read(src);
	SET_CARRY(m & 0x80);
	m <<= 1;
	m &= 0xFF;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	Write(src, m);
	return;
}

ITCM_CODE void mos6502::Op_ASL_ACC(uint16_t src)
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

ITCM_CODE void mos6502::Op_BCC(uint16_t src)
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


ITCM_CODE void mos6502::Op_BCS(uint16_t src)
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

ITCM_CODE void mos6502::Op_BEQ(uint16_t src)
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
	uint8_t m = Read(src);
	uint8_t res = m & A;
	SET_NEGATIVE(res & 0x80);
	status = (status & 0x3F) | (uint8_t)(m & 0xC0);
	SET_ZERO(!res);
	return;
}

ITCM_CODE void mos6502::Op_BMI(uint16_t src)
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

ITCM_CODE void mos6502::Op_BNE(uint16_t src)
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

ITCM_CODE void mos6502::Op_BPL(uint16_t src)
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
	pc = (Read(irqVectorH) << 8) + Read(irqVectorL);
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

ITCM_CODE void mos6502::Op_BVC(uint16_t src)
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

ITCM_CODE void mos6502::Op_BVS(uint16_t src)
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

ITCM_CODE void mos6502::Op_CLC(uint16_t src)
{
	SET_CARRY(0);
	return;
}

void mos6502::Op_CLD(uint16_t src)
{
	SET_DECIMAL(0);
	return;
}

ITCM_CODE void mos6502::Op_CLI(uint16_t src)
{
	SET_INTERRUPT(0);
	return;
}

ITCM_CODE void mos6502::Op_CLV(uint16_t src)
{
	SET_OVERFLOW(0);
	return;
}

ITCM_CODE void mos6502::Op_CMP(uint16_t src)
{
	unsigned int tmp = A - Read(src);
	SET_CARRY(tmp < 0x100);
	SET_NEGATIVE(tmp & 0x80);
	SET_ZERO(!(tmp & 0xFF));
	return;
}

ITCM_CODE void mos6502::Op_CPX(uint16_t src)
{
	unsigned int tmp = X - Read(src);
	SET_CARRY(tmp < 0x100);
	SET_NEGATIVE(tmp & 0x80);
	SET_ZERO(!(tmp & 0xFF));
	return;
}

ITCM_CODE void mos6502::Op_CPY(uint16_t src)
{
	unsigned int tmp = Y - Read(src);
	SET_CARRY(tmp < 0x100);
	SET_NEGATIVE(tmp & 0x80);
	SET_ZERO(!(tmp & 0xFF));
	return;
}

ITCM_CODE void mos6502::Op_DEC(uint16_t src)
{
	uint8_t m = Read(src);
	m = (m - 1) % 256;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	Write(src, m);
	return;
}

ITCM_CODE void mos6502::Op_DEC_ACC(uint16_t src)
{
	uint8_t m = A;
	m = (m - 1) % 256;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	A = m;
	return;
}

ITCM_CODE void mos6502::Op_DEX(uint16_t src)
{
	uint8_t m = X;
	m = (m - 1) % 256;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	X = m;
	return;
}

ITCM_CODE void mos6502::Op_DEY(uint16_t src)
{
	uint8_t m = Y;
	m = (m - 1) % 256;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	Y = m;
	return;
}

ITCM_CODE void mos6502::Op_EOR(uint16_t src)
{
	uint8_t m = Read(src);
	m = A ^ m;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	A = m;
}

ITCM_CODE void mos6502::Op_INC(uint16_t src)
{
	uint8_t m = Read(src);
	m = (m + 1) % 256;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	Write(src, m);
}

ITCM_CODE void mos6502::Op_INC_ACC(uint16_t src)
{
	uint8_t m = A;
	m = (m + 1) % 256;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	A = m;
}

ITCM_CODE void mos6502::Op_INX(uint16_t src)
{
	uint8_t m = X;
	m = (m + 1) % 256;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	X = m;
}

ITCM_CODE void mos6502::Op_INY(uint16_t src)
{
	uint8_t m = Y;
	m = (m + 1) % 256;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	Y = m;
}

ITCM_CODE void mos6502::Op_JMP(uint16_t src)
{
	pc = src;
}

ITCM_CODE void mos6502::Op_JSR(uint16_t src)
{
	pc--;
	StackPush((pc >> 8) & 0xFF);
	StackPush(pc & 0xFF);
	pc = src;
}

ITCM_CODE void mos6502::Op_LDA(uint16_t src)
{
	uint8_t m = Read(src);
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	A = m;
}

ITCM_CODE void mos6502::Op_LDX(uint16_t src)
{
	uint8_t m = Read(src);
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	X = m;
}

ITCM_CODE void mos6502::Op_LDY(uint16_t src)
{
	uint8_t m = Read(src);
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	Y = m;
}

ITCM_CODE void mos6502::Op_LSR(uint16_t src)
{
	uint8_t m = Read(src);
	SET_CARRY(m & 0x01);
	m >>= 1;
	SET_NEGATIVE(0);
	SET_ZERO(!m);
	Write(src, m);
}

ITCM_CODE void mos6502::Op_LSR_ACC(uint16_t src)
{
	uint8_t m = A;
	SET_CARRY(m & 0x01);
	m >>= 1;
	SET_NEGATIVE(0);
	SET_ZERO(!m);
	A = m;
}

ITCM_CODE void mos6502::Op_NOP(uint16_t src)
{
	return;
}

ITCM_CODE void mos6502::Op_ORA(uint16_t src)
{
	uint8_t m = Read(src);
	m = A | m;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	A = m;
}

ITCM_CODE void mos6502::Op_PHA(uint16_t src)
{
	StackPush(A);
	return;
}

void mos6502::Op_PHP(uint16_t src)
{
	StackPush(status | BREAK);
	return;
}

ITCM_CODE void mos6502::Op_PHX(uint16_t src)
{
	StackPush(X);
	return;
}

ITCM_CODE void mos6502::Op_PHY(uint16_t src)
{
	StackPush(Y);
	return;
}

ITCM_CODE void mos6502::Op_PLA(uint16_t src)
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

ITCM_CODE void mos6502::Op_PLX(uint16_t src)
{
	X = StackPop();
	SET_NEGATIVE(X & 0x80);
	SET_ZERO(!X);
	return;
}

ITCM_CODE void mos6502::Op_PLY(uint16_t src)
{
	Y = StackPop();
	SET_NEGATIVE(Y & 0x80);
	SET_ZERO(!Y);
	return;
}

ITCM_CODE void mos6502::Op_ROL(uint16_t src)
{
	uint16_t m = Read(src);
	m <<= 1;
	if (IF_CARRY()) m |= 0x01;
	SET_CARRY(m > 0xFF);
	m &= 0xFF;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	Write(src, m);
	return;
}

ITCM_CODE void mos6502::Op_ROL_ACC(uint16_t src)
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

ITCM_CODE void mos6502::Op_ROR(uint16_t src)
{
	uint16_t m = Read(src);
	if (IF_CARRY()) m |= 0x100;
	SET_CARRY(m & 0x01);
	m >>= 1;
	m &= 0xFF;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	Write(src, m);
	return;
}

ITCM_CODE void mos6502::Op_ROR_ACC(uint16_t src)
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

ITCM_CODE void mos6502::Op_RTI(uint16_t src)
{
	uint8_t lo, hi;

	status = StackPop();

	lo = StackPop();
	hi = StackPop();

	pc = (hi << 8) | lo;
	return;
}

ITCM_CODE void mos6502::Op_RTS(uint16_t src)
{
	uint8_t lo, hi;

	lo = StackPop();
	hi = StackPop();

	pc = ((hi << 8) | lo) + 1;
	return;
}

void mos6502::Op_SBC(uint16_t src)
{
	uint8_t m = Read(src);
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

ITCM_CODE void mos6502::Op_SEC(uint16_t src)
{
	SET_CARRY(1);
	return;
}

void mos6502::Op_SED(uint16_t src)
{
	SET_DECIMAL(1);
	return;
}

ITCM_CODE void mos6502::Op_SEI(uint16_t src)
{
	SET_INTERRUPT(1);
	return;
}

ITCM_CODE void mos6502::Op_STA(uint16_t src)
{
	Write(src, A);
	return;
}

ITCM_CODE void mos6502::Op_STZ(uint16_t src)
{
	Write(src, 0);
	return;
}

ITCM_CODE void mos6502::Op_STX(uint16_t src)
{
	Write(src, X);
	return;
}

ITCM_CODE void mos6502::Op_STY(uint16_t src)
{
	Write(src, Y);
	return;
}

ITCM_CODE void mos6502::Op_TAX(uint16_t src)
{
	uint8_t m = A;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	X = m;
	return;
}

ITCM_CODE void mos6502::Op_TAY(uint16_t src)
{
	uint8_t m = A;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	Y = m;
	return;
}

ITCM_CODE void mos6502::Op_TSX(uint16_t src)
{
	uint8_t m = sp;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	X = m;
	return;
}

ITCM_CODE void mos6502::Op_TXA(uint16_t src)
{
	uint8_t m = X;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	A = m;
	return;
}

ITCM_CODE void mos6502::Op_TXS(uint16_t src)
{
	sp = X;
	return;
}

ITCM_CODE void mos6502::Op_TYA(uint16_t src)
{
	uint8_t m = Y;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	A = m;
	return;
}

ITCM_CODE void mos6502::Op_BRA(uint16_t src)
{
	// An extra cycle is required if a page boundary is crossed
	if (!addressesSamePage(pc, src)) opExtraCycles++;

	pc = src;
	return;
}

ITCM_CODE void mos6502::Op_TRB(uint16_t src)
{
	uint8_t m = Read(src);
	SET_ZERO(m & A);
	m = m & ~A;
	Write(src, m);
}

ITCM_CODE void mos6502::Op_TSB(uint16_t src)
{
	uint8_t m = Read(src);
	SET_ZERO(m & A);
	m = m | A;
	Write(src, m);
}

ITCM_CODE void mos6502::Op_BBRx(uint8_t mask, uint8_t val, uint16_t offset)
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

ITCM_CODE void mos6502::Op_BBR0(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBRx(0x01, val, offset);
}

ITCM_CODE void mos6502::Op_BBR1(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBRx(0x02, val, offset);
}

ITCM_CODE void mos6502::Op_BBR2(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBRx(0x04, val, offset);
}

ITCM_CODE void mos6502::Op_BBR3(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBRx(0x08, val, offset);
}

ITCM_CODE void mos6502::Op_BBR4(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBRx(0x10, val, offset);
}

ITCM_CODE void mos6502::Op_BBR5(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBRx(0x20, val, offset);
}

ITCM_CODE void mos6502::Op_BBR6(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBRx(0x40, val, offset);
}

ITCM_CODE void mos6502::Op_BBR7(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBRx(0x80, val, offset);
}

ITCM_CODE void mos6502::Op_BBSx(uint8_t mask, uint8_t val, uint16_t offset)
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

ITCM_CODE void mos6502::Op_BBS0(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBSx(0x01, val, offset);
}

ITCM_CODE void mos6502::Op_BBS1(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBSx(0x02, val, offset);
}

ITCM_CODE void mos6502::Op_BBS2(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBSx(0x04, val, offset);
}

ITCM_CODE void mos6502::Op_BBS3(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBSx(0x08, val, offset);
}

ITCM_CODE void mos6502::Op_BBS4(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBSx(0x10, val, offset);
}

ITCM_CODE void mos6502::Op_BBS5(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBSx(0x20, val, offset);
}

ITCM_CODE void mos6502::Op_BBS6(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBSx(0x40, val, offset);
}

ITCM_CODE void mos6502::Op_BBS7(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBSx(0x80, val, offset);
}

ITCM_CODE void mos6502::Op_RMBx(uint8_t mask, uint16_t location)
{
	uint8_t m = Read(location);
	m = m & ~mask;
	Write(location, m);
}

ITCM_CODE void mos6502::Op_SMBx(uint8_t mask, uint16_t location)
{
	uint8_t m = Read(location);
	m = m | mask;
	Write(location, m);
}

ITCM_CODE void mos6502::Op_RMB0(uint16_t src)
{
	Op_RMBx(1, src);
}

ITCM_CODE void mos6502::Op_RMB1(uint16_t src)
{
	Op_RMBx(2, src);
}

ITCM_CODE void mos6502::Op_RMB2(uint16_t src)
{
	Op_RMBx(4, src);
}

ITCM_CODE void mos6502::Op_RMB3(uint16_t src)
{
	Op_RMBx(8, src);
}

ITCM_CODE void mos6502::Op_RMB4(uint16_t src)
{
	Op_RMBx(16, src);
}

ITCM_CODE void mos6502::Op_RMB5(uint16_t src)
{
	Op_RMBx(32, src);
}

ITCM_CODE void mos6502::Op_RMB6(uint16_t src)
{
	Op_RMBx(64, src);
}

ITCM_CODE void mos6502::Op_RMB7(uint16_t src)
{
	Op_RMBx(128, src);
}

ITCM_CODE void mos6502::Op_SMB0(uint16_t src)
{
	Op_SMBx(1, src);
}

ITCM_CODE void mos6502::Op_SMB1(uint16_t src)
{
	Op_SMBx(2, src);
}

ITCM_CODE void mos6502::Op_SMB2(uint16_t src)
{
	Op_SMBx(4, src);
}

ITCM_CODE void mos6502::Op_SMB3(uint16_t src)
{
	Op_SMBx(8, src);
}

ITCM_CODE void mos6502::Op_SMB4(uint16_t src)
{
	Op_SMBx(16, src);
}

ITCM_CODE void mos6502::Op_SMB5(uint16_t src)
{
	Op_SMBx(32, src);
}

ITCM_CODE void mos6502::Op_SMB6(uint16_t src)
{
	Op_SMBx(64, src);
}

ITCM_CODE void mos6502::Op_SMB7(uint16_t src)
{
	Op_SMBx(128, src);
}

void ITCM_CODE mos6502::RunOptimized(int32_t cyclesRemaining, uint64_t& cycleCount) {
    uint8_t opcode;
    uint8_t elapsedCycles;
    uint16_t src;

    if(freeze) return;

    while((cyclesRemaining > 0) && !illegalOpcode) {
        if(waiting) {
            if(irq_line) {
                waiting = false;
                IRQ();
            } else if(irq_timer > 0) {
                if(cyclesRemaining >= (int32_t)irq_timer) {
                    cycleCount += irq_timer;
                    cyclesRemaining -= irq_timer;
                    irq_timer = 0;
                    if((irq_gate == NULL) || (*irq_gate)) {
                        irq_line = true;
                        IRQ();
                    }
                } else {
                    irq_timer -= cyclesRemaining;
                    cycleCount += cyclesRemaining;
                    cyclesRemaining = 0;
                    break;
                }
            } else {
                // Consume all remaining cycles to fast-forward time
                cycleCount += cyclesRemaining;
                cyclesRemaining = 0;
                break;
            }
        } else if(irq_line) {
            IRQ();
        }
        if(Sync == NULL) {
            opcode = Read(pc++);
        } else {
            opcode = Sync(pc++);
        }
        if(freeze) {
            --pc;
            cyclesRemaining = 0;
            break;
        }

        // Spin-loop detection: BRA -2 (0x80 0xFE) is effectively WAI
        if (opcode == 0x80) {
             // Peek next byte without advancing PC yet (Read is idempotent for RAM/ROM)
             uint8_t offset = Read(pc);
             if (offset == 0xFE) {
                 // Found BRA -2 loop. Treat as waiting state.
                 // Back up PC to point to the opcode so we resume correctly after interrupt
                 pc--;
                 waiting = true;
                 continue;
             }
        }

        elapsedCycles = 0;
        switch(opcode) {

        case 0x00: { // BRK
            pc++;
            StackPush((pc >> 8) & 0xFF);
            StackPush(pc & 0xFF);
            StackPush(status | BREAK);
            SET_INTERRUPT(1);
            pc = (Read(irqVectorH) << 8) + Read(irqVectorL);
            elapsedCycles = 7; break;
        }
        case 0x01: { // ORA INX
            uint8_t zp = (Read(pc++) + X) & 0xFF;
            src = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            A |= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A);
            elapsedCycles = 6; break;
        }
        case 0x04: { // TSB ZER
            src = Read(pc++);
            uint8_t m = Read(src); SET_ZERO(!(A & m)); m |= A; Write(src, m);
            elapsedCycles = 5; break;
        }
        case 0x05: { // ORA ZER
            src = Read(pc++);
            A |= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A);
            elapsedCycles = 3; break;
        }
        case 0x06: { // ASL ZER
            src = Read(pc++);
            uint8_t m = Read(src); SET_CARRY(m & 0x80); m <<= 1; m &= 0xFF;
            SET_NEGATIVE(m & 0x80); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 5; break;
        }
        case 0x07: { src = Read(pc++); uint8_t m = Read(src); m &= ~0x01; Write(src, m); elapsedCycles = 5; break; } // RMB0
        case 0x08: { StackPush(status | BREAK); elapsedCycles = 3; break; } // PHP
        case 0x09: { src = pc++; A |= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A); elapsedCycles = 2; break; } // ORA IMM
        case 0x0A: { SET_CARRY(A & 0x80); A <<= 1; A &= 0xFF; SET_NEGATIVE(A & 0x80); SET_ZERO(!A); elapsedCycles = 2; break; } // ASL ACC
        case 0x0C: { // TSB ABS
            src = Read(pc) | (Read(pc+1) << 8); pc += 2;
            uint8_t m = Read(src); SET_ZERO(!(A & m)); m |= A; Write(src, m);
            elapsedCycles = 6; break;
        }
        case 0x0D: { src = Read(pc) | (Read(pc+1) << 8); pc += 2; A |= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A); elapsedCycles = 4; break; } // ORA ABS
        case 0x0E: { // ASL ABS
            src = Read(pc) | (Read(pc+1) << 8); pc += 2;
            uint8_t m = Read(src); SET_CARRY(m & 0x80); m <<= 1; m &= 0xFF;
            SET_NEGATIVE(m & 0x80); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 6; break;
        }
        case 0x0F: { auto val = Read(Read(pc++)); uint16_t offset = Read(pc++); Op_BBRx(0x01, val, offset); elapsedCycles = 5; break; } // BBR0
        case 0x10: { // BPL
            uint16_t off = Read(pc++); if(off & 0x80) off |= 0xFF00; uint16_t target = pc + (int16_t)off;
            if(!IF_NEGATIVE()) { if((pc & 0xFF00) != (target & 0xFF00)) opExtraCycles++; pc = target; opExtraCycles++; }
            elapsedCycles = 2; break;
        }
        case 0x11: { // ORA INY
            uint8_t zp = Read(pc++); uint16_t base = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            src = base + Y; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            A |= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A);
            elapsedCycles = 5; break;
        }
        case 0x12: { // ORA ZPI
            uint8_t zp = Read(pc++); src = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            A |= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A);
            elapsedCycles = 5; break;
        }
        case 0x14: { // TRB ZER
            src = Read(pc++); uint8_t m = Read(src); SET_ZERO(!(A & m)); m &= ~A; Write(src, m);
            elapsedCycles = 5; break;
        }
        case 0x15: { src = (Read(pc++) + X) & 0xFF; A |= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A); elapsedCycles = 4; break; } // ORA ZEX
        case 0x16: { // ASL ZEX
            src = (Read(pc++) + X) & 0xFF;
            uint8_t m = Read(src); SET_CARRY(m & 0x80); m <<= 1; m &= 0xFF;
            SET_NEGATIVE(m & 0x80); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 6; break;
        }
        case 0x17: { src = Read(pc++); uint8_t m = Read(src); m &= ~0x02; Write(src, m); elapsedCycles = 5; break; } // RMB1
        case 0x18: { SET_CARRY(0); elapsedCycles = 2; break; } // CLC
        case 0x19: { // ORA ABY
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2;
            src = base + Y; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            A |= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A);
            elapsedCycles = 4; break;
        }
        case 0x1A: { A++; SET_NEGATIVE(A & 0x80); SET_ZERO(!A); elapsedCycles = 2; break; } // INC_ACC
        case 0x1C: { // TRB ABS
            src = Read(pc) | (Read(pc+1) << 8); pc += 2;
            uint8_t m = Read(src); SET_ZERO(!(A & m)); m &= ~A; Write(src, m);
            elapsedCycles = 6; break;
        }
        case 0x1D: { // ORA ABX
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2;
            src = base + X; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            A |= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A);
            elapsedCycles = 4; break;
        }
        case 0x1E: { // ASL ABX
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2; src = base + X;
            uint8_t m = Read(src); SET_CARRY(m & 0x80); m <<= 1; m &= 0xFF;
            SET_NEGATIVE(m & 0x80); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 6; break;
        }
        case 0x1F: { auto val = Read(Read(pc++)); uint16_t offset = Read(pc++); Op_BBRx(0x02, val, offset); elapsedCycles = 5; break; } // BBR1
        case 0x20: { // JSR ABS
            src = Read(pc) | (Read(pc+1) << 8); pc += 2;
            pc--; StackPush((pc >> 8) & 0xFF); StackPush(pc & 0xFF); pc = src;
            elapsedCycles = 6; break;
        }
        case 0x21: { // AND INX
            uint8_t zp = (Read(pc++) + X) & 0xFF; src = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            A &= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A);
            elapsedCycles = 6; break;
        }
        case 0x24: { // BIT ZER
            src = Read(pc++); uint8_t m = Read(src); uint8_t res = m & A;
            SET_NEGATIVE(res & 0x80); status = (status & 0x3F) | (uint8_t)(m & 0xC0); SET_ZERO(!res);
            elapsedCycles = 3; break;
        }
        case 0x25: { src = Read(pc++); A &= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A); elapsedCycles = 3; break; } // AND ZER
        case 0x26: { // ROL ZER
            src = Read(pc++); uint16_t m = Read(src); m <<= 1; if(IF_CARRY()) m |= 0x01;
            SET_CARRY(m > 0xFF); m &= 0xFF; SET_NEGATIVE(m & 0x80); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 5; break;
        }
        case 0x27: { src = Read(pc++); uint8_t m = Read(src); m &= ~0x04; Write(src, m); elapsedCycles = 5; break; } // RMB2
        case 0x28: { status = StackPop(); SET_CONSTANT(1); elapsedCycles = 4; break; } // PLP
        case 0x29: { src = pc++; A &= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A); elapsedCycles = 2; break; } // AND IMM
        case 0x2A: { // ROL ACC
            uint16_t m = A; m <<= 1; if(IF_CARRY()) m |= 0x01;
            SET_CARRY(m > 0xFF); m &= 0xFF; SET_NEGATIVE(m & 0x80); SET_ZERO(!m); A = m;
            elapsedCycles = 2; break;
        }
        case 0x2C: { // BIT ABS
            src = Read(pc) | (Read(pc+1) << 8); pc += 2; uint8_t m = Read(src); uint8_t res = m & A;
            SET_NEGATIVE(res & 0x80); status = (status & 0x3F) | (uint8_t)(m & 0xC0); SET_ZERO(!res);
            elapsedCycles = 4; break;
        }
        case 0x2D: { src = Read(pc) | (Read(pc+1) << 8); pc += 2; A &= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A); elapsedCycles = 4; break; } // AND ABS
        case 0x2E: { // ROL ABS
            src = Read(pc) | (Read(pc+1) << 8); pc += 2; uint16_t m = Read(src); m <<= 1; if(IF_CARRY()) m |= 0x01;
            SET_CARRY(m > 0xFF); m &= 0xFF; SET_NEGATIVE(m & 0x80); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 6; break;
        }
        case 0x2F: { auto val = Read(Read(pc++)); uint16_t offset = Read(pc++); Op_BBRx(0x04, val, offset); elapsedCycles = 5; break; } // BBR2
        case 0x30: { // BMI
            uint16_t off = Read(pc++); if(off & 0x80) off |= 0xFF00; uint16_t target = pc + (int16_t)off;
            if(IF_NEGATIVE()) { if((pc & 0xFF00) != (target & 0xFF00)) opExtraCycles++; pc = target; opExtraCycles++; }
            elapsedCycles = 2; break;
        }
        case 0x31: { // AND INY
            uint8_t zp = Read(pc++); uint16_t base = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            src = base + Y; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            A &= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A);
            elapsedCycles = 5; break;
        }
        case 0x32: { // AND ZPI
            uint8_t zp = Read(pc++); src = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            A &= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A);
            elapsedCycles = 5; break;
        }
        case 0x34: { // BIT ZEX
            src = (Read(pc++) + X) & 0xFF; uint8_t m = Read(src); uint8_t res = m & A;
            SET_NEGATIVE(res & 0x80); status = (status & 0x3F) | (uint8_t)(m & 0xC0); SET_ZERO(!res);
            elapsedCycles = 4; break;
        }
        case 0x35: { src = (Read(pc++) + X) & 0xFF; A &= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A); elapsedCycles = 4; break; } // AND ZEX
        case 0x36: { // ROL ZEX
            src = (Read(pc++) + X) & 0xFF; uint16_t m = Read(src); m <<= 1; if(IF_CARRY()) m |= 0x01;
            SET_CARRY(m > 0xFF); m &= 0xFF; SET_NEGATIVE(m & 0x80); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 6; break;
        }
        case 0x37: { src = Read(pc++); uint8_t m = Read(src); m &= ~0x08; Write(src, m); elapsedCycles = 5; break; } // RMB3
        case 0x38: { SET_CARRY(1); elapsedCycles = 2; break; } // SEC
        case 0x39: { // AND ABY
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2;
            src = base + Y; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            A &= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A);
            elapsedCycles = 4; break;
        }
        case 0x3A: { A--; SET_NEGATIVE(A & 0x80); SET_ZERO(!A); elapsedCycles = 2; break; } // DEC_ACC
        case 0x3C: { // BIT ABX
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2;
            src = base + X; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            uint8_t m = Read(src); uint8_t res = m & A;
            SET_NEGATIVE(res & 0x80); status = (status & 0x3F) | (uint8_t)(m & 0xC0); SET_ZERO(!res);
            elapsedCycles = 4; break;
        }
        case 0x3D: { // AND ABX
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2;
            src = base + X; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            A &= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A);
            elapsedCycles = 4; break;
        }
        case 0x3E: { // ROL ABX
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2; src = base + X;
            uint16_t m = Read(src); m <<= 1; if(IF_CARRY()) m |= 0x01;
            SET_CARRY(m > 0xFF); m &= 0xFF; SET_NEGATIVE(m & 0x80); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 6; break;
        }
        case 0x3F: { auto val = Read(Read(pc++)); uint16_t offset = Read(pc++); Op_BBRx(0x08, val, offset); elapsedCycles = 5; break; } // BBR3
        case 0x40: { // RTI
            uint8_t lo, hi; status = StackPop(); lo = StackPop(); hi = StackPop(); pc = (hi << 8) | lo;
            elapsedCycles = 6; break;
        }
        case 0x41: { // EOR INX
            uint8_t zp = (Read(pc++) + X) & 0xFF; src = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            A ^= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A);
            elapsedCycles = 6; break;
        }
        case 0x45: { src = Read(pc++); A ^= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A); elapsedCycles = 3; break; } // EOR ZER
        case 0x46: { // LSR ZER
            src = Read(pc++); uint8_t m = Read(src); SET_CARRY(m & 0x01); m >>= 1;
            SET_NEGATIVE(0); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 5; break;
        }
        case 0x47: { src = Read(pc++); uint8_t m = Read(src); m &= ~0x10; Write(src, m); elapsedCycles = 5; break; } // RMB4
        case 0x48: { StackPush(A); elapsedCycles = 3; break; } // PHA
        case 0x49: { src = pc++; A ^= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A); elapsedCycles = 2; break; } // EOR IMM
        case 0x4A: { SET_CARRY(A & 0x01); A >>= 1; SET_NEGATIVE(0); SET_ZERO(!A); elapsedCycles = 2; break; } // LSR ACC
        case 0x4C: { pc = Read(pc) | (Read(pc+1) << 8); elapsedCycles = 3; break; } // JMP ABS
        case 0x4D: { src = Read(pc) | (Read(pc+1) << 8); pc += 2; A ^= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A); elapsedCycles = 4; break; } // EOR ABS
        case 0x4E: { // LSR ABS
            src = Read(pc) | (Read(pc+1) << 8); pc += 2; uint8_t m = Read(src);
            SET_CARRY(m & 0x01); m >>= 1; SET_NEGATIVE(0); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 6; break;
        }
        case 0x4F: { auto val = Read(Read(pc++)); uint16_t offset = Read(pc++); Op_BBRx(0x10, val, offset); elapsedCycles = 5; break; } // BBR4
        case 0x50: { // BVC
            uint16_t off = Read(pc++); if(off & 0x80) off |= 0xFF00; uint16_t target = pc + (int16_t)off;
            if(!IF_OVERFLOW()) { if((pc & 0xFF00) != (target & 0xFF00)) opExtraCycles++; pc = target; opExtraCycles++; }
            elapsedCycles = 2; break;
        }
        case 0x51: { // EOR INY
            uint8_t zp = Read(pc++); uint16_t base = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            src = base + Y; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            A ^= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A);
            elapsedCycles = 5; break;
        }
        case 0x52: { // EOR ZPI
            uint8_t zp = Read(pc++); src = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            A ^= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A);
            elapsedCycles = 5; break;
        }
        case 0x55: { src = (Read(pc++) + X) & 0xFF; A ^= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A); elapsedCycles = 4; break; } // EOR ZEX
        case 0x56: { // LSR ZEX
            src = (Read(pc++) + X) & 0xFF; uint8_t m = Read(src); SET_CARRY(m & 0x01); m >>= 1;
            SET_NEGATIVE(0); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 6; break;
        }
        case 0x57: { src = Read(pc++); uint8_t m = Read(src); m &= ~0x20; Write(src, m); elapsedCycles = 5; break; } // RMB5
        case 0x58: { SET_INTERRUPT(0); elapsedCycles = 2; break; } // CLI
        case 0x59: { // EOR ABY
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2;
            src = base + Y; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            A ^= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A);
            elapsedCycles = 4; break;
        }
        case 0x5A: { StackPush(Y); elapsedCycles = 3; break; } // PHY
        case 0x5D: { // EOR ABX
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2;
            src = base + X; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            A ^= Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A);
            elapsedCycles = 4; break;
        }
        case 0x5E: { // LSR ABX
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2; src = base + X;
            uint8_t m = Read(src); SET_CARRY(m & 0x01); m >>= 1; SET_NEGATIVE(0); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 6; break;
        }
        case 0x5F: { auto val = Read(Read(pc++)); uint16_t offset = Read(pc++); Op_BBRx(0x20, val, offset); elapsedCycles = 5; break; } // BBR5
        case 0x60: { // RTS
            uint8_t lo, hi; lo = StackPop(); hi = StackPop(); pc = ((hi << 8) | lo) + 1;
            elapsedCycles = 6; break;
        }
        case 0x61: { // ADC INX
            uint8_t zp = (Read(pc++) + X) & 0xFF; src = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            Op_ADC(src); elapsedCycles = 6; break;
        }
        case 0x64: { Write(Read(pc++), 0); elapsedCycles = 3; break; } // STZ ZER
        case 0x65: { Op_ADC(Read(pc++)); elapsedCycles = 3; break; } // ADC ZER
        case 0x66: { // ROR ZER
            src = Read(pc++); uint16_t m = Read(src); if(IF_CARRY()) m |= 0x100;
            SET_CARRY(m & 0x01); m >>= 1; m &= 0xFF; SET_NEGATIVE(m & 0x80); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 5; break;
        }
        case 0x67: { src = Read(pc++); uint8_t m = Read(src); m &= ~0x40; Write(src, m); elapsedCycles = 5; break; } // RMB6
        case 0x68: { A = StackPop(); SET_NEGATIVE(A & 0x80); SET_ZERO(!A); elapsedCycles = 4; break; } // PLA
        case 0x69: { Op_ADC(pc++); elapsedCycles = 2; break; } // ADC IMM
        case 0x6A: { // ROR ACC
            uint16_t m = A; if(IF_CARRY()) m |= 0x100;
            SET_CARRY(m & 0x01); m >>= 1; m &= 0xFF; SET_NEGATIVE(m & 0x80); SET_ZERO(!m); A = m;
            elapsedCycles = 2; break;
        }
        case 0x6C: { // JMP ABI
            uint16_t abs = Read(pc) | (Read(pc+1) << 8); pc += 2;
#ifndef CMOS_INDIRECT_JMP_FIX
            pc = Read(abs) | (Read((abs & 0xFF00) + ((abs + 1) & 0x00FF)) << 8);
#else
            pc = Read(abs) | (Read(abs + 1) << 8);
#endif
            elapsedCycles = 5; break;
        }
        case 0x6D: { src = Read(pc) | (Read(pc+1) << 8); pc += 2; Op_ADC(src); elapsedCycles = 4; break; } // ADC ABS
        case 0x6E: { // ROR ABS
            src = Read(pc) | (Read(pc+1) << 8); pc += 2;
            uint16_t m = Read(src); if(IF_CARRY()) m |= 0x100;
            SET_CARRY(m & 0x01); m >>= 1; m &= 0xFF; SET_NEGATIVE(m & 0x80); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 6; break;
        }
        case 0x6F: { auto val = Read(Read(pc++)); uint16_t offset = Read(pc++); Op_BBRx(0x40, val, offset); elapsedCycles = 5; break; } // BBR6
        case 0x70: { // BVS
            uint16_t off = Read(pc++); if(off & 0x80) off |= 0xFF00; uint16_t target = pc + (int16_t)off;
            if(IF_OVERFLOW()) { if((pc & 0xFF00) != (target & 0xFF00)) opExtraCycles++; pc = target; opExtraCycles++; }
            elapsedCycles = 2; break;
        }
        case 0x71: { // ADC INY
            uint8_t zp = Read(pc++); uint16_t base = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            src = base + Y; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            Op_ADC(src); elapsedCycles = 6; break;
        }
        case 0x72: { // ADC ZPI
            uint8_t zp = Read(pc++); src = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            Op_ADC(src); elapsedCycles = 6; break;
        }
        case 0x74: { Write((Read(pc++) + X) & 0xFF, 0); elapsedCycles = 4; break; } // STZ ZEX
        case 0x75: { Op_ADC((Read(pc++) + X) & 0xFF); elapsedCycles = 4; break; } // ADC ZEX
        case 0x76: { // ROR ZEX
            src = (Read(pc++) + X) & 0xFF; uint16_t m = Read(src); if(IF_CARRY()) m |= 0x100;
            SET_CARRY(m & 0x01); m >>= 1; m &= 0xFF; SET_NEGATIVE(m & 0x80); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 6; break;
        }
        case 0x77: { src = Read(pc++); uint8_t m = Read(src); m &= ~0x80; Write(src, m); elapsedCycles = 5; break; } // RMB7
        case 0x78: { SET_INTERRUPT(1); elapsedCycles = 2; break; } // SEI
        case 0x79: { // ADC ABY
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2;
            src = base + Y; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            Op_ADC(src); elapsedCycles = 4; break;
        }
        case 0x7A: { Y = StackPop(); SET_NEGATIVE(Y & 0x80); SET_ZERO(!Y); elapsedCycles = 4; break; } // PLY
        case 0x7C: { // JMP AIX
            uint16_t abs = (Read(pc) | (Read(pc+1) << 8)) + X; pc += 2;
#ifndef CMOS_INDIRECT_JMP_FIX
            pc = Read(abs) | (Read((abs & 0xFF00) + ((abs + 1) & 0x00FF)) << 8);
#else
            pc = Read(abs) | (Read(abs + 1) << 8);
#endif
            elapsedCycles = 6; break;
        }
        case 0x7D: { // ADC ABX
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2;
            src = base + X; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            Op_ADC(src); elapsedCycles = 4; break;
        }
        case 0x7E: { // ROR ABX
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2; src = base + X;
            uint16_t m = Read(src); if(IF_CARRY()) m |= 0x100;
            SET_CARRY(m & 0x01); m >>= 1; m &= 0xFF; SET_NEGATIVE(m & 0x80); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 6; break;
        }
        case 0x7F: { auto val = Read(Read(pc++)); uint16_t offset = Read(pc++); Op_BBRx(0x80, val, offset); elapsedCycles = 5; break; } // BBR7
        case 0x80: { // BRA
            uint16_t off = Read(pc++); if(off & 0x80) off |= 0xFF00; uint16_t target = pc + (int16_t)off;
            if((pc & 0xFF00) != (target & 0xFF00)) opExtraCycles++;
            pc = target; elapsedCycles = 3; break;
        }
        case 0x81: { // STA INX
            uint8_t zp = (Read(pc++) + X) & 0xFF; src = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            Write(src, A); elapsedCycles = 6; break;
        }
        case 0x84: { Write(Read(pc++), Y); elapsedCycles = 3; break; } // STY ZER
        case 0x85: { Write(Read(pc++), A); elapsedCycles = 3; break; } // STA ZER
        case 0x86: { Write(Read(pc++), X); elapsedCycles = 3; break; } // STX ZER
        case 0x87: { src = Read(pc++); uint8_t m = Read(src); m |= 0x01; Write(src, m); elapsedCycles = 5; break; } // SMB0
        case 0x88: { Y--; SET_NEGATIVE(Y & 0x80); SET_ZERO(!Y); elapsedCycles = 2; break; } // DEY
        case 0x89: { // BIT IMM - same behavior as original Op_BIT
            src = pc++; uint8_t m = Read(src); uint8_t res = m & A;
            SET_NEGATIVE(res & 0x80); status = (status & 0x3F) | (uint8_t)(m & 0xC0); SET_ZERO(!res);
            elapsedCycles = 2; break;
        }
        case 0x8A: { A = X; SET_NEGATIVE(A & 0x80); SET_ZERO(!A); elapsedCycles = 2; break; } // TXA
        case 0x8C: { src = Read(pc) | (Read(pc+1) << 8); pc += 2; Write(src, Y); elapsedCycles = 4; break; } // STY ABS
        case 0x8D: { src = Read(pc) | (Read(pc+1) << 8); pc += 2; Write(src, A); elapsedCycles = 4; break; } // STA ABS
        case 0x8E: { src = Read(pc) | (Read(pc+1) << 8); pc += 2; Write(src, X); elapsedCycles = 4; break; } // STX ABS
        case 0x8F: { auto val = Read(Read(pc++)); uint16_t offset = Read(pc++); Op_BBSx(0x01, val, offset); elapsedCycles = 5; break; } // BBS0
        case 0x90: { // BCC
            uint16_t off = Read(pc++); if(off & 0x80) off |= 0xFF00; uint16_t target = pc + (int16_t)off;
            if(!IF_CARRY()) { if((pc & 0xFF00) != (target & 0xFF00)) opExtraCycles++; pc = target; opExtraCycles++; }
            elapsedCycles = 2; break;
        }
        case 0x91: { // STA INY
            uint8_t zp = Read(pc++); src = Read(zp) | (Read((zp+1) & 0xFF) << 8); src += Y;
            Write(src, A); elapsedCycles = 6; break;
        }
        case 0x92: { // STA ZPI
            uint8_t zp = Read(pc++); src = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            Write(src, A); elapsedCycles = 5; break;
        }
        case 0x94: { Write((Read(pc++) + X) & 0xFF, Y); elapsedCycles = 4; break; } // STY ZEX
        case 0x95: { Write((Read(pc++) + X) & 0xFF, A); elapsedCycles = 4; break; } // STA ZEX
        case 0x96: { Write((Read(pc++) + Y) & 0xFF, X); elapsedCycles = 4; break; } // STX ZEY
        case 0x97: { src = Read(pc++); uint8_t m = Read(src); m |= 0x02; Write(src, m); elapsedCycles = 5; break; } // SMB1
        case 0x98: { A = Y; SET_NEGATIVE(A & 0x80); SET_ZERO(!A); elapsedCycles = 2; break; } // TYA
        case 0x99: { // STA ABY
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2; Write(base + Y, A);
            elapsedCycles = 5; break;
        }
        case 0x9A: { sp = X; elapsedCycles = 2; break; } // TXS
        case 0x9C: { src = Read(pc) | (Read(pc+1) << 8); pc += 2; Write(src, 0); elapsedCycles = 4; break; } // STZ ABS
        case 0x9D: { // STA ABX
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2; Write(base + X, A);
            elapsedCycles = 5; break;
        }
        case 0x9E: { // STZ ABX
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2; Write(base + X, 0);
            elapsedCycles = 5; break;
        }
        case 0x9F: { auto val = Read(Read(pc++)); uint16_t offset = Read(pc++); Op_BBSx(0x02, val, offset); elapsedCycles = 5; break; } // BBS1
        case 0xA0: { Y = Read(pc++); SET_NEGATIVE(Y & 0x80); SET_ZERO(!Y); elapsedCycles = 2; break; } // LDY IMM
        case 0xA1: { // LDA INX
            uint8_t zp = (Read(pc++) + X) & 0xFF; src = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            A = Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A);
            elapsedCycles = 6; break;
        }
        case 0xA2: { X = Read(pc++); SET_NEGATIVE(X & 0x80); SET_ZERO(!X); elapsedCycles = 2; break; } // LDX IMM
        case 0xA4: { Y = Read(Read(pc++)); SET_NEGATIVE(Y & 0x80); SET_ZERO(!Y); elapsedCycles = 3; break; } // LDY ZER
        case 0xA5: { A = Read(Read(pc++)); SET_NEGATIVE(A & 0x80); SET_ZERO(!A); elapsedCycles = 3; break; } // LDA ZER
        case 0xA6: { X = Read(Read(pc++)); SET_NEGATIVE(X & 0x80); SET_ZERO(!X); elapsedCycles = 3; break; } // LDX ZER
        case 0xA7: { src = Read(pc++); uint8_t m = Read(src); m |= 0x04; Write(src, m); elapsedCycles = 5; break; } // SMB2
        case 0xA8: { Y = A; SET_NEGATIVE(Y & 0x80); SET_ZERO(!Y); elapsedCycles = 2; break; } // TAY
        case 0xA9: { A = Read(pc++); SET_NEGATIVE(A & 0x80); SET_ZERO(!A); elapsedCycles = 2; break; } // LDA IMM
        case 0xAA: { X = A; SET_NEGATIVE(X & 0x80); SET_ZERO(!X); elapsedCycles = 2; break; } // TAX
        case 0xAC: { src = Read(pc) | (Read(pc+1) << 8); pc += 2; Y = Read(src); SET_NEGATIVE(Y & 0x80); SET_ZERO(!Y); elapsedCycles = 4; break; } // LDY ABS
        case 0xAD: { src = Read(pc) | (Read(pc+1) << 8); pc += 2; A = Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A); elapsedCycles = 4; break; } // LDA ABS
        case 0xAE: { src = Read(pc) | (Read(pc+1) << 8); pc += 2; X = Read(src); SET_NEGATIVE(X & 0x80); SET_ZERO(!X); elapsedCycles = 4; break; } // LDX ABS
        case 0xAF: { auto val = Read(Read(pc++)); uint16_t offset = Read(pc++); Op_BBSx(0x04, val, offset); elapsedCycles = 5; break; } // BBS2
        case 0xB0: { // BCS
            uint16_t off = Read(pc++); if(off & 0x80) off |= 0xFF00; uint16_t target = pc + (int16_t)off;
            if(IF_CARRY()) { if((pc & 0xFF00) != (target & 0xFF00)) opExtraCycles++; pc = target; opExtraCycles++; }
            elapsedCycles = 2; break;
        }
        case 0xB1: { // LDA INY
            uint8_t zp = Read(pc++); uint16_t base = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            src = base + Y; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            A = Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A);
            elapsedCycles = 5; break;
        }
        case 0xB2: { // LDA ZPI
            uint8_t zp = Read(pc++); src = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            A = Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A);
            elapsedCycles = 5; break;
        }
        case 0xB4: { Y = Read((Read(pc++) + X) & 0xFF); SET_NEGATIVE(Y & 0x80); SET_ZERO(!Y); elapsedCycles = 4; break; } // LDY ZEX
        case 0xB5: { A = Read((Read(pc++) + X) & 0xFF); SET_NEGATIVE(A & 0x80); SET_ZERO(!A); elapsedCycles = 4; break; } // LDA ZEX
        case 0xB6: { X = Read((Read(pc++) + Y) & 0xFF); SET_NEGATIVE(X & 0x80); SET_ZERO(!X); elapsedCycles = 4; break; } // LDX ZEY
        case 0xB7: { src = Read(pc++); uint8_t m = Read(src); m |= 0x08; Write(src, m); elapsedCycles = 5; break; } // SMB3
        case 0xB8: { SET_OVERFLOW(0); elapsedCycles = 2; break; } // CLV
        case 0xB9: { // LDA ABY
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2;
            src = base + Y; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            A = Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A);
            elapsedCycles = 4; break;
        }
        case 0xBA: { X = sp; SET_NEGATIVE(X & 0x80); SET_ZERO(!X); elapsedCycles = 2; break; } // TSX
        case 0xBC: { // LDY ABX
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2;
            src = base + X; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            Y = Read(src); SET_NEGATIVE(Y & 0x80); SET_ZERO(!Y);
            elapsedCycles = 4; break;
        }
        case 0xBD: { // LDA ABX
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2;
            src = base + X; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            A = Read(src); SET_NEGATIVE(A & 0x80); SET_ZERO(!A);
            elapsedCycles = 4; break;
        }
        case 0xBE: { // LDX ABY
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2;
            src = base + Y; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            X = Read(src); SET_NEGATIVE(X & 0x80); SET_ZERO(!X);
            elapsedCycles = 4; break;
        }
        case 0xBF: { auto val = Read(Read(pc++)); uint16_t offset = Read(pc++); Op_BBSx(0x08, val, offset); elapsedCycles = 5; break; } // BBS3
        case 0xC0: { unsigned int tmp = Y - Read(pc++); SET_CARRY(tmp < 0x100); SET_NEGATIVE(tmp & 0x80); SET_ZERO(!(tmp & 0xFF)); elapsedCycles = 2; break; } // CPY IMM
        case 0xC1: { // CMP INX
            uint8_t zp = (Read(pc++) + X) & 0xFF; src = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            unsigned int tmp = A - Read(src); SET_CARRY(tmp < 0x100); SET_NEGATIVE(tmp & 0x80); SET_ZERO(!(tmp & 0xFF));
            elapsedCycles = 6; break;
        }
        case 0xC4: { unsigned int tmp = Y - Read(Read(pc++)); SET_CARRY(tmp < 0x100); SET_NEGATIVE(tmp & 0x80); SET_ZERO(!(tmp & 0xFF)); elapsedCycles = 3; break; } // CPY ZER
        case 0xC5: { unsigned int tmp = A - Read(Read(pc++)); SET_CARRY(tmp < 0x100); SET_NEGATIVE(tmp & 0x80); SET_ZERO(!(tmp & 0xFF)); elapsedCycles = 3; break; } // CMP ZER
        case 0xC6: { // DEC ZER
            src = Read(pc++); uint8_t m = Read(src); m = (m - 1) % 256;
            SET_NEGATIVE(m & 0x80); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 5; break;
        }
        case 0xC7: { src = Read(pc++); uint8_t m = Read(src); m |= 0x10; Write(src, m); elapsedCycles = 5; break; } // SMB4
        case 0xC8: { Y = (Y + 1) % 256; SET_NEGATIVE(Y & 0x80); SET_ZERO(!Y); elapsedCycles = 2; break; } // INY
        case 0xC9: { unsigned int tmp = A - Read(pc++); SET_CARRY(tmp < 0x100); SET_NEGATIVE(tmp & 0x80); SET_ZERO(!(tmp & 0xFF)); elapsedCycles = 2; break; } // CMP IMM
        case 0xCA: { X = (X - 1) % 256; SET_NEGATIVE(X & 0x80); SET_ZERO(!X); elapsedCycles = 2; break; } // DEX
        case 0xCB: { waiting = true; elapsedCycles = 3; break; } // WAI
        case 0xCC: { src = Read(pc) | (Read(pc+1) << 8); pc += 2; unsigned int tmp = Y - Read(src); SET_CARRY(tmp < 0x100); SET_NEGATIVE(tmp & 0x80); SET_ZERO(!(tmp & 0xFF)); elapsedCycles = 4; break; } // CPY ABS
        case 0xCD: { src = Read(pc) | (Read(pc+1) << 8); pc += 2; unsigned int tmp = A - Read(src); SET_CARRY(tmp < 0x100); SET_NEGATIVE(tmp & 0x80); SET_ZERO(!(tmp & 0xFF)); elapsedCycles = 4; break; } // CMP ABS
        case 0xCE: { // DEC ABS
            src = Read(pc) | (Read(pc+1) << 8); pc += 2; uint8_t m = Read(src); m = (m - 1) % 256;
            SET_NEGATIVE(m & 0x80); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 6; break;
        }
        case 0xCF: { auto val = Read(Read(pc++)); uint16_t offset = Read(pc++); Op_BBSx(0x10, val, offset); elapsedCycles = 5; break; } // BBS4
        case 0xD0: { // BNE
            uint16_t off = Read(pc++); if(off & 0x80) off |= 0xFF00; uint16_t target = pc + (int16_t)off;
            if(!IF_ZERO()) { if((pc & 0xFF00) != (target & 0xFF00)) opExtraCycles++; pc = target; opExtraCycles++; }
            elapsedCycles = 2; break;
        }
        case 0xD1: { // CMP INY
            uint8_t zp = Read(pc++); uint16_t base = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            src = base + Y; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            unsigned int tmp = A - Read(src); SET_CARRY(tmp < 0x100); SET_NEGATIVE(tmp & 0x80); SET_ZERO(!(tmp & 0xFF));
            elapsedCycles = 3; break;
        }
        case 0xD2: { // CMP ZPI
            uint8_t zp = Read(pc++); src = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            unsigned int tmp = A - Read(src); SET_CARRY(tmp < 0x100); SET_NEGATIVE(tmp & 0x80); SET_ZERO(!(tmp & 0xFF));
            elapsedCycles = 5; break;
        }
        case 0xD5: { unsigned int tmp = A - Read((Read(pc++) + X) & 0xFF); SET_CARRY(tmp < 0x100); SET_NEGATIVE(tmp & 0x80); SET_ZERO(!(tmp & 0xFF)); elapsedCycles = 4; break; } // CMP ZEX
        case 0xD6: { // DEC ZEX
            src = (Read(pc++) + X) & 0xFF; uint8_t m = Read(src); m = (m - 1) % 256;
            SET_NEGATIVE(m & 0x80); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 6; break;
        }
        case 0xD7: { src = Read(pc++); uint8_t m = Read(src); m |= 0x20; Write(src, m); elapsedCycles = 5; break; } // SMB5
        case 0xD8: { SET_DECIMAL(0); elapsedCycles = 2; break; } // CLD
        case 0xD9: { // CMP ABY
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2;
            src = base + Y; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            unsigned int tmp = A - Read(src); SET_CARRY(tmp < 0x100); SET_NEGATIVE(tmp & 0x80); SET_ZERO(!(tmp & 0xFF));
            elapsedCycles = 4; break;
        }
        case 0xDA: { StackPush(X); elapsedCycles = 3; break; } // PHX
        case 0xDB: { illegalOpcode = true; Stopped(); elapsedCycles = 3; break; } // STP
        case 0xDD: { // CMP ABX
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2;
            src = base + X; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            unsigned int tmp = A - Read(src); SET_CARRY(tmp < 0x100); SET_NEGATIVE(tmp & 0x80); SET_ZERO(!(tmp & 0xFF));
            elapsedCycles = 4; break;
        }
        case 0xDE: { // DEC ABX
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2; src = base + X;
            uint8_t m = Read(src); m = (m - 1) % 256; SET_NEGATIVE(m & 0x80); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 7; break;
        }
        case 0xDF: { auto val = Read(Read(pc++)); uint16_t offset = Read(pc++); Op_BBSx(0x20, val, offset); elapsedCycles = 5; break; } // BBS5
        case 0xE0: { unsigned int tmp = X - Read(pc++); SET_CARRY(tmp < 0x100); SET_NEGATIVE(tmp & 0x80); SET_ZERO(!(tmp & 0xFF)); elapsedCycles = 2; break; } // CPX IMM
        case 0xE1: { // SBC INX
            uint8_t zp = (Read(pc++) + X) & 0xFF; src = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            Op_SBC(src); elapsedCycles = 6; break;
        }
        case 0xE4: { unsigned int tmp = X - Read(Read(pc++)); SET_CARRY(tmp < 0x100); SET_NEGATIVE(tmp & 0x80); SET_ZERO(!(tmp & 0xFF)); elapsedCycles = 3; break; } // CPX ZER
        case 0xE5: { Op_SBC(Read(pc++)); elapsedCycles = 3; break; } // SBC ZER
        case 0xE6: { // INC ZER
            src = Read(pc++); uint8_t m = Read(src); m = (m + 1) % 256;
            SET_NEGATIVE(m & 0x80); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 5; break;
        }
        case 0xE7: { src = Read(pc++); uint8_t m = Read(src); m |= 0x40; Write(src, m); elapsedCycles = 5; break; } // SMB6
        case 0xE8: { X = (X + 1) % 256; SET_NEGATIVE(X & 0x80); SET_ZERO(!X); elapsedCycles = 2; break; } // INX
        case 0xE9: { Op_SBC(pc++); elapsedCycles = 2; break; } // SBC IMM
        case 0xEA: { elapsedCycles = 2; break; } // NOP
        case 0xEC: { src = Read(pc) | (Read(pc+1) << 8); pc += 2; unsigned int tmp = X - Read(src); SET_CARRY(tmp < 0x100); SET_NEGATIVE(tmp & 0x80); SET_ZERO(!(tmp & 0xFF)); elapsedCycles = 4; break; } // CPX ABS
        case 0xED: { src = Read(pc) | (Read(pc+1) << 8); pc += 2; Op_SBC(src); elapsedCycles = 4; break; } // SBC ABS
        case 0xEE: { // INC ABS
            src = Read(pc) | (Read(pc+1) << 8); pc += 2; uint8_t m = Read(src); m = (m + 1) % 256;
            SET_NEGATIVE(m & 0x80); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 6; break;
        }
        case 0xEF: { auto val = Read(Read(pc++)); uint16_t offset = Read(pc++); Op_BBSx(0x40, val, offset); elapsedCycles = 5; break; } // BBS6
        case 0xF0: { // BEQ
            uint16_t off = Read(pc++); if(off & 0x80) off |= 0xFF00; uint16_t target = pc + (int16_t)off;
            if(IF_ZERO()) { if((pc & 0xFF00) != (target & 0xFF00)) opExtraCycles++; pc = target; opExtraCycles++; }
            elapsedCycles = 2; break;
        }
        case 0xF1: { // SBC INY
            uint8_t zp = Read(pc++); uint16_t base = Read(zp) | (Read((zp+1) & 0xFF) << 8);
            src = base + Y; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            Op_SBC(src); elapsedCycles = 5; break;
        }
        case 0xF2: { uint8_t zp = Read(pc++); src = Read(zp) | (Read((zp+1) & 0xFF) << 8); Op_SBC(src); elapsedCycles = 5; break; } // SBC ZPI
        case 0xF5: { Op_SBC((Read(pc++) + X) & 0xFF); elapsedCycles = 4; break; } // SBC ZEX
        case 0xF6: { // INC ZEX
            src = (Read(pc++) + X) & 0xFF; uint8_t m = Read(src); m = (m + 1) % 256;
            SET_NEGATIVE(m & 0x80); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 6; break;
        }
        case 0xF7: { src = Read(pc++); uint8_t m = Read(src); m |= 0x80; Write(src, m); elapsedCycles = 5; break; } // SMB7
        case 0xF8: { SET_DECIMAL(1); elapsedCycles = 2; break; } // SED
        case 0xF9: { // SBC ABY
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2;
            src = base + Y; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            Op_SBC(src); elapsedCycles = 4; break;
        }
        case 0xFA: { X = StackPop(); SET_NEGATIVE(X & 0x80); SET_ZERO(!X); elapsedCycles = 4; break; } // PLX
        case 0xFD: { // SBC ABX
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2;
            src = base + X; if((src & 0xFF00) != (base & 0xFF00)) opExtraCycles++;
            Op_SBC(src); elapsedCycles = 4; break;
        }
        case 0xFE: { // INC ABX
            uint16_t base = Read(pc) | (Read(pc+1) << 8); pc += 2; src = base + X;
            uint8_t m = Read(src); m = (m + 1) % 256; SET_NEGATIVE(m & 0x80); SET_ZERO(!m); Write(src, m);
            elapsedCycles = 7; break;
        }
        case 0xFF: { auto val = Read(Read(pc++)); uint16_t offset = Read(pc++); Op_BBSx(0x80, val, offset); elapsedCycles = 5; break; } // BBS7
        default: { illegalOpcode = true; illegalOpcodeSrc = opcode; break; }
        } // end switch

        elapsedCycles += opExtraCycles;
        opExtraCycles = 0;

        cycleCount += elapsedCycles;
        cyclesRemaining -= elapsedCycles;
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
}