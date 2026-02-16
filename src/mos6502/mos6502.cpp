
#include "mos6502.h"

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

uint16_t mos6502::Addr_ACC()
{
	return 0; // not used
}

uint16_t mos6502::Addr_IMM()
{
	return pc++;
}

uint16_t mos6502::Addr_ABS()
{
	uint16_t addrL;
	uint16_t addrH;
	uint16_t addr;

	addrL = Read(pc++);
	addrH = Read(pc++);

	addr = addrL + (addrH << 8);

	return addr;
}

uint16_t mos6502::Addr_ZER()
{
	return Read(pc++);
}

uint16_t mos6502::Addr_IMP()
{
	return 0; // not used
}

uint16_t mos6502::Addr_REL()
{
	uint16_t offset;
	uint16_t addr;

	offset = (uint16_t)Read(pc++);
	if (offset & 0x80) offset |= 0xFF00;
	addr = pc + (int16_t)offset;

	return addr;
}

uint16_t mos6502::Addr_ABI()
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

uint16_t mos6502::Addr_AIX()
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


uint16_t mos6502::Addr_ZEX()
{
	uint16_t addr = (Read(pc++) + X) % 256;
	return addr;
}

uint16_t mos6502::Addr_ZEY()
{
	uint16_t addr = (Read(pc++) + Y) % 256;
	return addr;
}

uint16_t mos6502::Addr_ABX()
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

uint16_t mos6502::Addr_ABY()
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


uint16_t mos6502::Addr_INX()
{
	uint16_t zeroL;
	uint16_t zeroH;
	uint16_t addr;

	zeroL = (Read(pc++) + X) % 256;
	zeroH = (zeroL + 1) % 256;
	addr = Read(zeroL) + (Read(zeroH) << 8);

	return addr;
}

uint16_t mos6502::Addr_INY()
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

uint16_t mos6502::Addr_ZPI()
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

void mos6502::StackPush(uint8_t byte)
{
	Write(0x0100 + sp, byte);
	if(sp == 0x00) sp = 0xFF;
	else sp--;
}

uint8_t mos6502::StackPop()
{
	if(sp == 0xFF) sp = 0x00;
	else sp++;
	return Read(0x0100 + sp);
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
	pc = (Read(nmiVectorH) << 8) + Read(nmiVectorL);
	return;
}

void mos6502::Freeze()
{
	freeze = true;
}

void mos6502::Run(
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



void mos6502::Op_AND(uint16_t src)
{
	uint8_t m = Read(src);
	uint8_t res = m & A;
	SET_NEGATIVE(res & 0x80);
	SET_ZERO(!res);
	A = res;
	return;
}


void mos6502::Op_ASL(uint16_t src)
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
	uint8_t m = Read(src);
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
	unsigned int tmp = A - Read(src);
	SET_CARRY(tmp < 0x100);
	SET_NEGATIVE(tmp & 0x80);
	SET_ZERO(!(tmp & 0xFF));
	return;
}

void mos6502::Op_CPX(uint16_t src)
{
	unsigned int tmp = X - Read(src);
	SET_CARRY(tmp < 0x100);
	SET_NEGATIVE(tmp & 0x80);
	SET_ZERO(!(tmp & 0xFF));
	return;
}

void mos6502::Op_CPY(uint16_t src)
{
	unsigned int tmp = Y - Read(src);
	SET_CARRY(tmp < 0x100);
	SET_NEGATIVE(tmp & 0x80);
	SET_ZERO(!(tmp & 0xFF));
	return;
}

void mos6502::Op_DEC(uint16_t src)
{
	uint8_t m = Read(src);
	m = (m - 1) % 256;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	Write(src, m);
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
	uint8_t m = Read(src);
	m = A ^ m;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	A = m;
}

void mos6502::Op_INC(uint16_t src)
{
	uint8_t m = Read(src);
	m = (m + 1) % 256;
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	Write(src, m);
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
	uint8_t m = Read(src);
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	A = m;
}

void mos6502::Op_LDX(uint16_t src)
{
	uint8_t m = Read(src);
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	X = m;
}

void mos6502::Op_LDY(uint16_t src)
{
	uint8_t m = Read(src);
	SET_NEGATIVE(m & 0x80);
	SET_ZERO(!m);
	Y = m;
}

void mos6502::Op_LSR(uint16_t src)
{
	uint8_t m = Read(src);
	SET_CARRY(m & 0x01);
	m >>= 1;
	SET_NEGATIVE(0);
	SET_ZERO(!m);
	Write(src, m);
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
	uint8_t m = Read(src);
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
	Write(src, A);
	return;
}

void mos6502::Op_STZ(uint16_t src)
{
	Write(src, 0);
	return;
}

void mos6502::Op_STX(uint16_t src)
{
	Write(src, X);
	return;
}

void mos6502::Op_STY(uint16_t src)
{
	Write(src, Y);
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
	uint8_t m = Read(src);
	SET_ZERO(m & A);
	m = m & ~A;
	Write(src, m);
}

void mos6502::Op_TSB(uint16_t src)
{
	uint8_t m = Read(src);
	SET_ZERO(m & A);
	m = m | A;
	Write(src, m);
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
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBRx(0x01, val, offset);
}

void mos6502::Op_BBR1(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBRx(0x02, val, offset);
}

void mos6502::Op_BBR2(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBRx(0x04, val, offset);
}

void mos6502::Op_BBR3(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBRx(0x08, val, offset);
}

void mos6502::Op_BBR4(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBRx(0x10, val, offset);
}

void mos6502::Op_BBR5(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBRx(0x20, val, offset);
}

void mos6502::Op_BBR6(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBRx(0x40, val, offset);
}

void mos6502::Op_BBR7(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

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
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBSx(0x01, val, offset);
}

void mos6502::Op_BBS1(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBSx(0x02, val, offset);
}

void mos6502::Op_BBS2(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBSx(0x04, val, offset);
}

void mos6502::Op_BBS3(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBSx(0x08, val, offset);
}

void mos6502::Op_BBS4(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBSx(0x10, val, offset);
}

void mos6502::Op_BBS5(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBSx(0x20, val, offset);
}

void mos6502::Op_BBS6(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBSx(0x40, val, offset);
}

void mos6502::Op_BBS7(uint16_t src)
{
	auto val = Read(Read(pc++));
	uint16_t offset = (uint16_t) Read(pc++);

	Op_BBSx(0x80, val, offset);
}

void mos6502::Op_RMBx(uint8_t mask, uint16_t location)
{
	uint8_t m = Read(location);
	m = m & ~mask;
	Write(location, m);
}

void mos6502::Op_SMBx(uint8_t mask, uint16_t location)
{
	uint8_t m = Read(location);
	m = m | mask;
	Write(location, m);
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

// Optimized switch-based instruction dispatch
// This eliminates function pointer indirection for ~15-30% speedup on ARM
void mos6502::RunOptimized(int32_t cyclesRemaining, uint64_t& cycleCount) {
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
				break;
			}
		} else if(irq_line) {
			IRQ();
		}

		// Fetch
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

		// Decode and execute via giant switch
		// Addressing mode computed inline, then opcode executed
		switch(opcode) {
			// ADC instructions
			case 0x69: src = Addr_IMM(); Op_ADC(src); elapsedCycles = 2; break;
			case 0x65: src = Addr_ZER(); Op_ADC(src); elapsedCycles = 3; break;
			case 0x75: src = Addr_ZEX(); Op_ADC(src); elapsedCycles = 4; break;
			case 0x6D: src = Addr_ABS(); Op_ADC(src); elapsedCycles = 4; break;
			case 0x7D: src = Addr_ABX(); Op_ADC(src); elapsedCycles = 4; break;
			case 0x79: src = Addr_ABY(); Op_ADC(src); elapsedCycles = 4; break;
			case 0x61: src = Addr_INX(); Op_ADC(src); elapsedCycles = 6; break;
			case 0x71: src = Addr_INY(); Op_ADC(src); elapsedCycles = 5; break;
			case 0x72: src = Addr_ZPI(); Op_ADC(src); elapsedCycles = 6; break;

			// AND instructions
			case 0x29: src = Addr_IMM(); Op_AND(src); elapsedCycles = 2; break;
			case 0x25: src = Addr_ZER(); Op_AND(src); elapsedCycles = 3; break;
			case 0x35: src = Addr_ZEX(); Op_AND(src); elapsedCycles = 4; break;
			case 0x2D: src = Addr_ABS(); Op_AND(src); elapsedCycles = 4; break;
			case 0x3D: src = Addr_ABX(); Op_AND(src); elapsedCycles = 4; break;
			case 0x39: src = Addr_ABY(); Op_AND(src); elapsedCycles = 4; break;
			case 0x21: src = Addr_INX(); Op_AND(src); elapsedCycles = 6; break;
			case 0x31: src = Addr_INY(); Op_AND(src); elapsedCycles = 5; break;
			case 0x32: src = Addr_ZPI(); Op_AND(src); elapsedCycles = 5; break;

			// ASL instructions
			case 0x0A: src = Addr_ACC(); Op_ASL_ACC(src); elapsedCycles = 2; break;
			case 0x06: src = Addr_ZER(); Op_ASL(src); elapsedCycles = 5; break;
			case 0x16: src = Addr_ZEX(); Op_ASL(src); elapsedCycles = 6; break;
			case 0x0E: src = Addr_ABS(); Op_ASL(src); elapsedCycles = 6; break;
			case 0x1E: src = Addr_ABX(); Op_ASL(src); elapsedCycles = 6; break;

			// Branch instructions
			case 0x90: src = Addr_REL(); Op_BCC(src); elapsedCycles = 2; break;
			case 0xB0: src = Addr_REL(); Op_BCS(src); elapsedCycles = 2; break;
			case 0xF0: src = Addr_REL(); Op_BEQ(src); elapsedCycles = 2; break;
			case 0x30: src = Addr_REL(); Op_BMI(src); elapsedCycles = 2; break;
			case 0xD0: src = Addr_REL(); Op_BNE(src); elapsedCycles = 2; break;
			case 0x10: src = Addr_REL(); Op_BPL(src); elapsedCycles = 2; break;
			case 0x50: src = Addr_REL(); Op_BVC(src); elapsedCycles = 2; break;
			case 0x70: src = Addr_REL(); Op_BVS(src); elapsedCycles = 2; break;
			case 0x80: src = Addr_REL(); Op_BRA(src); elapsedCycles = 3; break;

			// BIT instructions
			case 0x24: src = Addr_ZER(); Op_BIT(src); elapsedCycles = 3; break;
			case 0x2C: src = Addr_ABS(); Op_BIT(src); elapsedCycles = 4; break;

			// BRK
			case 0x00: src = Addr_IMP(); Op_BRK(src); elapsedCycles = 7; break;

			// Clear flag instructions
			case 0x18: src = Addr_IMP(); Op_CLC(src); elapsedCycles = 2; break;
			case 0xD8: src = Addr_IMP(); Op_CLD(src); elapsedCycles = 2; break;
			case 0x58: src = Addr_IMP(); Op_CLI(src); elapsedCycles = 2; break;
			case 0xB8: src = Addr_IMP(); Op_CLV(src); elapsedCycles = 2; break;

			// CMP instructions
			case 0xC9: src = Addr_IMM(); Op_CMP(src); elapsedCycles = 2; break;
			case 0xC5: src = Addr_ZER(); Op_CMP(src); elapsedCycles = 3; break;
			case 0xD5: src = Addr_ZEX(); Op_CMP(src); elapsedCycles = 4; break;
			case 0xCD: src = Addr_ABS(); Op_CMP(src); elapsedCycles = 4; break;
			case 0xDD: src = Addr_ABX(); Op_CMP(src); elapsedCycles = 4; break;
			case 0xD9: src = Addr_ABY(); Op_CMP(src); elapsedCycles = 4; break;
			case 0xC1: src = Addr_INX(); Op_CMP(src); elapsedCycles = 6; break;
			case 0xD1: src = Addr_INY(); Op_CMP(src); elapsedCycles = 5; break;
			case 0xD2: src = Addr_ZPI(); Op_CMP(src); elapsedCycles = 5; break;

			// CPX instructions
			case 0xE0: src = Addr_IMM(); Op_CPX(src); elapsedCycles = 2; break;
			case 0xE4: src = Addr_ZER(); Op_CPX(src); elapsedCycles = 3; break;
			case 0xEC: src = Addr_ABS(); Op_CPX(src); elapsedCycles = 4; break;

			// CPY instructions
			case 0xC0: src = Addr_IMM(); Op_CPY(src); elapsedCycles = 2; break;
			case 0xC4: src = Addr_ZER(); Op_CPY(src); elapsedCycles = 3; break;
			case 0xCC: src = Addr_ABS(); Op_CPY(src); elapsedCycles = 4; break;

			// DEC instructions
			case 0x3A: src = Addr_ACC(); Op_DEC_ACC(src); elapsedCycles = 2; break;
			case 0xC6: src = Addr_ZER(); Op_DEC(src); elapsedCycles = 5; break;
			case 0xD6: src = Addr_ZEX(); Op_DEC(src); elapsedCycles = 6; break;
			case 0xCE: src = Addr_ABS(); Op_DEC(src); elapsedCycles = 6; break;
			case 0xDE: src = Addr_ABX(); Op_DEC(src); elapsedCycles = 7; break;

			// DEX, DEY
			case 0xCA: src = Addr_IMP(); Op_DEX(src); elapsedCycles = 2; break;
			case 0x88: src = Addr_IMP(); Op_DEY(src); elapsedCycles = 2; break;

			// EOR instructions
			case 0x49: src = Addr_IMM(); Op_EOR(src); elapsedCycles = 2; break;
			case 0x45: src = Addr_ZER(); Op_EOR(src); elapsedCycles = 3; break;
			case 0x55: src = Addr_ZEX(); Op_EOR(src); elapsedCycles = 4; break;
			case 0x4D: src = Addr_ABS(); Op_EOR(src); elapsedCycles = 4; break;
			case 0x5D: src = Addr_ABX(); Op_EOR(src); elapsedCycles = 4; break;
			case 0x59: src = Addr_ABY(); Op_EOR(src); elapsedCycles = 4; break;
			case 0x41: src = Addr_INX(); Op_EOR(src); elapsedCycles = 6; break;
			case 0x51: src = Addr_INY(); Op_EOR(src); elapsedCycles = 5; break;
			case 0x52: src = Addr_ZPI(); Op_EOR(src); elapsedCycles = 5; break;

			// INC instructions
			case 0x1A: src = Addr_ACC(); Op_INC_ACC(src); elapsedCycles = 2; break;
			case 0xE6: src = Addr_ZER(); Op_INC(src); elapsedCycles = 5; break;
			case 0xF6: src = Addr_ZEX(); Op_INC(src); elapsedCycles = 6; break;
			case 0xEE: src = Addr_ABS(); Op_INC(src); elapsedCycles = 6; break;
			case 0xFE: src = Addr_ABX(); Op_INC(src); elapsedCycles = 7; break;

			// INX, INY
			case 0xE8: src = Addr_IMP(); Op_INX(src); elapsedCycles = 2; break;
			case 0xC8: src = Addr_IMP(); Op_INY(src); elapsedCycles = 2; break;

			// JMP instructions
			case 0x4C: src = Addr_ABS(); Op_JMP(src); elapsedCycles = 3; break;
			case 0x6C: src = Addr_ABI(); Op_JMP(src); elapsedCycles = 5; break;
			case 0x7C: src = Addr_AIX(); Op_JMP(src); elapsedCycles = 6; break;

			// JSR
			case 0x20: src = Addr_ABS(); Op_JSR(src); elapsedCycles = 6; break;

			// LDA instructions
			case 0xA9: src = Addr_IMM(); Op_LDA(src); elapsedCycles = 2; break;
			case 0xA5: src = Addr_ZER(); Op_LDA(src); elapsedCycles = 3; break;
			case 0xB5: src = Addr_ZEX(); Op_LDA(src); elapsedCycles = 4; break;
			case 0xAD: src = Addr_ABS(); Op_LDA(src); elapsedCycles = 4; break;
			case 0xBD: src = Addr_ABX(); Op_LDA(src); elapsedCycles = 4; break;
			case 0xB9: src = Addr_ABY(); Op_LDA(src); elapsedCycles = 4; break;
			case 0xA1: src = Addr_INX(); Op_LDA(src); elapsedCycles = 6; break;
			case 0xB1: src = Addr_INY(); Op_LDA(src); elapsedCycles = 5; break;
			case 0xB2: src = Addr_ZPI(); Op_LDA(src); elapsedCycles = 5; break;

			// LDX instructions
			case 0xA2: src = Addr_IMM(); Op_LDX(src); elapsedCycles = 2; break;
			case 0xA6: src = Addr_ZER(); Op_LDX(src); elapsedCycles = 3; break;
			case 0xB6: src = Addr_ZEY(); Op_LDX(src); elapsedCycles = 4; break;
			case 0xAE: src = Addr_ABS(); Op_LDX(src); elapsedCycles = 4; break;
			case 0xBE: src = Addr_ABY(); Op_LDX(src); elapsedCycles = 4; break;

			// LDY instructions
			case 0xA0: src = Addr_IMM(); Op_LDY(src); elapsedCycles = 2; break;
			case 0xA4: src = Addr_ZER(); Op_LDY(src); elapsedCycles = 3; break;
			case 0xB4: src = Addr_ZEX(); Op_LDY(src); elapsedCycles = 4; break;
			case 0xAC: src = Addr_ABS(); Op_LDY(src); elapsedCycles = 4; break;
			case 0xBC: src = Addr_ABX(); Op_LDY(src); elapsedCycles = 4; break;

			// LSR instructions
			case 0x4A: src = Addr_ACC(); Op_LSR_ACC(src); elapsedCycles = 2; break;
			case 0x46: src = Addr_ZER(); Op_LSR(src); elapsedCycles = 5; break;
			case 0x56: src = Addr_ZEX(); Op_LSR(src); elapsedCycles = 6; break;
			case 0x4E: src = Addr_ABS(); Op_LSR(src); elapsedCycles = 6; break;
			case 0x5E: src = Addr_ABX(); Op_LSR(src); elapsedCycles = 6; break;

			// NOP
			case 0xEA: src = Addr_IMP(); Op_NOP(src); elapsedCycles = 2; break;

			// ORA instructions
			case 0x09: src = Addr_IMM(); Op_ORA(src); elapsedCycles = 2; break;
			case 0x05: src = Addr_ZER(); Op_ORA(src); elapsedCycles = 3; break;
			case 0x15: src = Addr_ZEX(); Op_ORA(src); elapsedCycles = 4; break;
			case 0x0D: src = Addr_ABS(); Op_ORA(src); elapsedCycles = 4; break;
			case 0x1D: src = Addr_ABX(); Op_ORA(src); elapsedCycles = 4; break;
			case 0x19: src = Addr_ABY(); Op_ORA(src); elapsedCycles = 4; break;
			case 0x01: src = Addr_INX(); Op_ORA(src); elapsedCycles = 6; break;
			case 0x11: src = Addr_INY(); Op_ORA(src); elapsedCycles = 5; break;
			case 0x12: src = Addr_ZPI(); Op_ORA(src); elapsedCycles = 5; break;

			// Push/Pop instructions
			case 0x48: src = Addr_IMP(); Op_PHA(src); elapsedCycles = 3; break;
			case 0x08: src = Addr_IMP(); Op_PHP(src); elapsedCycles = 3; break;
			case 0x5A: src = Addr_IMP(); Op_PHY(src); elapsedCycles = 3; break;
			case 0xDA: src = Addr_IMP(); Op_PHX(src); elapsedCycles = 3; break;
			case 0x68: src = Addr_IMP(); Op_PLA(src); elapsedCycles = 4; break;
			case 0x28: src = Addr_IMP(); Op_PLP(src); elapsedCycles = 4; break;
			case 0x7A: src = Addr_IMP(); Op_PLY(src); elapsedCycles = 4; break;
			case 0xFA: src = Addr_IMP(); Op_PLX(src); elapsedCycles = 4; break;

			// ROL instructions
			case 0x2A: src = Addr_ACC(); Op_ROL_ACC(src); elapsedCycles = 2; break;
			case 0x26: src = Addr_ZER(); Op_ROL(src); elapsedCycles = 5; break;
			case 0x36: src = Addr_ZEX(); Op_ROL(src); elapsedCycles = 6; break;
			case 0x2E: src = Addr_ABS(); Op_ROL(src); elapsedCycles = 6; break;
			case 0x3E: src = Addr_ABX(); Op_ROL(src); elapsedCycles = 7; break;

			// ROR instructions
			case 0x6A: src = Addr_ACC(); Op_ROR_ACC(src); elapsedCycles = 2; break;
			case 0x66: src = Addr_ZER(); Op_ROR(src); elapsedCycles = 5; break;
			case 0x76: src = Addr_ZEX(); Op_ROR(src); elapsedCycles = 6; break;
			case 0x6E: src = Addr_ABS(); Op_ROR(src); elapsedCycles = 6; break;
			case 0x7E: src = Addr_ABX(); Op_ROR(src); elapsedCycles = 7; break;

			// RTI, RTS
			case 0x40: src = Addr_IMP(); Op_RTI(src); elapsedCycles = 6; break;
			case 0x60: src = Addr_IMP(); Op_RTS(src); elapsedCycles = 6; break;

			// SBC instructions
			case 0xE9: src = Addr_IMM(); Op_SBC(src); elapsedCycles = 2; break;
			case 0xE5: src = Addr_ZER(); Op_SBC(src); elapsedCycles = 3; break;
			case 0xF5: src = Addr_ZEX(); Op_SBC(src); elapsedCycles = 4; break;
			case 0xED: src = Addr_ABS(); Op_SBC(src); elapsedCycles = 4; break;
			case 0xFD: src = Addr_ABX(); Op_SBC(src); elapsedCycles = 4; break;
			case 0xF9: src = Addr_ABY(); Op_SBC(src); elapsedCycles = 4; break;
			case 0xE1: src = Addr_INX(); Op_SBC(src); elapsedCycles = 6; break;
			case 0xF1: src = Addr_INY(); Op_SBC(src); elapsedCycles = 5; break;
			case 0xF2: src = Addr_ZPI(); Op_SBC(src); elapsedCycles = 5; break;

			// Set flag instructions
			case 0x38: src = Addr_IMP(); Op_SEC(src); elapsedCycles = 2; break;
			case 0xF8: src = Addr_IMP(); Op_SED(src); elapsedCycles = 2; break;
			case 0x78: src = Addr_IMP(); Op_SEI(src); elapsedCycles = 2; break;

			// STA instructions
			case 0x85: src = Addr_ZER(); Op_STA(src); elapsedCycles = 3; break;
			case 0x95: src = Addr_ZEX(); Op_STA(src); elapsedCycles = 4; break;
			case 0x8D: src = Addr_ABS(); Op_STA(src); elapsedCycles = 4; break;
			case 0x9D: src = Addr_ABX(); Op_STA(src); elapsedCycles = 5; break;
			case 0x99: src = Addr_ABY(); Op_STA(src); elapsedCycles = 5; break;
			case 0x81: src = Addr_INX(); Op_STA(src); elapsedCycles = 6; break;
			case 0x91: src = Addr_INY(); Op_STA(src); elapsedCycles = 6; break;
			case 0x92: src = Addr_ZPI(); Op_STA(src); elapsedCycles = 5; break;

			// STX instructions
			case 0x86: src = Addr_ZER(); Op_STX(src); elapsedCycles = 3; break;
			case 0x96: src = Addr_ZEY(); Op_STX(src); elapsedCycles = 4; break;
			case 0x8E: src = Addr_ABS(); Op_STX(src); elapsedCycles = 4; break;

			// STY instructions
			case 0x84: src = Addr_ZER(); Op_STY(src); elapsedCycles = 3; break;
			case 0x94: src = Addr_ZEX(); Op_STY(src); elapsedCycles = 4; break;
			case 0x8C: src = Addr_ABS(); Op_STY(src); elapsedCycles = 4; break;

			// STZ instructions (65C02)
			case 0x64: src = Addr_ZER(); Op_STZ(src); elapsedCycles = 3; break;
			case 0x74: src = Addr_ZEX(); Op_STZ(src); elapsedCycles = 4; break;
			case 0x9C: src = Addr_ABS(); Op_STZ(src); elapsedCycles = 4; break;
			case 0x9E: src = Addr_ABX(); Op_STZ(src); elapsedCycles = 5; break;

			// Transfer instructions
			case 0xAA: src = Addr_IMP(); Op_TAX(src); elapsedCycles = 2; break;
			case 0xA8: src = Addr_IMP(); Op_TAY(src); elapsedCycles = 2; break;
			case 0xBA: src = Addr_IMP(); Op_TSX(src); elapsedCycles = 2; break;
			case 0x8A: src = Addr_IMP(); Op_TXA(src); elapsedCycles = 2; break;
			case 0x9A: src = Addr_IMP(); Op_TXS(src); elapsedCycles = 2; break;
			case 0x98: src = Addr_IMP(); Op_TYA(src); elapsedCycles = 2; break;

			// TRB, TSB (65C02)
			case 0x14: src = Addr_ZER(); Op_TRB(src); elapsedCycles = 5; break;
			case 0x1C: src = Addr_ABS(); Op_TRB(src); elapsedCycles = 6; break;
			case 0x04: src = Addr_ZER(); Op_TSB(src); elapsedCycles = 5; break;
			case 0x0C: src = Addr_ABS(); Op_TSB(src); elapsedCycles = 6; break;

			// WAI, STP (65C02)
			case 0xCB: src = Addr_IMP(); Op_WAI(src); elapsedCycles = 3; break;
			case 0xDB: src = Addr_IMP(); Op_STP(src); elapsedCycles = 3; break;

			// BBRx instructions (65C02)
			case 0x0F: src = Addr_ZER(); Op_BBR0(src); elapsedCycles = 5; break;
			case 0x1F: src = Addr_ZER(); Op_BBR1(src); elapsedCycles = 5; break;
			case 0x2F: src = Addr_ZER(); Op_BBR2(src); elapsedCycles = 5; break;
			case 0x3F: src = Addr_ZER(); Op_BBR3(src); elapsedCycles = 5; break;
			case 0x4F: src = Addr_ZER(); Op_BBR4(src); elapsedCycles = 5; break;
			case 0x5F: src = Addr_ZER(); Op_BBR5(src); elapsedCycles = 5; break;
			case 0x6F: src = Addr_ZER(); Op_BBR6(src); elapsedCycles = 5; break;
			case 0x7F: src = Addr_ZER(); Op_BBR7(src); elapsedCycles = 5; break;

			// BBSx instructions (65C02)
			case 0x8F: src = Addr_ZER(); Op_BBS0(src); elapsedCycles = 5; break;
			case 0x9F: src = Addr_ZER(); Op_BBS1(src); elapsedCycles = 5; break;
			case 0xAF: src = Addr_ZER(); Op_BBS2(src); elapsedCycles = 5; break;
			case 0xBF: src = Addr_ZER(); Op_BBS3(src); elapsedCycles = 5; break;
			case 0xCF: src = Addr_ZER(); Op_BBS4(src); elapsedCycles = 5; break;
			case 0xDF: src = Addr_ZER(); Op_BBS5(src); elapsedCycles = 5; break;
			case 0xEF: src = Addr_ZER(); Op_BBS6(src); elapsedCycles = 5; break;
			case 0xFF: src = Addr_ZER(); Op_BBS7(src); elapsedCycles = 5; break;

			// RMBx instructions (65C02)
			case 0x07: src = Addr_ZER(); Op_RMB0(src); elapsedCycles = 5; break;
			case 0x17: src = Addr_ZER(); Op_RMB1(src); elapsedCycles = 5; break;
			case 0x27: src = Addr_ZER(); Op_RMB2(src); elapsedCycles = 5; break;
			case 0x37: src = Addr_ZER(); Op_RMB3(src); elapsedCycles = 5; break;
			case 0x47: src = Addr_ZER(); Op_RMB4(src); elapsedCycles = 5; break;
			case 0x57: src = Addr_ZER(); Op_RMB5(src); elapsedCycles = 5; break;
			case 0x67: src = Addr_ZER(); Op_RMB6(src); elapsedCycles = 5; break;
			case 0x77: src = Addr_ZER(); Op_RMB7(src); elapsedCycles = 5; break;

			// SMBx instructions (65C02)
			case 0x87: src = Addr_ZER(); Op_SMB0(src); elapsedCycles = 5; break;
			case 0x97: src = Addr_ZER(); Op_SMB1(src); elapsedCycles = 5; break;
			case 0xA7: src = Addr_ZER(); Op_SMB2(src); elapsedCycles = 5; break;
			case 0xB7: src = Addr_ZER(); Op_SMB3(src); elapsedCycles = 5; break;
			case 0xC7: src = Addr_ZER(); Op_SMB4(src); elapsedCycles = 5; break;
			case 0xD7: src = Addr_ZER(); Op_SMB5(src); elapsedCycles = 5; break;
			case 0xE7: src = Addr_ZER(); Op_SMB6(src); elapsedCycles = 5; break;
			case 0xF7: src = Addr_ZER(); Op_SMB7(src); elapsedCycles = 5; break;

			// Illegal opcodes
			default:
				src = Addr_IMP();
				Op_ILLEGAL(src);
				elapsedCycles = 2;
				break;
		}

		if(illegalOpcode) {
			illegalOpcodeSrc = opcode;
		}

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