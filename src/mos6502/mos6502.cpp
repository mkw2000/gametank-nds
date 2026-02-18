
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
extern RomType loadedRomType;
extern uint8_t* cached_ram_ptr;
extern bool* cached_ram_init_ptr;
extern uint8_t* cached_rom_lo_ptr;
extern uint8_t* cached_rom_hi_ptr;
extern uint16_t cached_rom_linear_mask;
extern uint32_t cached_rom_decode_epoch;
extern uint8_t open_bus();
extern uint8_t VDMA_Read(uint16_t address);
extern void VDMA_Write(uint16_t address, uint8_t value);
extern void UpdateFlashShiftRegister(uint8_t nextVal);
extern "C" uint8_t GT_AudioRamRead(uint16_t address);
extern "C" void GT_AudioRamWrite(uint16_t address, uint8_t value);
extern "C" uint8_t GT_JoystickReadFast(uint8_t portNum);

#ifndef NDS_OPCODE_PROFILE_STRIDE
#define NDS_OPCODE_PROFILE_STRIDE 33u
#endif

static inline bool NDSMainReadFast(uint16_t address, uint8_t& out)
{
	if(address & 0x8000) {
		const RomType romType = loadedRomType;
		if (LIKELY((romType == RomType::FLASH2M) || (romType == RomType::FLASH2M_RAM32K))) {
			out = (address & 0x4000)
				? cached_rom_hi_ptr[address & 0x3FFF]
				: cached_rom_lo_ptr[address & 0x3FFF];
			return true;
		}
		if ((romType == RomType::EEPROM32K) || (romType == RomType::EEPROM8K)) {
			out = cached_rom_lo_ptr[address & cached_rom_linear_mask];
			return true;
		}
	}
	if(address < 0x2000) {
		out = cached_ram_ptr[address];
		return true;
	}
	// VDMA reads are timing-coupled with the blitter and must flush cycles.
	if (address & 0x4000) {
		return false;
	}
	if ((address >= 0x3000) && (address <= 0x3FFF)) {
		out = GT_AudioRamRead(address);
		return true;
	}
	if ((address >= 0x2800) && (address <= 0x2FFF)) {
		out = system_state.VIA_regs[address & 0xF];
		return true;
	}
	if ((address == 0x2008) || (address == 0x2009)) {
		out = GT_JoystickReadFast((uint8_t)address);
		return true;
	}
	out = open_bus();
	return true;
}

struct NDSRomDecodeEntry {
	uint32_t tag;  // (epoch << 16) | (pc & ~0x1FF) for validation
	uint8_t opcode;
	uint8_t op1;
	uint8_t op2;
	uint16_t abs;
	uint8_t abs_read_mode;
	int16_t rel;
	uint16_t rel_target;
	uint8_t rel_taken_cycles;
	const uint8_t* abs_ptr;
};

// Cache size: 512 entries * ~24 bytes = ~12KB
// Smaller table = better ARM9 data cache utilization (4KB D-cache)
// Direct-mapped: index by PC & 0x1FF, tag includes high bits + epoch
#define NDS_DECODE_CACHE_SIZE 512
#define NDS_DECODE_CACHE_MASK 0x1FF
static NDSRomDecodeEntry g_nds_rom_decode[NDS_DECODE_CACHE_SIZE];

enum NDSAbsReadMode : uint8_t {
	NDS_ABS_READ_RAM = 0,
	NDS_ABS_READ_ROM_LO,
	NDS_ABS_READ_ROM_HI,
	NDS_ABS_READ_ROM_LINEAR,
	NDS_ABS_READ_AUDIO,
	NDS_ABS_READ_VIA,
	NDS_ABS_READ_JOY,
	NDS_ABS_READ_OPEN_BUS,
	NDS_ABS_READ_FALLBACK
};

static inline uint8_t NDSClassifyAbsReadMode(uint16_t address)
{
	if (address < 0x2000) return NDS_ABS_READ_RAM;
	if (address & 0x8000) {
		const RomType romType = loadedRomType;
		if (LIKELY((romType == RomType::FLASH2M) || (romType == RomType::FLASH2M_RAM32K))) {
			return (address & 0x4000) ? NDS_ABS_READ_ROM_HI : NDS_ABS_READ_ROM_LO;
		}
		if ((romType == RomType::EEPROM32K) || (romType == RomType::EEPROM8K)) {
			return NDS_ABS_READ_ROM_LINEAR;
		}
		return NDS_ABS_READ_FALLBACK;
	}
	if (address & 0x4000) return NDS_ABS_READ_FALLBACK; // VDMA/timing-coupled
	if ((address >= 0x3000) && (address <= 0x3FFF)) return NDS_ABS_READ_AUDIO;
	if ((address >= 0x2800) && (address <= 0x2FFF)) return NDS_ABS_READ_VIA;
	if ((address == 0x2008) || (address == 0x2009)) return NDS_ABS_READ_JOY;
	return NDS_ABS_READ_OPEN_BUS;
}

static inline const NDSRomDecodeEntry& NDSGetRomDecode(uint16_t opPc)
{
	NDSRomDecodeEntry& entry = g_nds_rom_decode[opPc & NDS_DECODE_CACHE_MASK];
	const uint32_t epoch = cached_rom_decode_epoch;
	const uint32_t expected_tag = (epoch << 16) | (opPc & ~NDS_DECODE_CACHE_MASK);
	if (UNLIKELY(entry.tag != expected_tag)) {
		entry.tag = expected_tag;
		if (!NDSMainReadFast(opPc, entry.opcode)) entry.opcode = 0xFF;
		if (!NDSMainReadFast((uint16_t)(opPc + 1), entry.op1)) entry.op1 = 0;
		if (!NDSMainReadFast((uint16_t)(opPc + 2), entry.op2)) entry.op2 = 0;
		entry.abs = (uint16_t)(entry.op1 | (entry.op2 << 8));
		entry.abs_read_mode = NDSClassifyAbsReadMode(entry.abs);
		entry.rel = (int16_t)(int8_t)entry.op1;
		const uint16_t rel_base = (uint16_t)(opPc + 2);
		entry.rel_target = (uint16_t)(rel_base + entry.rel);
		entry.rel_taken_cycles = (uint8_t)(3 + (((rel_base ^ entry.rel_target) & 0xFF00) ? 1 : 0));
		switch (entry.abs_read_mode) {
			case NDS_ABS_READ_ROM_LO:
				entry.abs_ptr = &cached_rom_lo_ptr[entry.abs & 0x3FFF];
				break;
			case NDS_ABS_READ_ROM_HI:
				entry.abs_ptr = &cached_rom_hi_ptr[entry.abs & 0x3FFF];
				break;
			case NDS_ABS_READ_ROM_LINEAR:
				entry.abs_ptr = &cached_rom_lo_ptr[entry.abs & cached_rom_linear_mask];
				break;
			case NDS_ABS_READ_VIA:
				entry.abs_ptr = &system_state.VIA_regs[entry.abs & 0xF];
				break;
			default:
				entry.abs_ptr = nullptr;
				break;
		}
	}
	return entry;
}
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
	if (LIKELY(Sync == NULL)) {
	// GameTank map fast path:
	// 0000-1FFF: RAM, 2008/2009: controller, 2800-2FFF: VIA mirror,
	// 3000-3FFF: audio RAM, 4000-7FFF: VDMA, 8000-FFFF: cart ROM/flash.
	if(address & 0x8000) {
		const RomType romType = loadedRomType;
		if (LIKELY((romType == RomType::FLASH2M) || (romType == RomType::FLASH2M_RAM32K))) {
			return (address & 0x4000)
				? cached_rom_hi_ptr[address & 0x3FFF]
				: cached_rom_lo_ptr[address & 0x3FFF];
		}
		if ((romType == RomType::EEPROM32K) || (romType == RomType::EEPROM8K)) {
			return cached_rom_lo_ptr[address & cached_rom_linear_mask];
		}
	}
	if(address < 0x2000) {
		return cached_ram_ptr[address];
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
	}
#endif
	FlushRunCycles();
	return (*Read)(address);
}

inline void mos6502::WriteBus(uint16_t address, uint8_t value)
{
#if defined(NDS_BUILD) && defined(ARM9)
	if (LIKELY(Sync == NULL)) {
	if(address < 0x2000) {
		cached_ram_init_ptr[address] = true;
		cached_ram_ptr[address] = value;
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
	}
#endif
	FlushRunCycles();
	(*Write)(address, value);
}

inline uint8_t mos6502::FetchByte()
{
	const uint16_t address = pc++;
#if defined(NDS_BUILD) && defined(ARM9)
	if (LIKELY(Sync == NULL)) {
	// Instruction stream is usually ROM; keep this path as lean as possible.
	if (address & 0x8000) {
		const RomType romType = loadedRomType;
		if (LIKELY((romType == RomType::FLASH2M) || (romType == RomType::FLASH2M_RAM32K))) {
			return (address & 0x4000)
				? cached_rom_hi_ptr[address & 0x3FFF]
				: cached_rom_lo_ptr[address & 0x3FFF];
		}
		if ((romType == RomType::EEPROM32K) || (romType == RomType::EEPROM8K)) {
			return cached_rom_lo_ptr[address & cached_rom_linear_mask];
		}
	}

		if (address < 0x2000) {
			return cached_ram_ptr[address];
		}
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

inline void mos6502::ADCFast(uint8_t m)
{
	const unsigned int carryIn = IF_CARRY() ? 1u : 0u;
	unsigned int tmp = m + A + carryIn;
	SET_ZERO(!(tmp & 0xFF));
	if (IF_DECIMAL())
	{
		opExtraCycles += 1;
		if (((A & 0xF) + (m & 0xF) + carryIn) > 9) tmp += 6;
		SET_NEGATIVE(tmp & 0x80);
		SET_OVERFLOW(!((A ^ m) & 0x80) && ((A ^ tmp) & 0x80));
		if (tmp > 0x99) {
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
	A = (uint8_t)(tmp & 0xFF);
}

inline void mos6502::SBCFast(uint8_t m)
{
	const unsigned int borrowIn = IF_CARRY() ? 0u : 1u;
	unsigned int tmp = A - m - borrowIn;
	SET_NEGATIVE(tmp & 0x80);
	SET_ZERO(!(tmp & 0xFF));
	SET_OVERFLOW(((A ^ tmp) & 0x80) && ((A ^ m) & 0x80));

	if (IF_DECIMAL())
	{
		opExtraCycles += 1;
		if (((A & 0x0F) - borrowIn) < (m & 0x0F)) tmp -= 6;
		if (tmp > 0x99) {
			tmp -= 0x60;
		}
	}
	SET_CARRY(tmp < 0x100);
	A = (uint8_t)(tmp & 0xFF);
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

	addrL = FetchByte();
	addrH = FetchByte();

	addr = addrL + (addrH << 8);

	return addr;
}

uint16_t ITCM_CODE mos6502::Addr_ZER()
{
	return FetchByte();
}

uint16_t ITCM_CODE mos6502::Addr_IMP()
{
	return 0; // not used
}

uint16_t ITCM_CODE mos6502::Addr_REL()
{
	uint16_t offset;
	uint16_t addr;

	offset = (uint16_t)FetchByte();
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

	addrL = FetchByte();
	addrH = FetchByte();

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

	addrL = FetchByte();
	addrH = FetchByte();

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
	uint16_t addr = (FetchByte() + X) % 256;
	return addr;
}

uint16_t ITCM_CODE mos6502::Addr_ZEY()
{
	uint16_t addr = (FetchByte() + Y) % 256;
	return addr;
}

uint16_t ITCM_CODE mos6502::Addr_ABX()
{
	uint16_t addr;
	uint16_t addrBase;
	uint16_t addrL;
	uint16_t addrH;

	addrL = FetchByte();
	addrH = FetchByte();

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

	addrL = FetchByte();
	addrH = FetchByte();

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

	zeroL = (FetchByte() + X) % 256;
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

	zeroL = FetchByte();
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

	zeroL = FetchByte();
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

void mos6502::StackPush(uint8_t byte)
{
#if defined(NDS_BUILD) && defined(ARM9)
	if (LIKELY(Sync == NULL)) {
		const uint16_t addr = (uint16_t)(0x0100u + sp);
		cached_ram_init_ptr[addr] = true;
		cached_ram_ptr[addr] = byte;
		if (sp == 0x00) sp = 0xFF;
		else sp--;
		return;
	}
#endif
	WriteBus(0x0100 + sp, byte);
	if(sp == 0x00) sp = 0xFF;
	else sp--;
}

uint8_t mos6502::StackPop()
{
#if defined(NDS_BUILD) && defined(ARM9)
	if (LIKELY(Sync == NULL)) {
		if (sp == 0xFF) sp = 0x00;
		else sp++;
		return cached_ram_ptr[(uint16_t)(0x0100u + sp)];
	}
#endif
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
		/* ARM asm dispatch loop disabled: the C++ decode cache avoids slow
		   EWRAM ROM reads on cache hits, making it faster than the asm loop
		   which reads ROM for every opcode/operand fetch. */
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
#if defined(NDS_BUILD) && defined(ARM9)
		// Try decode cache first: if the entry for this PC is valid,
		// use the cached opcode byte and skip the ROM read entirely.
		if (LIKELY(Sync == NULL)) {
			const uint16_t prefetchPc = pc;
			const NDSRomDecodeEntry& prefetch = g_nds_rom_decode[prefetchPc & NDS_DECODE_CACHE_MASK];
			const uint32_t expected_tag = (cached_rom_decode_epoch << 16) | (prefetchPc & ~NDS_DECODE_CACHE_MASK);
			if (LIKELY(prefetch.tag == expected_tag)) {
				opcode = prefetch.opcode;
				pc++;
			} else {
				opcode = FetchByte();
			}
		} else
#endif
		{
			if(Sync == NULL) {
				opcode = FetchByte();
			} else {
				opcode = Sync(pc++);
			}
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
			const int8_t off = (int8_t)FetchByte();
			if (!IF_ZERO()) {
				const uint16_t oldPc = pc;
				pc = (uint16_t)(pc + off);
				elapsedCycles = (uint8_t)(3 + (((oldPc ^ pc) & 0xFF00) ? 1 : 0));
			} else {
				elapsedCycles = 2;
			}
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
			{ // scope for handledHot (threaded dispatch gotos skip this)
			bool handledHot = false;
			if (LIKELY(Sync == NULL)) {
				if (LIKELY(opcode == 0xAD)) { // LDA ABS - fully inlined
					const uint16_t opPc = (uint16_t)(pc - 1);
					NDSRomDecodeEntry& entry = g_nds_rom_decode[opPc & NDS_DECODE_CACHE_MASK];
					const uint32_t epoch = cached_rom_decode_epoch;
					const uint32_t expected_tag = (epoch << 16) | (opPc & ~NDS_DECODE_CACHE_MASK);
					if (UNLIKELY(entry.tag != expected_tag)) {
						entry.tag = expected_tag;
						if (!NDSMainReadFast(opPc, entry.opcode)) entry.opcode = 0xFF;
						if (!NDSMainReadFast((uint16_t)(opPc + 1), entry.op1)) entry.op1 = 0;
						if (!NDSMainReadFast((uint16_t)(opPc + 2), entry.op2)) entry.op2 = 0;
						entry.abs = (uint16_t)(entry.op1 | (entry.op2 << 8));
						entry.abs_read_mode = NDSClassifyAbsReadMode(entry.abs);
						entry.rel = (int16_t)(int8_t)entry.op1;
						const uint16_t rel_base = (uint16_t)(opPc + 2);
						entry.rel_target = (uint16_t)(rel_base + entry.rel);
						entry.rel_taken_cycles = (uint8_t)(3 + (((rel_base ^ entry.rel_target) & 0xFF00) ? 1 : 0));
						switch (entry.abs_read_mode) {
							case NDS_ABS_READ_ROM_LO:
								entry.abs_ptr = &cached_rom_lo_ptr[entry.abs & 0x3FFF];
								break;
							case NDS_ABS_READ_ROM_HI:
								entry.abs_ptr = &cached_rom_hi_ptr[entry.abs & 0x3FFF];
								break;
							case NDS_ABS_READ_ROM_LINEAR:
								entry.abs_ptr = &cached_rom_lo_ptr[entry.abs & cached_rom_linear_mask];
								break;
							case NDS_ABS_READ_VIA:
								entry.abs_ptr = &system_state.VIA_regs[entry.abs & 0xF];
								break;
							default:
								entry.abs_ptr = nullptr;
								break;
						}
					}
					const uint16_t addr = entry.abs;
					const uint8_t* const absPtr = entry.abs_ptr;
					uint8_t m;
					if (LIKELY(absPtr != nullptr)) {
						m = *absPtr;
					} else {
						switch (entry.abs_read_mode) {
							case NDS_ABS_READ_RAM: m = cached_ram_ptr[addr]; break;
							case NDS_ABS_READ_AUDIO: m = GT_AudioRamRead(addr); break;
							case NDS_ABS_READ_JOY: m = GT_JoystickReadFast((uint8_t)addr); break;
							case NDS_ABS_READ_OPEN_BUS: m = open_bus(); break;
							default: m = ReadBus(addr); break;
						}
					}
					pc = (uint16_t)(pc + 2);
					A = m;
					SetNZFast(A);
					elapsedCycles = 4;
					handledHot = true;

						} else if (LIKELY(opcode == 0xD0)) { // BNE REL - fully inlined
					const uint16_t opPc = (uint16_t)(pc - 1);
					NDSRomDecodeEntry& entry = g_nds_rom_decode[opPc & NDS_DECODE_CACHE_MASK];
					const uint32_t epoch = cached_rom_decode_epoch;
					const uint32_t expected_tag = (epoch << 16) | (opPc & ~NDS_DECODE_CACHE_MASK);
					if (UNLIKELY(entry.tag != expected_tag)) {
						entry.tag = expected_tag;
						if (!NDSMainReadFast(opPc, entry.opcode)) entry.opcode = 0xFF;
						if (!NDSMainReadFast((uint16_t)(opPc + 1), entry.op1)) entry.op1 = 0;
						if (!NDSMainReadFast((uint16_t)(opPc + 2), entry.op2)) entry.op2 = 0;
						entry.abs = (uint16_t)(entry.op1 | (entry.op2 << 8));
						entry.abs_read_mode = NDSClassifyAbsReadMode(entry.abs);
						entry.rel = (int16_t)(int8_t)entry.op1;
						const uint16_t rel_base = (uint16_t)(opPc + 2);
						entry.rel_target = (uint16_t)(rel_base + entry.rel);
						entry.rel_taken_cycles = (uint8_t)(3 + (((rel_base ^ entry.rel_target) & 0xFF00) ? 1 : 0));
						switch (entry.abs_read_mode) {
							case NDS_ABS_READ_ROM_LO:
								entry.abs_ptr = &cached_rom_lo_ptr[entry.abs & 0x3FFF];
								break;
							case NDS_ABS_READ_ROM_HI:
								entry.abs_ptr = &cached_rom_hi_ptr[entry.abs & 0x3FFF];
								break;
							case NDS_ABS_READ_ROM_LINEAR:
								entry.abs_ptr = &cached_rom_lo_ptr[entry.abs & cached_rom_linear_mask];
								break;
							case NDS_ABS_READ_VIA:
								entry.abs_ptr = &system_state.VIA_regs[entry.abs & 0xF];
								break;
							default:
								entry.abs_ptr = nullptr;
								break;
						}
					}
					pc = (uint16_t)(opPc + 2);
					if ((status & ZERO) == 0) {
						pc = entry.rel_target;
						elapsedCycles = entry.rel_taken_cycles;
					} else {
						elapsedCycles = 2;
					}
					handledHot = true;
				} else if (LIKELY(opcode == 0xA5)) { // LDA ZER - fully inlined
					const uint16_t opPc = (uint16_t)(pc - 1);
					NDSRomDecodeEntry& entry = g_nds_rom_decode[opPc & NDS_DECODE_CACHE_MASK];
					const uint32_t epoch = cached_rom_decode_epoch;
					const uint32_t expected_tag = (epoch << 16) | (opPc & ~NDS_DECODE_CACHE_MASK);
					if (UNLIKELY(entry.tag != expected_tag)) {
						entry.tag = expected_tag;
						if (!NDSMainReadFast(opPc, entry.opcode)) entry.opcode = 0xFF;
						if (!NDSMainReadFast((uint16_t)(opPc + 1), entry.op1)) entry.op1 = 0;
						entry.op2 = 0;
						entry.abs = entry.op1;
						entry.abs_read_mode = NDS_ABS_READ_RAM; // ZER always reads from RAM
						entry.abs_ptr = nullptr;
						entry.rel = (int16_t)(int8_t)entry.op1;
						const uint16_t rel_base = (uint16_t)(opPc + 2);
						entry.rel_target = (uint16_t)(rel_base + entry.rel);
						entry.rel_taken_cycles = (uint8_t)(3 + (((rel_base ^ entry.rel_target) & 0xFF00) ? 1 : 0));
					}
					A = cached_ram_ptr[entry.op1];
					SetNZFast(A);
					pc = (uint16_t)(pc + 1);
					elapsedCycles = 3;
					handledHot = true;
				} else if (LIKELY(opcode == 0x8D)) { // STA ABS - fully inlined
					const uint16_t opPc = (uint16_t)(pc - 1);
					NDSRomDecodeEntry& entry = g_nds_rom_decode[opPc & NDS_DECODE_CACHE_MASK];
					const uint32_t epoch = cached_rom_decode_epoch;
					const uint32_t expected_tag = (epoch << 16) | (opPc & ~NDS_DECODE_CACHE_MASK);
					if (UNLIKELY(entry.tag != expected_tag)) {
						entry.tag = expected_tag;
						if (!NDSMainReadFast(opPc, entry.opcode)) entry.opcode = 0xFF;
						if (!NDSMainReadFast((uint16_t)(opPc + 1), entry.op1)) entry.op1 = 0;
						if (!NDSMainReadFast((uint16_t)(opPc + 2), entry.op2)) entry.op2 = 0;
						entry.abs = (uint16_t)(entry.op1 | (entry.op2 << 8));
						entry.abs_read_mode = NDSClassifyAbsReadMode(entry.abs);
						entry.rel = (int16_t)(int8_t)entry.op1;
						const uint16_t rel_base = (uint16_t)(opPc + 2);
						entry.rel_target = (uint16_t)(rel_base + entry.rel);
						entry.rel_taken_cycles = (uint8_t)(3 + (((rel_base ^ entry.rel_target) & 0xFF00) ? 1 : 0));
						switch (entry.abs_read_mode) {
							case NDS_ABS_READ_ROM_LO:
								entry.abs_ptr = &cached_rom_lo_ptr[entry.abs & 0x3FFF];
								break;
							case NDS_ABS_READ_ROM_HI:
								entry.abs_ptr = &cached_rom_hi_ptr[entry.abs & 0x3FFF];
								break;
							case NDS_ABS_READ_ROM_LINEAR:
								entry.abs_ptr = &cached_rom_lo_ptr[entry.abs & cached_rom_linear_mask];
								break;
							case NDS_ABS_READ_VIA:
								entry.abs_ptr = &system_state.VIA_regs[entry.abs & 0xF];
								break;
							default:
								entry.abs_ptr = nullptr;
								break;
						}
					}
					const uint16_t addr = entry.abs;
					if (LIKELY(addr < 0x2000)) {
						cached_ram_init_ptr[addr] = true;
						cached_ram_ptr[addr] = A;
					} else if (addr & 0x4000) {
						FlushRunCycles();
						VDMA_Write(addr, A);
					} else if ((addr >= 0x3000) && (addr <= 0x3FFF)) {
						GT_AudioRamWrite(addr, A);
					} else if ((addr >= 0x2800) && (addr <= 0x2FFF)) {
						const uint8_t viaReg = (uint8_t)(addr & 0xF);
						if ((loadedRomType == RomType::FLASH2M) && (viaReg == 0x1)) {
							UpdateFlashShiftRegister(A);
						}
						system_state.VIA_regs[viaReg] = A;
					} else {
						// Keep full bus semantics for control I/O (e.g. $2007 DMA control)
						// and any non-fast-mapped ranges.
						WriteBus(addr, A);
					}
					pc = (uint16_t)(pc + 2);
					elapsedCycles = 4;
					handledHot = true;
				} else if (opcode == 0xA9) { // LDA IMM - fully inlined
					const uint16_t opPc = (uint16_t)(pc - 1);
					NDSRomDecodeEntry& entry = g_nds_rom_decode[opPc & NDS_DECODE_CACHE_MASK];
					const uint32_t epoch = cached_rom_decode_epoch;
					const uint32_t expected_tag = (epoch << 16) | (opPc & ~NDS_DECODE_CACHE_MASK);
					if (UNLIKELY(entry.tag != expected_tag)) {
						entry.tag = expected_tag;
						if (!NDSMainReadFast(opPc, entry.opcode)) entry.opcode = 0xFF;
						if (!NDSMainReadFast((uint16_t)(opPc + 1), entry.op1)) entry.op1 = 0;
						entry.op2 = 0;
						entry.abs = entry.op1;
						entry.abs_read_mode = NDS_ABS_READ_RAM;
						entry.abs_ptr = nullptr;
						entry.rel = (int16_t)(int8_t)entry.op1;
						const uint16_t rel_base = (uint16_t)(opPc + 2);
						entry.rel_target = (uint16_t)(rel_base + entry.rel);
						entry.rel_taken_cycles = (uint8_t)(3 + (((rel_base ^ entry.rel_target) & 0xFF00) ? 1 : 0));
					}
					A = entry.op1;
					SetNZFast(A);
					pc = (uint16_t)(pc + 1);
					elapsedCycles = 2;
					handledHot = true;
				} else if (opcode == 0x85) { // STA ZER - fully inlined
					const uint16_t opPc = (uint16_t)(pc - 1);
					NDSRomDecodeEntry& entry = g_nds_rom_decode[opPc & NDS_DECODE_CACHE_MASK];
					const uint32_t epoch = cached_rom_decode_epoch;
					const uint32_t expected_tag = (epoch << 16) | (opPc & ~NDS_DECODE_CACHE_MASK);
					if (UNLIKELY(entry.tag != expected_tag)) {
						entry.tag = expected_tag;
						if (!NDSMainReadFast(opPc, entry.opcode)) entry.opcode = 0xFF;
						if (!NDSMainReadFast((uint16_t)(opPc + 1), entry.op1)) entry.op1 = 0;
						entry.op2 = 0;
						entry.abs = entry.op1;
						entry.abs_read_mode = NDS_ABS_READ_RAM;
						entry.abs_ptr = nullptr;
						entry.rel = 0;
						entry.rel_target = 0;
						entry.rel_taken_cycles = 0;
					}
					cached_ram_init_ptr[entry.op1] = true;
					cached_ram_ptr[entry.op1] = A;
					pc = (uint16_t)(pc + 1);
					elapsedCycles = 3;
					handledHot = true;
				} else if (opcode == 0xF0) { // BEQ REL - fully inlined
					const uint16_t opPc = (uint16_t)(pc - 1);
					NDSRomDecodeEntry& entry = g_nds_rom_decode[opPc & NDS_DECODE_CACHE_MASK];
					const uint32_t epoch = cached_rom_decode_epoch;
					const uint32_t expected_tag = (epoch << 16) | (opPc & ~NDS_DECODE_CACHE_MASK);
					if (UNLIKELY(entry.tag != expected_tag)) {
						entry.tag = expected_tag;
						if (!NDSMainReadFast(opPc, entry.opcode)) entry.opcode = 0xFF;
						if (!NDSMainReadFast((uint16_t)(opPc + 1), entry.op1)) entry.op1 = 0;
						if (!NDSMainReadFast((uint16_t)(opPc + 2), entry.op2)) entry.op2 = 0;
						entry.abs = (uint16_t)(entry.op1 | (entry.op2 << 8));
						entry.abs_read_mode = NDSClassifyAbsReadMode(entry.abs);
						entry.rel = (int16_t)(int8_t)entry.op1;
						const uint16_t rel_base = (uint16_t)(opPc + 2);
						entry.rel_target = (uint16_t)(rel_base + entry.rel);
						entry.rel_taken_cycles = (uint8_t)(3 + (((rel_base ^ entry.rel_target) & 0xFF00) ? 1 : 0));
						switch (entry.abs_read_mode) {
							case NDS_ABS_READ_ROM_LO:
								entry.abs_ptr = &cached_rom_lo_ptr[entry.abs & 0x3FFF];
								break;
							case NDS_ABS_READ_ROM_HI:
								entry.abs_ptr = &cached_rom_hi_ptr[entry.abs & 0x3FFF];
								break;
							case NDS_ABS_READ_ROM_LINEAR:
								entry.abs_ptr = &cached_rom_lo_ptr[entry.abs & cached_rom_linear_mask];
								break;
							case NDS_ABS_READ_VIA:
								entry.abs_ptr = &system_state.VIA_regs[entry.abs & 0xF];
								break;
							default:
								entry.abs_ptr = nullptr;
								break;
						}
					}
					pc = (uint16_t)(opPc + 2);
					if (status & ZERO) {
						pc = entry.rel_target;
						elapsedCycles = entry.rel_taken_cycles;
					} else {
						elapsedCycles = 2;
					}
					handledHot = true;
				} else if (opcode == 0x4C) { // JMP ABS - fully inlined
					const uint16_t opPc = (uint16_t)(pc - 1);
					NDSRomDecodeEntry& entry = g_nds_rom_decode[opPc & NDS_DECODE_CACHE_MASK];
					const uint32_t epoch = cached_rom_decode_epoch;
					const uint32_t expected_tag = (epoch << 16) | (opPc & ~NDS_DECODE_CACHE_MASK);
					if (UNLIKELY(entry.tag != expected_tag)) {
						entry.tag = expected_tag;
						if (!NDSMainReadFast(opPc, entry.opcode)) entry.opcode = 0xFF;
						if (!NDSMainReadFast((uint16_t)(opPc + 1), entry.op1)) entry.op1 = 0;
						if (!NDSMainReadFast((uint16_t)(opPc + 2), entry.op2)) entry.op2 = 0;
						entry.abs = (uint16_t)(entry.op1 | (entry.op2 << 8));
						entry.abs_read_mode = NDSClassifyAbsReadMode(entry.abs);
						entry.rel = (int16_t)(int8_t)entry.op1;
						const uint16_t rel_base = (uint16_t)(opPc + 2);
						entry.rel_target = (uint16_t)(rel_base + entry.rel);
						entry.rel_taken_cycles = (uint8_t)(3 + (((rel_base ^ entry.rel_target) & 0xFF00) ? 1 : 0));
						switch (entry.abs_read_mode) {
							case NDS_ABS_READ_ROM_LO:
								entry.abs_ptr = &cached_rom_lo_ptr[entry.abs & 0x3FFF];
								break;
							case NDS_ABS_READ_ROM_HI:
								entry.abs_ptr = &cached_rom_hi_ptr[entry.abs & 0x3FFF];
								break;
							case NDS_ABS_READ_ROM_LINEAR:
								entry.abs_ptr = &cached_rom_lo_ptr[entry.abs & cached_rom_linear_mask];
								break;
							case NDS_ABS_READ_VIA:
								entry.abs_ptr = &system_state.VIA_regs[entry.abs & 0xF];
								break;
							default:
								entry.abs_ptr = nullptr;
								break;
						}
					}
					pc = entry.abs;
					elapsedCycles = 3;
					handledHot = true;
				} else if (opcode == 0x20) { // JSR ABS - fully inlined
					const uint16_t opPc = (uint16_t)(pc - 1);
					NDSRomDecodeEntry& entry = g_nds_rom_decode[opPc & NDS_DECODE_CACHE_MASK];
					const uint32_t epoch = cached_rom_decode_epoch;
					const uint32_t expected_tag = (epoch << 16) | (opPc & ~NDS_DECODE_CACHE_MASK);
					if (UNLIKELY(entry.tag != expected_tag)) {
						entry.tag = expected_tag;
						if (!NDSMainReadFast(opPc, entry.opcode)) entry.opcode = 0xFF;
						if (!NDSMainReadFast((uint16_t)(opPc + 1), entry.op1)) entry.op1 = 0;
						if (!NDSMainReadFast((uint16_t)(opPc + 2), entry.op2)) entry.op2 = 0;
						entry.abs = (uint16_t)(entry.op1 | (entry.op2 << 8));
						entry.abs_read_mode = NDSClassifyAbsReadMode(entry.abs);
						entry.rel = (int16_t)(int8_t)entry.op1;
						const uint16_t rel_base = (uint16_t)(opPc + 2);
						entry.rel_target = (uint16_t)(rel_base + entry.rel);
						entry.rel_taken_cycles = (uint8_t)(3 + (((rel_base ^ entry.rel_target) & 0xFF00) ? 1 : 0));
						switch (entry.abs_read_mode) {
							case NDS_ABS_READ_ROM_LO:
								entry.abs_ptr = &cached_rom_lo_ptr[entry.abs & 0x3FFF];
								break;
							case NDS_ABS_READ_ROM_HI:
								entry.abs_ptr = &cached_rom_hi_ptr[entry.abs & 0x3FFF];
								break;
							case NDS_ABS_READ_ROM_LINEAR:
								entry.abs_ptr = &cached_rom_lo_ptr[entry.abs & cached_rom_linear_mask];
								break;
							case NDS_ABS_READ_VIA:
								entry.abs_ptr = &system_state.VIA_regs[entry.abs & 0xF];
								break;
							default:
								entry.abs_ptr = nullptr;
								break;
						}
					}
					pc = (uint16_t)(opPc + 3);
					const uint16_t ret = (uint16_t)(pc - 1);
					uint16_t saddr = (uint16_t)(0x0100u + sp);
					cached_ram_init_ptr[saddr] = true;
					cached_ram_ptr[saddr] = (uint8_t)(ret >> 8);
					sp = (sp == 0x00) ? 0xFF : (uint8_t)(sp - 1);
					saddr = (uint16_t)(0x0100u + sp);
					cached_ram_init_ptr[saddr] = true;
					cached_ram_ptr[saddr] = (uint8_t)(ret & 0xFF);
					sp = (sp == 0x00) ? 0xFF : (uint8_t)(sp - 1);
					pc = entry.abs;
					elapsedCycles = 6;
					handledHot = true;
				} else if (opcode == 0x60) { // RTS - fully inlined
					sp = (sp == 0xFF) ? 0x00 : (uint8_t)(sp + 1);
					const uint16_t lo = cached_ram_ptr[(uint16_t)(0x0100u + sp)];
					sp = (sp == 0xFF) ? 0x00 : (uint8_t)(sp + 1);
					const uint16_t hi = cached_ram_ptr[(uint16_t)(0x0100u + sp)];
					pc = (uint16_t)(((hi << 8) | lo) + 1);
					elapsedCycles = 6;
					handledHot = true;
				}
			}
			if (!handledHot) switch(opcode) {
				case 0x69: { // ADC IMM
					uint8_t imm;
					const uint16_t opPc = (uint16_t)(pc - 1);
					if (LIKELY(Sync == NULL)) {
						const NDSRomDecodeEntry& dec = NDSGetRomDecode(opPc);
						imm = dec.op1;
						pc = (uint16_t)(pc + 1);
					} else {
						imm = FetchByte();
					}
					ADCFast(imm);
					elapsedCycles = 2;
					break;
				}
				case 0x65: { // ADC ZER
					ADCFast(cached_ram_ptr[FetchByte()]);
					elapsedCycles = 3;
					break;
				}
				case 0x6D: { // ADC ABS
					const uint16_t lo = FetchByte();
					const uint16_t hi = FetchByte();
					const uint16_t addr = (uint16_t)(lo | (hi << 8));
					uint8_t m;
					if (LIKELY(NDSMainReadFast(addr, m))) {
						ADCFast(m);
					} else {
						ADCFast(ReadBus(addr));
					}
					elapsedCycles = 4;
					break;
				}
				case 0x72: { // ADC ZPI
					const uint8_t zp = FetchByte();
					const uint16_t addr = (uint16_t)(
						cached_ram_ptr[zp] |
						(cached_ram_ptr[(uint8_t)(zp + 1)] << 8));
					uint8_t m;
					if (LIKELY(NDSMainReadFast(addr, m))) {
						ADCFast(m);
					} else {
						ADCFast(ReadBus(addr));
					}
					elapsedCycles = 6;
					break;
				}
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
					A = cached_ram_ptr[addr];
					SetNZFast(A);
					elapsedCycles = 3;
					break;
				}
				case 0xA9: { // LDA IMM
					uint8_t imm;
					const uint16_t opPc = (uint16_t)(pc - 1);
					if (LIKELY(Sync == NULL)) {
						const NDSRomDecodeEntry& dec = NDSGetRomDecode(opPc);
						imm = dec.op1;
						pc = (uint16_t)(pc + 1);
					} else {
						imm = FetchByte();
					}
					A = imm;
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
					cached_ram_init_ptr[addr] = true;
					cached_ram_ptr[addr] = A;
					elapsedCycles = 3;
					break;
				}
				case 0xB2: { // LDA ZPI
					const uint8_t zp = FetchByte();
					const uint16_t addr = (uint16_t)(
						cached_ram_ptr[zp] |
						(cached_ram_ptr[(uint8_t)(zp + 1)] << 8));
					uint8_t m;
					if (LIKELY(NDSMainReadFast(addr, m))) {
						A = m;
					} else {
						A = ReadBus(addr);
					}
					SetNZFast(A);
					elapsedCycles = 5;
					break;
				}
				case 0x49: { // EOR IMM
					uint8_t imm;
					const uint16_t opPc = (uint16_t)(pc - 1);
					if (LIKELY(Sync == NULL)) {
						const NDSRomDecodeEntry& dec = NDSGetRomDecode(opPc);
						imm = dec.op1;
						pc = (uint16_t)(pc + 1);
					} else {
						imm = FetchByte();
					}
					A ^= imm;
					SetNZFast(A);
					elapsedCycles = 2;
					break;
				}
				case 0xE9: { // SBC IMM
					uint8_t imm;
					const uint16_t opPc = (uint16_t)(pc - 1);
					if (LIKELY(Sync == NULL)) {
						const NDSRomDecodeEntry& dec = NDSGetRomDecode(opPc);
						imm = dec.op1;
						pc = (uint16_t)(pc + 1);
					} else {
						imm = FetchByte();
					}
					SBCFast(imm);
					elapsedCycles = 2;
					break;
				}
				case 0xE5: { // SBC ZER
					SBCFast(cached_ram_ptr[FetchByte()]);
					elapsedCycles = 3;
					break;
				}
				case 0xED: { // SBC ABS
					const uint16_t lo = FetchByte();
					const uint16_t hi = FetchByte();
					const uint16_t addr = (uint16_t)(lo | (hi << 8));
					uint8_t m;
					if (LIKELY(NDSMainReadFast(addr, m))) {
						SBCFast(m);
					} else {
						SBCFast(ReadBus(addr));
					}
					elapsedCycles = 4;
					break;
				}
				case 0xF2: { // SBC ZPI
					const uint8_t zp = FetchByte();
					const uint16_t addr = (uint16_t)(
						cached_ram_ptr[zp] |
						(cached_ram_ptr[(uint8_t)(zp + 1)] << 8));
					uint8_t m;
					if (LIKELY(NDSMainReadFast(addr, m))) {
						SBCFast(m);
					} else {
						SBCFast(ReadBus(addr));
					}
					elapsedCycles = 5;
					break;
				}
				case 0xB0: { // BCS REL
					int8_t off8;
					const uint16_t opPc = (uint16_t)(pc - 1);
					if (LIKELY(Sync == NULL)) {
						const NDSRomDecodeEntry& dec = NDSGetRomDecode(opPc);
						off8 = (int8_t)dec.op1;
						pc = (uint16_t)(pc + 1);
					} else {
						off8 = (int8_t)FetchByte();
					}
					uint16_t off = (uint16_t)off8;
					if (off8 < 0) off |= 0xFF00;
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
					int8_t off8;
					const uint16_t opPc = (uint16_t)(pc - 1);
					if (LIKELY(Sync == NULL)) {
						const NDSRomDecodeEntry& dec = NDSGetRomDecode(opPc);
						off8 = (int8_t)dec.op1;
						pc = (uint16_t)(pc + 1);
					} else {
						off8 = (int8_t)FetchByte();
					}
					uint16_t off = (uint16_t)off8;
					if (off8 < 0) off |= 0xFF00;
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
					int8_t off8;
					const uint16_t opPc = (uint16_t)(pc - 1);
					if (LIKELY(Sync == NULL)) {
						const NDSRomDecodeEntry& dec = NDSGetRomDecode(opPc);
						off8 = (int8_t)dec.op1;
						pc = (uint16_t)(pc + 1);
					} else {
						off8 = (int8_t)FetchByte();
					}
					uint16_t off = (uint16_t)off8;
					if (off8 < 0) off |= 0xFF00;
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
					int8_t off8;
					const uint16_t opPc = (uint16_t)(pc - 1);
					if (LIKELY(Sync == NULL)) {
						const NDSRomDecodeEntry& dec = NDSGetRomDecode(opPc);
						off8 = (int8_t)dec.op1;
						pc = (uint16_t)(pc + 1);
					} else {
						off8 = (int8_t)FetchByte();
					}
					uint16_t off = (uint16_t)off8;
					if (off8 < 0) off |= 0xFF00;
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
					int8_t off8;
					const uint16_t opPc = (uint16_t)(pc - 1);
					if (LIKELY(Sync == NULL)) {
						const NDSRomDecodeEntry& dec = NDSGetRomDecode(opPc);
						off8 = (int8_t)dec.op1;
						pc = (uint16_t)(pc + 1);
					} else {
						off8 = (int8_t)FetchByte();
					}
					uint16_t off = (uint16_t)off8;
					if (off8 < 0) off |= 0xFF00;
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
					int8_t off8;
					const uint16_t opPc = (uint16_t)(pc - 1);
					if (LIKELY(Sync == NULL)) {
						const NDSRomDecodeEntry& dec = NDSGetRomDecode(opPc);
						off8 = (int8_t)dec.op1;
						pc = (uint16_t)(pc + 1);
					} else {
						off8 = (int8_t)FetchByte();
					}
					uint16_t off = (uint16_t)off8;
					if (off8 < 0) off |= 0xFF00;
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
					int8_t off8;
					const uint16_t opPc = (uint16_t)(pc - 1);
					if (LIKELY(Sync == NULL)) {
						const NDSRomDecodeEntry& dec = NDSGetRomDecode(opPc);
						off8 = (int8_t)dec.op1;
						pc = (uint16_t)(pc + 1);
					} else {
						off8 = (int8_t)FetchByte();
					}
					uint16_t off = (uint16_t)off8;
					if (off8 < 0) off |= 0xFF00;
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
					int8_t off8;
					const uint16_t opPc = (uint16_t)(pc - 1);
					if (LIKELY(Sync == NULL)) {
						const NDSRomDecodeEntry& dec = NDSGetRomDecode(opPc);
						off8 = (int8_t)dec.op1;
						pc = (uint16_t)(pc + 1);
					} else {
						off8 = (int8_t)FetchByte();
					}
					uint16_t off = (uint16_t)off8;
					if (off8 < 0) off |= 0xFF00;
					const uint16_t target = pc + (int16_t)off;
					if (!addressesSamePage(pc, target)) opExtraCycles++;
					pc = target;
					opExtraCycles++;
					elapsedCycles = 3;
					break;
				}
				case 0xD0: { // BNE REL
					const int16_t rel = (int16_t)(int8_t)FetchByte();
					if ((status & ZERO) == 0) {
						const uint16_t oldPc = pc;
						pc = (uint16_t)(pc + rel);
						elapsedCycles = (uint8_t)(3 + (((oldPc ^ pc) & 0xFF00) ? 1 : 0));
					} else {
						elapsedCycles = 2;
					}
					break;
				}
				case 0x20: { // JSR ABS
					uint16_t target;
					const uint16_t opPc = (uint16_t)(pc - 1);
					if (LIKELY(Sync == NULL)) {
						const NDSRomDecodeEntry& dec = NDSGetRomDecode(opPc);
						target = dec.abs;
						pc = (uint16_t)(pc + 2);
					} else {
						const uint16_t lo = FetchByte();
						const uint16_t hi = FetchByte();
						target = (uint16_t)(lo | (hi << 8));
					}
					const uint16_t ret = (uint16_t)(pc - 1);
#if defined(NDS_BUILD) && defined(ARM9)
					if (LIKELY(Sync == NULL)) {
						uint16_t saddr = (uint16_t)(0x0100u + sp);
						cached_ram_init_ptr[saddr] = true;
						cached_ram_ptr[saddr] = (uint8_t)(ret >> 8);
						sp = (sp == 0x00) ? 0xFF : (uint8_t)(sp - 1);
						saddr = (uint16_t)(0x0100u + sp);
						cached_ram_init_ptr[saddr] = true;
						cached_ram_ptr[saddr] = (uint8_t)(ret & 0xFF);
						sp = (sp == 0x00) ? 0xFF : (uint8_t)(sp - 1);
					} else
#endif
					{
						StackPush((uint8_t)(ret >> 8));
						StackPush((uint8_t)(ret & 0xFF));
					}
					pc = target;
					elapsedCycles = 6;
					break;
				}
				case 0x4C: { // JMP ABS
					const uint16_t opPc = (uint16_t)(pc - 1);
					if (LIKELY(Sync == NULL)) {
						const NDSRomDecodeEntry& dec = NDSGetRomDecode(opPc);
						pc = dec.abs;
					} else {
						const uint16_t lo = FetchByte();
						const uint16_t hi = FetchByte();
						pc = (uint16_t)(lo | (hi << 8));
					}
					elapsedCycles = 3;
					break;
				}
				case 0x60: { // RTS
					uint16_t lo;
					uint16_t hi;
#if defined(NDS_BUILD) && defined(ARM9)
					if (LIKELY(Sync == NULL)) {
						sp = (sp == 0xFF) ? 0x00 : (uint8_t)(sp + 1);
						lo = cached_ram_ptr[(uint16_t)(0x0100u + sp)];
						sp = (sp == 0xFF) ? 0x00 : (uint8_t)(sp + 1);
						hi = cached_ram_ptr[(uint16_t)(0x0100u + sp)];
					} else
#endif
					{
						lo = StackPop();
						hi = StackPop();
					}
					pc = (uint16_t)(((hi << 8) | lo) + 1);
					elapsedCycles = 6;
					break;
				}
				case 0xEE: { // INC ABS
					const uint16_t lo = FetchByte();
					const uint16_t hi = FetchByte();
					const uint16_t addr = (uint16_t)(lo | (hi << 8));
					if (LIKELY(addr < 0x2000)) {
						uint8_t m = (uint8_t)(cached_ram_ptr[addr] + 1);
						cached_ram_init_ptr[addr] = true;
						cached_ram_ptr[addr] = m;
						SetNZFast(m);
					} else {
						uint8_t m;
						if (LIKELY(NDSMainReadFast(addr, m))) {
							m = (uint8_t)(m + 1);
							SetNZFast(m);
							WriteBus(addr, m);
						} else {
							m = (uint8_t)(ReadBus(addr) + 1);
							SetNZFast(m);
							WriteBus(addr, m);
						}
					}
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
		} // end handledHot scope
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
			// Use odd-stride periodic sampling to avoid phase-lock aliasing
			// on tiny opcode loops (e.g. alternating AD/D0).
			if (opcode_profile_decim >= NDS_OPCODE_PROFILE_STRIDE) {
				opcode_profile_decim = 0;
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
	ADCFast(ReadBus(src));
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
	SBCFast(ReadBus(src));
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
	auto val = ReadBus(FetchByte());
	uint16_t offset = (uint16_t)FetchByte();

	Op_BBRx(0x01, val, offset);
}

void mos6502::Op_BBR1(uint16_t src)
{
	auto val = ReadBus(FetchByte());
	uint16_t offset = (uint16_t)FetchByte();

	Op_BBRx(0x02, val, offset);
}

void mos6502::Op_BBR2(uint16_t src)
{
	auto val = ReadBus(FetchByte());
	uint16_t offset = (uint16_t)FetchByte();

	Op_BBRx(0x04, val, offset);
}

void mos6502::Op_BBR3(uint16_t src)
{
	auto val = ReadBus(FetchByte());
	uint16_t offset = (uint16_t)FetchByte();

	Op_BBRx(0x08, val, offset);
}

void mos6502::Op_BBR4(uint16_t src)
{
	auto val = ReadBus(FetchByte());
	uint16_t offset = (uint16_t)FetchByte();

	Op_BBRx(0x10, val, offset);
}

void mos6502::Op_BBR5(uint16_t src)
{
	auto val = ReadBus(FetchByte());
	uint16_t offset = (uint16_t)FetchByte();

	Op_BBRx(0x20, val, offset);
}

void mos6502::Op_BBR6(uint16_t src)
{
	auto val = ReadBus(FetchByte());
	uint16_t offset = (uint16_t)FetchByte();

	Op_BBRx(0x40, val, offset);
}

void mos6502::Op_BBR7(uint16_t src)
{
	auto val = ReadBus(FetchByte());
	uint16_t offset = (uint16_t)FetchByte();

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
	auto val = ReadBus(FetchByte());
	uint16_t offset = (uint16_t)FetchByte();

	Op_BBSx(0x01, val, offset);
}

void mos6502::Op_BBS1(uint16_t src)
{
	auto val = ReadBus(FetchByte());
	uint16_t offset = (uint16_t)FetchByte();

	Op_BBSx(0x02, val, offset);
}

void mos6502::Op_BBS2(uint16_t src)
{
	auto val = ReadBus(FetchByte());
	uint16_t offset = (uint16_t)FetchByte();

	Op_BBSx(0x04, val, offset);
}

void mos6502::Op_BBS3(uint16_t src)
{
	auto val = ReadBus(FetchByte());
	uint16_t offset = (uint16_t)FetchByte();

	Op_BBSx(0x08, val, offset);
}

void mos6502::Op_BBS4(uint16_t src)
{
	auto val = ReadBus(FetchByte());
	uint16_t offset = (uint16_t)FetchByte();

	Op_BBSx(0x10, val, offset);
}

void mos6502::Op_BBS5(uint16_t src)
{
	auto val = ReadBus(FetchByte());
	uint16_t offset = (uint16_t)FetchByte();

	Op_BBSx(0x20, val, offset);
}

void mos6502::Op_BBS6(uint16_t src)
{
	auto val = ReadBus(FetchByte());
	uint16_t offset = (uint16_t)FetchByte();

	Op_BBSx(0x40, val, offset);
}

void mos6502::Op_BBS7(uint16_t src)
{
	auto val = ReadBus(FetchByte());
	uint16_t offset = (uint16_t)FetchByte();

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
