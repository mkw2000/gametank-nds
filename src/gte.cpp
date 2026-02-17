#include "SDL_inc.h"
#ifdef NDS_BUILD
#include <sys/iosupport.h>
#include <sys/reent.h>
#include <stdlib.h>
#include <errno.h>
#include <fat.h>
#include <nds/disc_io.h>
#include <nds/arm9/console.h>
// _open_r override removed — using standard libfat implementation
#endif

#ifdef NDS_BUILD
void WaitForA(const char* msg) {
	printf("%s\nPress A...\n", msg);
	while(1) { swiWaitForVBlank(); scanKeys(); if (keysDown() & KEY_A) break; }
}

void DebugFilesystem() {
	printf("Debug Filesystem:\n");
	
	// List all devices
	for (int i = 0; i < 32; ++i) { 
		if (devoptab_list[i]) {
			printf("  dev[%d]:name='%s' open=%p\n", i, devoptab_list[i]->name, devoptab_list[i]->open_r);
		} else {
			// printf("  dev[%d]: NULL\n", i); // Optional
		}
		
		if ((i + 1) % 8 == 0) {
			WaitForA("-- More --");
		}
	}

	int dev = FindDevice("fat:");
	printf("FindDevice('fat:'): %d\n", dev);
	if (dev == -1) {
		dev = FindDevice("sd:");
		printf("FindDevice('sd:'): %d\n", dev);
	}
	
	printf("\n");
	WaitForA("Continue...");
}
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmath>
#include <time.h>
#include <fstream>
#include <cstring>
#ifndef NDS_BUILD
#include <filesystem>
#endif
#include <vector>
#ifndef NDS_BUILD
#include <thread>
#endif
#include <algorithm>
#ifdef NDS_BUILD
// NDS build — no ImGui, tinyfiledialogs, whereami, or emscripten
#elif defined(WASM_BUILD)
#include "emscripten.h"
#include <emscripten/html5.h>
#else
#include "tinyfd/tinyfiledialogs.h"
#endif

#include "joystick_adapter.h"
#include "audio_coprocessor.h"
#include "blitter.h"
#include "palette.h"

#include "timekeeper.h"
#include "system_state.h"
#include "emulator_config.h"

#ifndef NDS_BUILD
#include "game_config.h"
#endif

#include "mos6502/mos6502.h"

#if !defined(NDS_BUILD) && !defined(WASM_BUILD)
#include "devtools/memory_map.h"
#include "devtools/breakpoints.h"
#include "devtools/source_map.h"

#include "ui/ui_utils.h"
#include "devtools/profiler.h"
#include "devtools/disassembler.h"

#include "devtools/profiler_window.h"
#include "devtools/mem_browser_window.h"
#include "devtools/vram_window.h"
#include "devtools/stepping_window.h"
#include "devtools/patching_window.h"
#include "devtools/controller_options_window.h"
#include "imgui.h"
#include "implot.h"
#include "imgui/backends/imgui_impl_sdl2.h"
#include "imgui/backends/imgui_impl_sdlrenderer2.h"
#include "whereami/whereami.h"
#endif

#ifndef WINDOW_TITLE
#define WINDOW_TITLE "GameTank Emulator"
#endif

using namespace std;

const int GT_WIDTH = 128;
const int GT_HEIGHT = 128;

RomType loadedRomType;

mos6502 *cpu_core;
Blitter *blitter;
AudioCoprocessor *soundcard;
JoystickAdapter *joysticks;
SystemState system_state;
CartridgeState cartridge_state;

#ifndef NDS_BUILD
const int SCREEN_WIDTH = 683;	
const int SCREEN_HEIGHT = 512;
#endif
RGB_Color *palette;

std::string currentRomFilePath;
std::string nvramFileFullPath;
std::string flashFileFullPath;

#if !defined(NDS_BUILD) && !defined(WASM_BUILD)
MemoryMap* loadedMemoryMap;
#endif
#ifndef NDS_BUILD
GameConfig* gameconfig;
#endif

bool vsyncProfileArmed = false;
bool vsyncProfileRunning = false;

bool showMenu = false;
bool menuOpening = false;
int resetQueued = 0;
#define MUTE_SOURCE_MANUAL 1
#define MUTE_SOURCE_MENU 2
int muteMask = 0;

void SaveNVRAM() {
	fstream file;
	if(loadedRomType != RomType::FLASH2M_RAM32K) return;
	printf("SAVING %s\n", nvramFileFullPath.c_str());
	file.open(nvramFileFullPath.c_str(), ios_base::out | ios_base::binary | ios_base::trunc);
	file.write((char*) cartridge_state.save_ram, CARTRAMSIZE);
	file.close();
}

void LoadNVRAM() {
	fstream file;
	if(loadedRomType != RomType::FLASH2M_RAM32K) return;
	printf("LOADING %s\n", nvramFileFullPath.c_str());
	file.open(nvramFileFullPath.c_str(), ios_base::in | ios_base::binary);
	file.read((char*) cartridge_state.save_ram, CARTRAMSIZE);
	file.close();
}

#ifndef NDS_BUILD
std::thread savingThread;
#endif

void SaveModifiedFlash() {
	if(EmulatorConfig::noSave) return;
	fstream file_out, file_in;
	uint8_t* rom_cursor = cartridge_state.rom;
	uint8_t buf[256];
	file_in.open(currentRomFilePath, ios_base::in | ios_base::binary);
	file_out.open(flashFileFullPath.c_str(), ios_base::out | ios_base::binary | ios_base::trunc);
	while(file_in) {
		file_in.read((char*) buf, 256);
		size_t bytesRead = file_in.gcount();
		if(bytesRead) {
			for(int i = 0; i < bytesRead; ++i) {
				buf[i] ^= *(rom_cursor++);
			}
			file_out.write((char*) buf, bytesRead);
		}
	}
	file_in.close();
	file_out.close();
#ifdef WASM_BUILD
	EM_ASM(
		FS.syncfs(false, function (err) {
			assert(!err);
			});
	);
#endif
}

fstream orig_rom, xor_file;
void LoadModifiedFlash() {
	uint8_t* rom_cursor = cartridge_state.rom;
	uint8_t buf[256];
	uint8_t bufx[256];
	size_t bytes_read = 0;
	std::cout << "opening " << currentRomFilePath << " and " << flashFileFullPath << "\n";
	orig_rom.open(currentRomFilePath, ios_base::in | ios_base::binary);
	xor_file.open(flashFileFullPath, ios_base::in | ios_base::binary);
	std::cout << "XORing files together... \n";
	while(orig_rom && xor_file) {
		orig_rom.read((char*) buf, 256);
		xor_file.read((char*) bufx, 256); 
		for(int i = 0; i < orig_rom.gcount(); ++i) {
			*(rom_cursor++) = buf[i] ^ bufx[i];
		}
		bytes_read += 256;
	}
	std::cout << bytes_read << " bytes read from xor file\n";
#ifndef WASM_BUILD
	orig_rom.close();
	xor_file.close();
#endif
}

const uint8_t VIA_ORB    = 0x0;
const uint8_t VIA_ORA    = 0x1;
const uint8_t VIA_DDRB   = 0x2;
const uint8_t VIA_DDRA   = 0x3;
const uint8_t VIA_T1CL   = 0x4;
const uint8_t VIA_T1CH   = 0x5;
const uint8_t VIA_T1LL   = 0x6;
const uint8_t VIA_T1LH   = 0x7;
const uint8_t VIA_T2CL   = 0x8;
const uint8_t VIA_T2CH   = 0x9;
const uint8_t VIA_SR     = 0xA;
const uint8_t VIA_ACR    = 0xB;
const uint8_t VIA_PCR    = 0xC;
const uint8_t VIA_IFR    = 0xD;
const uint8_t VIA_IER    = 0xE;
const uint8_t VIA_ORA_NH = 0xF;

//Pins of VIA Port A used for Serial comms (or other misc cartridge use)
const uint8_t VIA_SPI_BIT_CLK  = 0b00000001;
const uint8_t VIA_SPI_BIT_MOSI = 0b00000010;
const uint8_t VIA_SPI_BIT_CS   = 0b00000100;
const uint8_t VIA_SPI_BIT_MISO = 0b10000000;

#define RAM_HIGHBITS_SHIFT 7

// Cached ram_base: updated whenever banking register ($2005) changes
static uint16_t cached_ram_base = 0;

static inline void UpdateBankingCache() {
	cached_ram_base = (system_state.banking & BANK_RAM_MASK) << RAM_HIGHBITS_SHIFT;
}

#define FULL_RAM_ADDRESS(x) (cached_ram_base | (x))

extern unsigned char font_map[];

#if !defined(NDS_BUILD) && !defined(WASM_BUILD)
Timekeeper timekeeper;
Profiler profiler(timekeeper);
#else
Timekeeper timekeeper;
#endif

#ifndef NDS_BUILD
SDL_Surface* gRAM_Surface = NULL;
SDL_Surface* vRAM_Surface = NULL;

SDL_Window* mainWindow = NULL;
SDL_Window* buffers_window = NULL;
Uint32 rmask, gmask, bmask, amask;

#ifndef WASM_BUILD
ImGuiContext* main_imgui_ctx;
ImPlotContext* main_implot_ctx;

std::vector<BaseWindow*> toolWindows;
#endif

SDL_Renderer* mainRenderer = NULL;
SDL_Texture* framebufferTexture = NULL;
#else
// NDS build — use stub surfaces, no windows/renderers
SDL_Surface* gRAM_Surface = NULL;
SDL_Surface* vRAM_Surface = NULL;
Uint32 rmask, gmask, bmask, amask;
#endif

bool isFullScreen = false;

bool profiler_open = false;
bool buffers_open = false;
int profiler_x_axis = 0;

uint8_t open_bus() {
	return rand() % 256;
}

uint8_t VDMA_Read(uint16_t address) {
	blitter->CatchUp();
	if(system_state.dma_control & DMA_COPY_ENABLE_BIT) {
		return open_bus();
	} else {
		uint8_t* bufPtr;
		uint32_t offset = 0;
		if(system_state.dma_control & DMA_CPU_TO_VRAM) {
			bufPtr = system_state.vram;
			if(system_state.banking & BANK_VRAM_MASK) {
				offset = 0x4000;
			}
		} else {
			bufPtr = system_state.gram;
			offset = (((system_state.banking & BANK_GRAM_MASK) << 2) | (blitter->gram_mid_bits)) << 14;
		}
		return bufPtr[(address & 0x3FFF) | offset];
	}
}

void VDMA_Write(uint16_t address, uint8_t value) {
	blitter->CatchUp();
	if(system_state.dma_control & DMA_COPY_ENABLE_BIT) {
		blitter->SetParam(address, value);
	} else {
		uint8_t* bufPtr;
		uint32_t offset = 0;
		SDL_Surface* targetSurface = NULL;
		uint32_t yShift = 0;
		if(system_state.dma_control & DMA_CPU_TO_VRAM) {
			bufPtr = system_state.vram;
			targetSurface = vRAM_Surface;
			if(system_state.banking & BANK_VRAM_MASK) {
				offset = 0x4000;
				yShift = GT_HEIGHT;
			}
		} else {
			bufPtr = system_state.gram;
			targetSurface = gRAM_Surface;
			yShift = (((system_state.banking & BANK_GRAM_MASK) << 2) | (blitter->gram_mid_bits)) * GT_HEIGHT;
			offset = (((system_state.banking & BANK_GRAM_MASK) << 2) | (blitter->gram_mid_bits)) << 14;
		}
		bufPtr[(address & 0x3FFF) | offset] = value;

		if(targetSurface) {
			uint8_t x, y;
			x = address & 127;
			y = (address >> 7) & 127;
			put_pixel32(targetSurface, x, y + yShift, Palette::ConvertColor(targetSurface, value));
		}
	}
}

void UpdateFlashShiftRegister(uint8_t nextVal) {
	//TODO: Care about DDR bits
	//For now assuming that if we're using Flash2M hardware we're behaving ourselves
	uint8_t oldVal = system_state.VIA_regs[VIA_ORA];
	uint8_t risingBits = nextVal & ~oldVal;
	if(risingBits & VIA_SPI_BIT_CLK) {
		cartridge_state.bank_shifter = cartridge_state.bank_shifter << 1;
		cartridge_state.bank_shifter &= 0xFE;
		cartridge_state.bank_shifter |= !!(oldVal & VIA_SPI_BIT_MOSI);
	} else if(risingBits & VIA_SPI_BIT_CS) {
		//flash cart CS is connected to latch clock
		if((cartridge_state.bank_mask ^ cartridge_state.bank_shifter) & 0x80) {
			SaveNVRAM();
		}
		cartridge_state.bank_mask = cartridge_state.bank_shifter;
		if(loadedRomType != RomType::FLASH2M_RAM32K) {
			cartridge_state.bank_mask |= 0x80;
		}
		//printf("Flash highbits set to %x\n", cartridge_state.bank_mask);
	}
}

uint8_t MemoryRead_Flash2M(uint16_t address) {
	if(address & 0x4000) {
		return cartridge_state.rom[0b111111100000000000000 | (address & 0x3FFF)];
	} else {
		if(!(cartridge_state.bank_mask & 0x80))
			return cartridge_state.save_ram[(address & 0x3FFF) | ((cartridge_state.bank_mask & 0x40) << 8)];
		else return cartridge_state.rom[((cartridge_state.bank_mask & 0x7F) << 14) | (address & 0x3FFF)];
	}
}

uint8_t MemoryRead_Unknown(uint16_t address) {
	//If cartridge_state.size is smaller than unbanked ROM range, align end with 0xFFFF and wrap
	//If cartridge_state.size is bigger than unbanked ROM range, access mainWindow at end of file.
	//TODO: Decide if unknown ROM type should just terminate emulator :P
	if(cartridge_state.size <= 32768) {
		return cartridge_state.rom[((address & 0x7FFF) + 32768 - cartridge_state.size) % cartridge_state.size];
	} else {
		return cartridge_state.rom[((address & 0x7FFF) + cartridge_state.size - 32768)];
	}
}

uint8_t* GetRAM(const uint16_t address) {
	return &(system_state.ram[FULL_RAM_ADDRESS(address & 0x1FFF)]);
}

uint8_t MemoryReadResolve(const uint16_t address, bool stateful) {
	if(address & 0x8000) {
		switch(loadedRomType) {
			case RomType::EEPROM8K:
			return cartridge_state.rom[address & 0x1FFF];
			case RomType::EEPROM32K:
			return cartridge_state.rom[address & 0x7FFF];
			case RomType::FLASH2M:
			case RomType::FLASH2M_RAM32K:
			return MemoryRead_Flash2M(address);
			case RomType::UNKNOWN:
			return MemoryRead_Unknown(address);
		}
	} else if(address & 0x4000) {
		return VDMA_Read(address);
	} else if((address >= 0x3000) && (address <= 0x3FFF)) {
		return soundcard->ram_read(address);
	} else if((address >= 0x2800) && (address <= 0x2FFF)) {
		return system_state.VIA_regs[address & 0xF];
	} else if(address < 0x2000) {
		if(stateful) {
			if(!system_state.ram_initialized[FULL_RAM_ADDRESS(address & 0x1FFF)]) {
				//printf("WARNING! Uninitialized RAM read at %x (Bank %x)\n", address, system_state.banking >> 5);
			}
		}
		return *GetRAM(address);
	} else if((address == 0x2008) || (address == 0x2009)) {
		return joysticks->read((uint8_t) address, stateful);
	}
	if(stateful) {
		printf("Attempted to read write-only device, may be unintended? %x\n", address);
	}
	return open_bus();
}

uint8_t MemoryRead(uint16_t address) {
	return MemoryReadResolve(address, true);
}

// Fast path for the main CPU: zero page reads bypass MemoryReadResolve entirely
uint8_t ITCM_CODE MemoryReadFast(uint16_t address) {
	if(address < 0x100) {
		return system_state.ram[FULL_RAM_ADDRESS(address)];
	}
	return MemoryReadResolve(address, true);
}

uint8_t MemorySync(uint16_t address) {
#if !defined(NDS_BUILD) && !defined(WASM_BUILD)
	if(timekeeper.clock_mode == CLOCKMODE_NORMAL) {
		if(Breakpoints::checkBreakpoint(address, cartridge_state.bank_mask)) {
			timekeeper.clock_mode = CLOCKMODE_STOPPED;
			Disassembler::Decode(MemoryReadResolve, loadedMemoryMap, address, 32);
			cpu_core->Freeze();
		}
		uint8_t opcode = MemoryReadResolve(address, false);
		if(opcode == 0x20) { //JSR
			uint16_t jsr_dest = MemoryReadResolve(address+1, false) | (MemoryReadResolve(address+2, false) << 8);
			profiler.LogJSR(address, cartridge_state.bank_mask, jsr_dest);
		} else if(opcode == 0x60) { //RTS
			profiler.LogRTS(address, cartridge_state.bank_mask);
		}
	}
#endif
	return MemoryRead(address);
}

void ITCM_CODE MemoryWrite(uint16_t address, uint8_t value) {
	if(address & 0x8000) {
		if(loadedRomType == RomType::FLASH2M_RAM32K) {
			if(!(address & 0x4000)) {
				if(!(cartridge_state.bank_mask & 0x80)) {
					cartridge_state.save_ram[(address & 0x3FFF) | ((cartridge_state.bank_mask & 0x40) << 8)] = value;
				}
			}
		}
		if(loadedRomType == RomType::FLASH2M) {
			if(cartridge_state.write_mode) {
				uint8_t* location;
				if(address & 0x4000) {
					location = &(cartridge_state.rom[0b111111100000000000000 | (address & 0x3FFF)]);
				} else {
					location = &(cartridge_state.rom[((cartridge_state.bank_mask & 0x7F) << 14) | (address & 0x3FFF)]);
				}
				*location &= value;
				cartridge_state.write_mode = false;
			} else {
				//Skipping over details like bypass and unlock commands for now
				//So off-spec flash operation will be inaccurate
				if(value == 0x10) {
					//Chip Erase
					for(int i = 0; i < (1 << 21); ++i) {
						cartridge_state.rom[i] = 0xFF;
					}
				} else if (value == 0x30) {
					//Sector erase
					uint8_t sectorBits = ((address & (1 << 13)) >> 13) | ((cartridge_state.bank_mask & 0x7F) << 1);
					uint8_t sectorNum = sectorBits >> 3;
					if(sectorNum < 31) {
						//most of the sector table
						uint32_t x = sectorNum << 16;
						for(uint32_t i = 0; i < (1 << 16); ++i) {
							cartridge_state.rom[x] = 0xFF;
							++x;
						}
					} else if((sectorBits & 4) == 0) {
						uint32_t x = 0x1F0000;
						for(uint32_t i = 0; i < (1 << 15); ++i) {
							cartridge_state.rom[x] = 0xFF;
							++x;
						}
					} else if(sectorBits == 0b11111100) {
						uint32_t x = 0x1F8000;
						for(uint32_t i = 0; i < (1 << 13); ++i) {
							cartridge_state.rom[x] = 0xFF;
							++x;
						}
					} else if(sectorBits == 0b11111101) {
						uint32_t x = 0x1FA000;
						for(uint32_t i = 0; i < (1 << 13); ++i) {
							cartridge_state.rom[x] = 0xFF;
							++x;
						}
					} else if((sectorBits >> 1) == 0b1111111) {
						uint32_t x = 0x1FC000;
						for(uint32_t i = 0; i < (1 << 14); ++i) {
							cartridge_state.rom[x] = 0xFF;
							++x;
						}
					}
				} else if(value == 0xA0) {
					cartridge_state.write_mode = true;
				} else if(value == 0x90) {
					//first byte of lock command should be a good time to write to file
#if defined(WASM_BUILD) || defined(NDS_BUILD)
					SaveModifiedFlash();
#else
					if(savingThread.joinable()) {
						savingThread.join();
					}
					savingThread = std::thread(SaveModifiedFlash);
#endif
				}
			}
		}
	}
	else if(address & 0x4000) {
		VDMA_Write(address, value);
	} else if(address >= 0x3000 && address <= 0x3FFF) {
		soundcard->ram_write(address, value);
	} else if((address & 0x2000)) {
		if(address & 0x800) {
			if(loadedRomType == RomType::FLASH2M) {
				if((address & 0xF) == VIA_ORA) {
					UpdateFlashShiftRegister(value);
				}
			}
			if((address & 0xF) == VIA_ORB) {
				if((system_state.VIA_regs[VIA_ORB] & 0x80) && !(value & 0x80)) {
					//falling edge of high bit of ORB
#if !defined(NDS_BUILD) && !defined(WASM_BUILD)
					if(value & 0x40) {
						//report duration
						profiler.LogTime(value & 0x3F);
					} else {
						//store timestamp
						profiler.profilingTimeStamps[value & 0x3F] = timekeeper.totalCyclesCount;
					}
#endif
				}
			}
			system_state.VIA_regs[address & 0xF] = value;
		} else {
			if((address & 0x000F) == 0x0007) {
				blitter->CatchUp();
				if((value & DMA_VID_OUT_PAGE_BIT) != (system_state.dma_control & DMA_VID_OUT_PAGE_BIT)) {
#if !defined(NDS_BUILD) && !defined(WASM_BUILD)
					profiler.bufferFlipCount++;
					if(profiler.measure_by_frameflip) {
						profiler.ResetTimers();
						profiler.last_blitter_activity = blitter->pixels_this_frame;
						blitter->pixels_this_frame = 0;
					}
#endif
				}
				system_state.dma_control = value;
				system_state.dma_control_irq = (system_state.dma_control & DMA_COPY_IRQ_BIT) != 0;
				if(gRAM_Surface) {
					if(system_state.dma_control & DMA_TRANSPARENCY_BIT) {
						SDL_SetColorKey(gRAM_Surface, SDL_TRUE, SDL_MapRGB(gRAM_Surface->format, 0, 0, 0));
					} else {
						SDL_SetColorKey(gRAM_Surface, SDL_FALSE, 0);
					}
				}
			} else if((address & 0x000F) == 0x0005) {
				blitter->CatchUp();
				system_state.banking = value;
				UpdateBankingCache();
				//printf("banking reg set to %x\n", value);
			} else {
				soundcard->register_write(address, value);
			}
		}
	}
	else if(address < 0x2000) {
		/*if(!system_state.ram_initialized[FULL_RAM_ADDRESS(address & 0x1FFF)]) {
			printf("First RAM write at %x (Bank %x) (Value %x)\n", address, system_state.banking >> 6, value);
		}*/
		system_state.ram_initialized[FULL_RAM_ADDRESS(address & 0x1FFF)] = true;
		system_state.ram[FULL_RAM_ADDRESS(address & 0x1FFF)] = value;
	}
}

SDL_Event e;
bool running = true;
bool gofast = false;
bool paused = false;
bool lshift = false;
bool rshift = false;

void randomize_vram() {
	for(int i = 0; i < VRAM_BUFFER_SIZE; i ++) {
		system_state.vram[i] = rand() % 256;
		put_pixel32(vRAM_Surface, i & 127, i >> 7, Palette::ConvertColor(vRAM_Surface, system_state.vram[i]));
	}
	for(int i = 0; i < GRAM_BUFFER_SIZE; i ++) {
		system_state.gram[i] = rand() % 256;
		if (gRAM_Surface) {
			put_pixel32(gRAM_Surface, i & 127, i >> 7, Palette::ConvertColor(gRAM_Surface, system_state.gram[i]));
		}
	}
}

void randomize_memory() {
	for(int i = 0; i < RAMSIZE; i++) {
		system_state.ram[i] = rand() % 256;
		system_state.ram_initialized[i] = false;
	}

	for(int i = 0; i < VRAM_BUFFER_SIZE; i++) {
		system_state.vram[i] = rand() % 256;	
	}

	for(int i = 0; i < GRAM_BUFFER_SIZE; i++) {
		system_state.gram[i] = rand() % 256;	
	}
	
	system_state.dma_control = rand() % 256;
	system_state.dma_control_irq = (system_state.dma_control & DMA_COPY_IRQ_BIT) != 0;
	system_state.banking = rand() % 256;
	UpdateBankingCache();
	blitter->gram_mid_bits = rand() % 4;
}

extern "C" {
void PauseEmulation() {
  paused = true;

  AudioCoprocessor::singleton_acp_state->isEmulationPaused = true;
}

void ResumeEmulation() {
  paused = false;

  AudioCoprocessor::singleton_acp_state->isEmulationPaused = false;
}
}

void CPUStopped() {
	paused = true;
	printf("CPU stopped");
#ifdef TINYFILEDIALOGS_H
	tinyfd_notifyPopup("Alert",
		"CPU has stopped either due to STP opcode",
		"info");
#endif
}

#ifndef NDS_BUILD
const char * open_rom_dialog() {
	char const * lFilterPatterns[1] = {"*.gtr"};
#ifdef TINYFILEDIALOGS_H
	return tinyfd_openFileDialog(
		"Select a GameTank ROM file",
		"",
		1,
		lFilterPatterns,
		"GameTank Rom",
		0);
#else
	return EMBED_ROM_FILE;
#endif
}
#endif // !NDS_BUILD

extern "C" {
	// Attempts to load a rom by filename into a buffer
	// 0 on success
	// -1 on failure (e.g. file by name doesn't exist)
	int LoadRomFile(const char* filename) {
		printf("Enter LoadRomFile\n");
#ifdef NDS_BUILD
		// NDS: simple string-based path handling
        // avoid std::string allocation for debugging
		// currentRomFilePath = std::string(filename);

		// Build save file paths by replacing extension
        /*
		std::string basePath = currentRomFilePath;
		size_t dotPos = basePath.rfind('.');
		if (dotPos != std::string::npos) {
			basePath = basePath.substr(0, dotPos);
		}
		nvramFileFullPath = basePath + ".sav";
        */
        // Hardcode save path for now or use C-string
        // nvramFileFullPath = "fat:/save.sav"; 
        
		if (EmulatorConfig::xorFile != NULL) {
			flashFileFullPath = std::string(EmulatorConfig::xorFile);
		} else {
			// flashFileFullPath = basePath + ".xor";
		}
#else
		std::filesystem::path filepath(filename);
		std::filesystem::path filepath(filename);
		currentRomFilePath = filepath.string();
#ifdef WASM_BUILD
		std::filesystem::path nvramPath("/idbfs");
		nvramPath /= std::filesystem::path(currentRomFilePath).filename();
#else
		std::filesystem::path nvramPath(filename);
#endif
		nvramPath.replace_extension("sav");
		nvramFileFullPath = nvramPath.string();
		if (EmulatorConfig::xorFile != NULL) {
		  flashFileFullPath = std::string(EmulatorConfig::xorFile);
		} else {
		    nvramPath.replace_extension("xor");
		    flashFileFullPath = nvramPath.string();
		}
		nvramPath.replace_extension("gtrcfg");

		gameconfig = new GameConfig(nvramPath.string().c_str());

		std::filesystem::path defaultMemMapFilePath = filepath.parent_path().append("../build/out.map");
		std::filesystem::path defaultSourceMapFilePath = filepath.parent_path().append("../build/sourcemap.dbg");

		if(std::filesystem::exists(defaultMemMapFilePath)) {
			printf("found default memory map file location %s\n", defaultMemMapFilePath.c_str());
			loadedMemoryMap = new MemoryMap(defaultMemMapFilePath.string());
			Breakpoints::linkBreakpoints(*loadedMemoryMap);
		} else {
			loadedMemoryMap = new MemoryMap();
			printf("default memory map file %s not found\n", defaultMemMapFilePath.c_str());
		}

		if(std::filesystem::exists(defaultSourceMapFilePath)) {
			printf("found default source map file location %s\n", defaultSourceMapFilePath.c_str());
			std::string sourceMapPathString = defaultSourceMapFilePath.string();
			SourceMap::singleton = new SourceMap(sourceMapPathString);
		} else {
			printf("default source map file %s not found\n", defaultSourceMapFilePath.c_str());
		}
#endif // NDS_BUILD

		printf("loading %s\n", filename);
		FILE* romFileP = fopen(filename, "rb");
		if(!romFileP) {
#ifdef NDS_BUILD
			printf("fopen failed: %s\n", strerror(errno));
#endif
			printf("Unable to open file: %s\n", filename);
			return -1;
		}
		fseek(romFileP, 0L, SEEK_END);
		long fsize = ftell(romFileP);
		rewind(romFileP);

		printf("File size: %ld\n", fsize);
		
		if (fsize < 0) {
			printf("ftell failed! closing.\n");
			fclose(romFileP);
			return -1;
		}
		
		if (fsize > (1 << 21)) {
			printf("ROM too large! Cap to 2MB.\n");
			cartridge_state.size = (1 << 21);
		} else {
			cartridge_state.size = fsize;
		}
		
		cartridge_state.write_mode = false;
		
		switch(cartridge_state.size) {
			case 8192:
			loadedRomType = RomType::EEPROM8K;
			printf("Detected 8K (EEPROM)\n");
			break;
			case 32768:
			loadedRomType = RomType::EEPROM32K;
			printf("Detected 32K (EEPROM)\n");
			break;
			case 2097152:
			loadedRomType = RomType::FLASH2M;
			printf("Detected 2M (Flash)\n");
			break;
			default:
			// loadedRomType = RomType::UNKNOWN; // Don't override unknown?
			printf("Unknown ROM type (size %d)\n", cartridge_state.size);
			if (cartridge_state.size > 2000000) loadedRomType = RomType::FLASH2M; // Assume flash if large
			else loadedRomType = RomType::EEPROM32K; // Fallback?
			break;
		}
		
		printf("Reading %d bytes...\n", cartridge_state.size);
		fread(cartridge_state.rom, sizeof(uint8_t), cartridge_state.size, romFileP);
		printf("Read complete.\n");
		fclose(romFileP);

		if(cpu_core) {
			paused = false;
			cpu_core->Reset();
			cartridge_state.write_mode = false;
		}

		if(loadedRomType == RomType::FLASH2M) {
#ifdef NDS_BUILD
			if(file_exists(flashFileFullPath.c_str())) {
#else
			if(std::filesystem::exists(flashFileFullPath.c_str())) {
#endif
				std::cout << "Loading flash save from " << flashFileFullPath << "\n";
				LoadModifiedFlash();
			} else {
				std::cout << "Couldn't find " << flashFileFullPath << "\n";
			}

			if(
				(cartridge_state.rom[0x1FFFF0] == 'S') &&
				(cartridge_state.rom[0x1FFFF1] == 'A') &&
				(cartridge_state.rom[0x1FFFF2] == 'V') &&
				(cartridge_state.rom[0x1FFFF3] == 'E')) {
					loadedRomType = RomType::FLASH2M_RAM32K;
#ifdef NDS_BUILD
					if(file_exists(nvramFileFullPath.c_str())) {
#else
					if(std::filesystem::exists(nvramFileFullPath.c_str())) {
#endif
						LoadNVRAM();
					}
				}
		}
		return 0;
	}

	void SetButtons(int buttonMask) {
		if(joysticks != NULL) {
			joysticks->SetHeldButtons(buttonMask);
		}
	}

#ifndef NDS_BUILD
	void takeScreenShot() {
		SDL_Surface *screenshot = SDL_CreateRGBSurface(0, SCREEN_WIDTH, SCREEN_HEIGHT, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
		SDL_RenderReadPixels(mainRenderer, NULL, SDL_PIXELFORMAT_ARGB8888, screenshot->pixels, screenshot->pitch);
		SDL_SaveBMP(screenshot, "screenshot.bmp");
		SDL_FreeSurface(screenshot);
	}
#endif
}
#if !defined(WASM_BUILD) && !defined(NDS_BUILD)
template <typename T>
void closeToolByType() {
    toolWindows.erase(
        std::remove_if(
            toolWindows.begin(),
            toolWindows.end(),
            [](BaseWindow* window) {
                if(dynamic_cast<T*>(window) != nullptr) {
					delete window;
					return true;
				}
				return false;
            }
        ),
        toolWindows.end()
    );
}

template <typename T>
bool toolTypeIsOpen() {
    for (const auto& window : toolWindows) {
        if (dynamic_cast<T*>(window) != nullptr) {
            return true;
        }
    }
    return false;
}

void toggleProfilerWindow() {
	if(!toolTypeIsOpen<ProfilerWindow>()) {
		toolWindows.push_back(new ProfilerWindow(profiler));
	} else {
		closeToolByType<ProfilerWindow>();
	}
}

void toggleMemBrowserWindow() {
	if(!toolTypeIsOpen<MemBrowserWindow>()) {
		toolWindows.push_back(new MemBrowserWindow(loadedMemoryMap, MemoryReadResolve, GetRAM, *gameconfig));
	} else {
		closeToolByType<MemBrowserWindow>();
	}
}

void toggleVRAMWindow() {
	if(!toolTypeIsOpen<VRAMWindow>()) {
		toolWindows.push_back(new VRAMWindow(vRAM_Surface, gRAM_Surface,
			&system_state, cpu_core, &cartridge_state));
	} else {
		closeToolByType<VRAMWindow>();
	}
}

void toggleSteppingWindow() {
	if(!toolTypeIsOpen<SteppingWindow>()) {
		toolWindows.push_back(new SteppingWindow(timekeeper, loadedMemoryMap, cpu_core, *gameconfig, cartridge_state));
	} else {
		closeToolByType<SteppingWindow>();
	}
}

void togglePatchingWindow() {
	if(!toolTypeIsOpen<PatchingWindow>()) {
		toolWindows.push_back(new PatchingWindow(loadedMemoryMap, gameconfig));
	} else {
		closeToolByType<PatchingWindow>();
	}
}

void doRamDump() {
	soundcard->dump_ram("audio_debug.dat");
	ofstream dumpfile ("ram_debug.dat", ios::out | ios::binary);
	dumpfile.write((char*) system_state.ram, RAMSIZE);
	dumpfile.close();
}

void toggleControllerOptionsWindow() {
	if(!toolTypeIsOpen<ControllerOptionsWindow>()) {
		toolWindows.push_back(new ControllerOptionsWindow(joysticks));
	} else {
		closeToolByType<ControllerOptionsWindow>();
	}
}

#endif

#ifndef NDS_BUILD
void toggleFullScreen() {
	if(isFullScreen) {
		SDL_SetWindowFullscreen(mainWindow, 0);
		isFullScreen = false;
	} else {
		SDL_SetWindowFullscreen(mainWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
		isFullScreen = true;
	}
	timekeeper.scaling_increment = INITIAL_SCALING_INCREMENT;
}
#endif

void toggleMute() {
	muteMask = muteMask ^ MUTE_SOURCE_MANUAL;
	AudioCoprocessor::singleton_acp_state->isMuted = (muteMask != 0);
}

void setMenuMute(bool muted) {
	muteMask &= ~MUTE_SOURCE_MENU;
	if(muted) {
		muteMask |= MUTE_SOURCE_MENU;
	}
	AudioCoprocessor::singleton_acp_state->isMuted = (muteMask != 0);
}

#ifndef NDS_BUILD
typedef struct HotkeyAssignment {
	void (*func)();
	SDL_Keycode  key;
} HotkeyAssignment;

HotkeyAssignment hotkeys[] = {
	{&toggleFullScreen, SDLK_F11},
	{&toggleMute, SDLK_m},
#if !defined(WASM_BUILD) && !defined(WRAPPER_MODE)
	{&doRamDump, SDLK_F6},
	{&toggleSteppingWindow, SDLK_F7},
	{&takeScreenShot, SDLK_F8},
	{&toggleMemBrowserWindow, SDLK_F9},
	{&toggleVRAMWindow, SDLK_F10},
	{&toggleProfilerWindow, SDLK_F12},
#endif
};

bool checkHotkey(SDL_Keycode  key) {
	for(HotkeyAssignment assignment : hotkeys) {
		if(assignment.key == key) {
			assignment.func();
			return true;
		}
	}
	return false;
}
#endif // !NDS_BUILD

#ifdef NDS_BUILD
#include <dirent.h>
#include <errno.h>
#include <strings.h>

void DebugListDir(const char* path) {
	DIR* dir = opendir(path);
	if (!dir) {
		printf("opendir('%s') failed: %s\n", path, strerror(errno));
		return;
	}
	printf("Contents of '%s':\n", path);
	struct dirent* ent;
	while ((ent = readdir(dir)) != NULL) {
		printf("  %s%s\n", ent->d_name, (ent->d_type == DT_DIR) ? "/" : "");
	}
	closedir(dir);
	printf("---\n");
}

// ============================================================
// NDS Bottom Screen Menu System
// ============================================================

enum NDSMenuScreen {
	NDS_MENU_MAIN,
	NDS_MENU_FILEBROWSER
};

enum NDSMainMenuItem {
	NDS_ITEM_LOAD_ROM = 0,
	NDS_ITEM_RESET,
	NDS_ITEM_MUTE,
	NDS_ITEM_VOLUME,
	NDS_ITEM_FRAMESKIP,
	NDS_ITEM_EXIT,
	NDS_MAIN_ITEM_COUNT
};

struct NDSMenuState {
	NDSMenuScreen screen;
	int cursor;
	// File browser state
	char currentDir[256];
	struct FileEntry {
		char name[128];
		bool isDir;
	};
	FileEntry entries[128];
	int entryCount;
	int fileScroll;     // first visible index
	bool needsRedraw;
};

static NDSMenuState ndsMenu;
static bool ndsMenuOpen = false;
static int ndsFrameSkip = 1;    // render every Nth frame (1=no skip, 2=skip 1, etc)
static int ndsFrameCounter = 0;

#define NDS_FILE_LINES 20  // visible file entries on screen

static void ndsMenuScanDir() {
	DIR* dir = opendir(ndsMenu.currentDir);
	ndsMenu.entryCount = 0;
	ndsMenu.fileScroll = 0;
	if (!dir) return;

	// Add parent directory entry unless at root
	if (strcmp(ndsMenu.currentDir, "fat:/") != 0 &&
	    strcmp(ndsMenu.currentDir, "sd:/") != 0) {
		strncpy(ndsMenu.entries[0].name, "..", sizeof(ndsMenu.entries[0].name));
		ndsMenu.entries[0].isDir = true;
		ndsMenu.entryCount = 1;
	}

	struct dirent* ent;
	while ((ent = readdir(dir)) != NULL && ndsMenu.entryCount < 128) {
		// Skip . and ..
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;

		bool isDir = (ent->d_type == DT_DIR);
		// Skip non-directory, non-.gtr files
		if (!isDir) {
			const char* ext = strrchr(ent->d_name, '.');
			if (!ext || (strcasecmp(ext, ".gtr") != 0))
				continue;
		}

		strncpy(ndsMenu.entries[ndsMenu.entryCount].name, ent->d_name,
				sizeof(ndsMenu.entries[0].name) - 1);
		ndsMenu.entries[ndsMenu.entryCount].name[sizeof(ndsMenu.entries[0].name) - 1] = '\0';
		ndsMenu.entries[ndsMenu.entryCount].isDir = isDir;
		ndsMenu.entryCount++;
	}
	closedir(dir);
}

static void ndsMenuDrawMain() {
	// Clear console and draw main menu
	printf("\x1b[2J");  // clear screen
	printf("\x1b[0;0H"); // cursor home
	printf("GAMETANK EMULATOR\n");
	printf("=========================\n");

	const char* labels[NDS_MAIN_ITEM_COUNT] = {
		"Load ROM",
		"Reset",
		NULL, // Mute (dynamic)
		NULL, // Volume (dynamic)
		NULL, // Frame Skip (dynamic)
		"Exit"
	};

	for (int i = 0; i < NDS_MAIN_ITEM_COUNT; i++) {
		printf("%s ", (ndsMenu.cursor == i) ? ">" : " ");
		switch (i) {
			case NDS_ITEM_MUTE:
				printf("Mute: %s\n",
					(muteMask & MUTE_SOURCE_MANUAL) ? "ON" : "OFF");
				break;
			case NDS_ITEM_VOLUME: {
				int vol = AudioCoprocessor::singleton_acp_state->volume;
				printf("Volume: < %3d >\n", vol);
				break;
			}
			case NDS_ITEM_FRAMESKIP:
				printf("Frame Skip: < %d >\n", ndsFrameSkip);
				break;
			default:
				printf("%s\n", labels[i]);
				break;
		}
	}

	printf("\n D-Pad:Navigate  A:Select  B:Close\n");
	printf(" Left/Right to adjust values\n");
}

static void ndsMenuDrawFileBrowser() {
	printf("\x1b[2J");
	printf("\x1b[0;0H");
	printf("SELECT ROM\n");
	// Show shortened path
	printf("%.30s\n", ndsMenu.currentDir);
	printf("=========================\n");

	int visible = NDS_FILE_LINES;
	if (ndsMenu.entryCount == 0) {
		printf("  (empty)\n");
		return;
	}

	int end = ndsMenu.fileScroll + visible;
	if (end > ndsMenu.entryCount) end = ndsMenu.entryCount;

	for (int i = ndsMenu.fileScroll; i < end; i++) {
		printf("%s ", (ndsMenu.cursor == i) ? ">" : " ");
		if (ndsMenu.entries[i].isDir) {
			printf("[%s]/\n", ndsMenu.entries[i].name);
		} else {
			printf("%s\n", ndsMenu.entries[i].name);
		}
	}

	printf("\n A:Select B:Back L/R:Page\n");
}

static void ndsMenuDraw() {
	if (ndsMenu.screen == NDS_MENU_MAIN) {
		ndsMenuDrawMain();
	} else {
		ndsMenuDrawFileBrowser();
	}
	ndsMenu.needsRedraw = false;
}

static void ndsMenuOpen_() {
	ndsMenuOpen = true;
	showMenu = true;
	paused = true;
	setMenuMute(true);
	ndsMenu.screen = NDS_MENU_MAIN;
	ndsMenu.cursor = 0;
	ndsMenu.needsRedraw = true;
}

static void ndsMenuClose() {
	ndsMenuOpen = false;
	showMenu = false;
	paused = false;
	setMenuMute(false);
	// Clear console
	printf("\x1b[2J");
	printf("\x1b[0;0H");
}

// Called after scanKeys() has already been invoked for this frame
static void ndsMenuHandleInput() {
	uint16_t down = keysDown();
	uint16_t repeat = keysDownRepeat();

	if (ndsMenu.screen == NDS_MENU_MAIN) {
		if (down & (KEY_L | KEY_R | KEY_B)) {
			ndsMenuClose();
			return;
		}
		if (repeat & KEY_UP) {
			ndsMenu.cursor--;
			if (ndsMenu.cursor < 0) ndsMenu.cursor = NDS_MAIN_ITEM_COUNT - 1;
			ndsMenu.needsRedraw = true;
		}
		if (repeat & KEY_DOWN) {
			ndsMenu.cursor++;
			if (ndsMenu.cursor >= NDS_MAIN_ITEM_COUNT) ndsMenu.cursor = 0;
			ndsMenu.needsRedraw = true;
		}
		// LEFT/RIGHT for value adjustment
		if (repeat & KEY_LEFT) {
			if (ndsMenu.cursor == NDS_ITEM_VOLUME) {
				int& vol = AudioCoprocessor::singleton_acp_state->volume;
				vol -= 16;
				if (vol < 0) vol = 0;
				ndsMenu.needsRedraw = true;
			} else if (ndsMenu.cursor == NDS_ITEM_FRAMESKIP) {
				ndsFrameSkip--;
				if (ndsFrameSkip < 1) ndsFrameSkip = 1;
				ndsMenu.needsRedraw = true;
			}
		}
		if (repeat & KEY_RIGHT) {
			if (ndsMenu.cursor == NDS_ITEM_VOLUME) {
				int& vol = AudioCoprocessor::singleton_acp_state->volume;
				vol += 16;
				if (vol > 256) vol = 256;
				ndsMenu.needsRedraw = true;
			} else if (ndsMenu.cursor == NDS_ITEM_FRAMESKIP) {
				ndsFrameSkip++;
				if (ndsFrameSkip > 8) ndsFrameSkip = 8;
				ndsMenu.needsRedraw = true;
			}
		}
		if (down & KEY_A) {
			switch (ndsMenu.cursor) {
				case NDS_ITEM_LOAD_ROM:
					ndsMenu.screen = NDS_MENU_FILEBROWSER;
					strncpy(ndsMenu.currentDir, "sd:/", sizeof(ndsMenu.currentDir));
					ndsMenuScanDir();
					ndsMenu.cursor = 0;
					ndsMenu.needsRedraw = true;
					break;
				case NDS_ITEM_RESET:
					ndsMenuClose();
					resetQueued = 2;
					break;
				case NDS_ITEM_MUTE:
					toggleMute();
					ndsMenu.needsRedraw = true;
					break;
				case NDS_ITEM_VOLUME:
					// A on volume does nothing; use left/right
					break;
				case NDS_ITEM_EXIT:
					running = false;
					ndsMenuClose();
					break;
			}
		}
	} else {
		// File browser
		if (down & KEY_B) {
			// Go back to main menu
			ndsMenu.screen = NDS_MENU_MAIN;
			ndsMenu.cursor = 0;
			ndsMenu.needsRedraw = true;
			return;
		}
		if (repeat & KEY_UP) {
			ndsMenu.cursor--;
			if (ndsMenu.cursor < 0) ndsMenu.cursor = ndsMenu.entryCount - 1;
			// Adjust scroll
			if (ndsMenu.cursor < ndsMenu.fileScroll)
				ndsMenu.fileScroll = ndsMenu.cursor;
			if (ndsMenu.cursor >= ndsMenu.fileScroll + NDS_FILE_LINES)
				ndsMenu.fileScroll = ndsMenu.cursor - NDS_FILE_LINES + 1;
			ndsMenu.needsRedraw = true;
		}
		if (repeat & KEY_DOWN) {
			ndsMenu.cursor++;
			if (ndsMenu.cursor >= ndsMenu.entryCount) ndsMenu.cursor = 0;
			if (ndsMenu.cursor >= ndsMenu.fileScroll + NDS_FILE_LINES)
				ndsMenu.fileScroll = ndsMenu.cursor - NDS_FILE_LINES + 1;
			if (ndsMenu.cursor < ndsMenu.fileScroll)
				ndsMenu.fileScroll = ndsMenu.cursor;
			ndsMenu.needsRedraw = true;
		}
		// Page up/down with L/R
		if (down & KEY_L) {
			ndsMenu.cursor -= NDS_FILE_LINES;
			if (ndsMenu.cursor < 0) ndsMenu.cursor = 0;
			ndsMenu.fileScroll = ndsMenu.cursor;
			ndsMenu.needsRedraw = true;
		}
		if (down & KEY_R) {
			ndsMenu.cursor += NDS_FILE_LINES;
			if (ndsMenu.cursor >= ndsMenu.entryCount)
				ndsMenu.cursor = ndsMenu.entryCount - 1;
			ndsMenu.fileScroll = ndsMenu.cursor - NDS_FILE_LINES + 1;
			if (ndsMenu.fileScroll < 0) ndsMenu.fileScroll = 0;
			ndsMenu.needsRedraw = true;
		}
		if (down & KEY_A) {
			if (ndsMenu.entryCount == 0) return;
			auto& entry = ndsMenu.entries[ndsMenu.cursor];
			if (entry.isDir) {
				if (strcmp(entry.name, "..") == 0) {
					// Go up one directory
					char* lastSlash = strrchr(ndsMenu.currentDir, '/');
					if (lastSlash && lastSlash != ndsMenu.currentDir) {
						// Check if it's like "fat:/" — don't go above mount root
						char* colon = strchr(ndsMenu.currentDir, ':');
						if (colon && lastSlash == colon + 1) {
							// Already at root (e.g. "fat:/")
						} else {
							*lastSlash = '\0';
							// Find the new last slash to keep trailing /
							char* newLast = strrchr(ndsMenu.currentDir, '/');
							if (newLast) {
								*(newLast + 1) = '\0';
							}
						}
					}
				} else {
					// Enter subdirectory
					size_t len = strlen(ndsMenu.currentDir);
					snprintf(ndsMenu.currentDir + len,
							sizeof(ndsMenu.currentDir) - len,
							"%s/", entry.name);
				}
				ndsMenuScanDir();
				ndsMenu.cursor = 0;
				ndsMenu.needsRedraw = true;
			} else {
				// Load ROM file
				char fullPath[512];
				snprintf(fullPath, sizeof(fullPath), "%s%s",
						ndsMenu.currentDir, entry.name);
				ndsMenuClose();
				LoadRomFile(fullPath);
			}
		}
	}
}

#endif // NDS_BUILD

#ifndef EM_BOOL
#define EM_BOOL int
#endif

void refreshScreen() {
#ifdef NDS_BUILD
	// Use pre-converted RGB15 buffer written by blitter, copy via DMA
	int srcPage = (system_state.dma_control & DMA_VID_OUT_PAGE_BIT) ? 1 : 0;
	uint16_t* srcBuffer = system_state.vram_rgb15 + (srcPage * 128 * 128);
	uint16_t* dsVram = (uint16_t*)BG_BMP_RAM(0);

	// Center 128x128 on 256x192
	int xOff = (NDS_SCREEN_WIDTH - GT_WIDTH) / 2;  // 64
	int yOff = (NDS_SCREEN_HEIGHT - GT_HEIGHT) / 2;  // 32

	// Copy each row via DMA - much faster than pixel-by-pixel
	for (int y = 0; y < GT_HEIGHT; y++) {
		uint16_t* srcRow = srcBuffer + (y * GT_WIDTH);
		uint16_t* dstRow = dsVram + ((yOff + y) * NDS_SCREEN_WIDTH) + xOff;
		dmaCopy(srcRow, dstRow, GT_WIDTH * sizeof(uint16_t));
	}
#else
	SDL_Rect src, dest;
	int scr_w, scr_h;
	src.x = 0;
	src.y = (system_state.dma_control & DMA_VID_OUT_PAGE_BIT) ? GT_HEIGHT : 0;
	src.w = GT_WIDTH;
	src.h = GT_HEIGHT;
	SDL_GetWindowSize(mainWindow, &scr_w, &scr_h);
	dest.w = min(scr_w, scr_h);
	dest.h = dest.w;
	dest.x = (scr_w - dest.w) / 2;
	dest.y = (scr_h - dest.h) / 2;
	SDL_UpdateTexture(framebufferTexture, NULL, vRAM_Surface->pixels, vRAM_Surface->pitch);

	SDL_RenderClear(mainRenderer);
	SDL_RenderCopy(mainRenderer, framebufferTexture, &src, &dest);

	src.x = GT_WIDTH-1;
	src.w = 1;
	dest.w = dest.w * 86.0 / 512.0;
	dest.x -= dest.w;

	SDL_RenderCopy(mainRenderer, framebufferTexture, &src, &dest);

	dest.x += dest.w + dest.h;

	SDL_RenderCopy(mainRenderer, framebufferTexture, &src, &dest);

#if !defined(WASM_BUILD)
	ImGui::SetCurrentContext(main_imgui_ctx);
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();
	if(showMenu) {
#ifndef WRAPPER_MODE
		if(ImGui::BeginMainMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				if(ImGui::MenuItem("Open Rom")) {
					const char* rom_file_name = open_rom_dialog();
					if(rom_file_name) {
						LoadRomFile(rom_file_name);
					}	
				}
				if(ImGui::MenuItem("Exit")) {
					running = false;
				}
				ImGui::EndMenu();
			}
			if(ImGui::BeginMenu("Settings")) {
				if(ImGui::MenuItem("Controllers")) {
					toggleControllerOptionsWindow();
				}
				ImGui::MenuItem("Toggle Instant Blits", NULL, &(blitter->instant_mode));
				ImGui::SliderInt("Volume", &AudioCoprocessor::singleton_acp_state->volume, 0, 256);
				ImGui::Checkbox("Mute", &AudioCoprocessor::singleton_acp_state->isMuted);
				if(ImGui::BeginMenu("Pallete")) {
					ImGui::RadioButton("Unscaled Capture", &palette_select, PALETTE_SELECT_CAPTURE);
					ImGui::RadioButton("Full Contrast", &palette_select, PALETTE_SELECT_SCALED);
					ImGui::RadioButton("Cheap HDMI converter", &palette_select, PALETTE_SELECT_HDMI);
					ImGui::RadioButton("Flawed Theory (Legacy)", &palette_select, PALETTE_SELECT_OLD);
					ImGui::EndMenu();
				}
				ImGui::EndMenu();
			}
			if(ImGui::BeginMenu("Tools")) {
				if(ImGui::MenuItem("Profiler (F12)")) {
					toggleProfilerWindow();
				}
				if(ImGui::MenuItem("Memory Browser (F9)")) {
					toggleMemBrowserWindow();
				}
				if(ImGui::MenuItem("VRAM Viewer (F10)")) {
					toggleVRAMWindow();
				}
				if(ImGui::MenuItem("Code Stepper (F7)")) {
					toggleSteppingWindow();
				}
				if(ImGui::MenuItem("Patching Window")) {
					togglePatchingWindow();
				}
				if(ImGui::MenuItem("Update Patches")) {
					gameconfig->UpdateAllPatches(cartridge_state.rom);
				}
				if(ImGui::MenuItem("Dump RAM to file (F6)")) {
					doRamDump();
				}
				if(ImGui::MenuItem("Deep Profile Single Vsync")) {
					vsyncProfileArmed = true;
				}
				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}
#else
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.5f)); // 50% transparent black
		ImGui::Begin("OverlayBackground", nullptr,
			ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_NoInputs | 
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoBringToFrontOnFocus);
		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(2);

		// Now create the actual menu window in the top-left corner
		ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.9f);
		if(!ImGui::IsAnyItemFocused()) {
			ImGui::SetNextWindowFocus();
		}
		menuOpening = false;
		ImGui::Begin("MainMenu", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoTitleBar);

			ImGui::SetWindowFontScale(2.0f);

		if (ImGui::IsWindowAppearing())
    		ImGui::SetKeyboardFocusHere(-1);
		

		if (ImGui::BeginMenu("Options")) {
			// These are items inside the pop-out menu
			if (ImGui::MenuItem("Toggle Full Screen")) {
				toggleFullScreen();
			}
			ImGui::SliderInt("Volume", &AudioCoprocessor::singleton_acp_state->volume, 0, 256, "", ImGuiSliderFlags_NoInput);
			bool appMute = (muteMask & MUTE_SOURCE_MANUAL) != 0;
			ImGui::Checkbox("Mute Audio", &appMute);
			if(appMute) muteMask |= MUTE_SOURCE_MANUAL;
			else muteMask &= ~MUTE_SOURCE_MANUAL;
			AudioCoprocessor::singleton_acp_state->isMuted = (muteMask != 0);
			ImGui::EndMenu();
		}

		if(ImGui::Selectable("Reset")) {
			resetQueued = 2;
			showMenu = false;
			setMenuMute(showMenu);
			joysticks->Reset();
		}

		if(ImGui::Selectable("Exit")) {
			running = false;
		}

		ImGui::End();
#endif
	}
	ImGui::Render();
	ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
#endif
	SDL_RenderPresent(mainRenderer);
#endif // !NDS_BUILD
}

char titlebuf[256];
int32_t intended_cycles = 0;

#ifdef WASM_BUILD
double target_frame_period_ms = 1000.0 / 60.0;
double last_raf_time = 0;
double frame_time_accumulator = 0;
#endif

EM_BOOL mainloop(double time, void* userdata) {
#ifdef WASM_BUILD
        double delta_time = time - last_raf_time;
        frame_time_accumulator += delta_time;
        last_raf_time = time;
        if(frame_time_accumulator < target_frame_period_ms) {
                return true;
        }
        frame_time_accumulator -= target_frame_period_ms;
#endif

#ifdef WRAPPER_MODE
	if(!paused && !showMenu) {
#else
	if(!paused) {
#endif
			timekeeper.actual_cycles = timekeeper.totalCyclesCount;
#if !defined(WASM_BUILD) && !defined(NDS_BUILD)
			switch(timekeeper.clock_mode) {
				case CLOCKMODE_NORMAL:
					cpu_core->freeze = false;
					intended_cycles = timekeeper.cycles_per_vsync;
					break;
				case CLOCKMODE_SINGLE:
					cpu_core->freeze = false;
					Disassembler::Decode(MemoryReadResolve, loadedMemoryMap, cpu_core->pc, 32);
					intended_cycles = 1;
					timekeeper.clock_mode = CLOCKMODE_STOPPED;
					break;
				case CLOCKMODE_STOPPED:
					intended_cycles = 0;
					break;
			}
			if(intended_cycles) {
				cpu_core->Run(intended_cycles, timekeeper.totalCyclesCount);
			}
#else
			intended_cycles = timekeeper.cycles_per_vsync;
			cpu_core->RunOptimized(intended_cycles, timekeeper.totalCyclesCount);
#endif
			timekeeper.actual_cycles = timekeeper.totalCyclesCount - timekeeper.actual_cycles;
			if(cpu_core->illegalOpcode) {
				printf("Hit illegal opcode %x\npc = %x\n", cpu_core->illegalOpcodeSrc, cpu_core->pc);
				paused = true;
			} else if((timekeeper.clock_mode == CLOCKMODE_NORMAL) && (timekeeper.actual_cycles == 0)) {
#if !defined(NDS_BUILD) && !defined(WASM_BUILD)
				profiler.zeroConsec++;
				if(profiler.zeroConsec == 10) {
					printf("(Got stuck at 0x%x)\n", cpu_core->pc);
					paused = true;
				}
#endif
				timekeeper.totalCyclesCount += intended_cycles;
			} else {
#if !defined(NDS_BUILD) && !defined(WASM_BUILD)
				profiler.zeroConsec = 0;
#endif
			}

#if !defined(WASM_BUILD) && !defined(NDS_BUILD)
			if(!gofast) {
				SDL_Delay(timekeeper.time_scaling * intended_cycles/timekeeper.system_clock);
			} else {
				timekeeper.lastTicks = 0;
			}
			timekeeper.currentTicks = SDL_GetTicks();

			if(timekeeper.clock_mode == CLOCKMODE_NORMAL) {
				if(timekeeper.lastTicks != 0) {
					int time_error = (timekeeper.currentTicks - timekeeper.lastTicks) - (1000 * intended_cycles/timekeeper.system_clock);
					if(timekeeper.frameCount == 100) {
#ifndef WRAPPER_MODE						
					  sprintf(titlebuf, "%s | %s | s: %.1f inc: %.1f err: %d\n", WINDOW_TITLE, currentRomFilePath.c_str(), timekeeper.time_scaling, timekeeper.scaling_increment, time_error);
						SDL_SetWindowTitle(mainWindow, titlebuf);
#endif
#if !defined(NDS_BUILD) && !defined(WASM_BUILD)
						profiler.fps = profiler.bufferFlipCount * 60 / 100;
#endif
						timekeeper.frameCount = 0;
#if !defined(NDS_BUILD) && !defined(WASM_BUILD)
						profiler.bufferFlipCount = 0;
#endif
					}
					bool overlong = time_error > 0;

					if(overlong == timekeeper.prev_overlong) {
						//scaling_increment = 1;
					} else if(timekeeper.scaling_increment > 1) {
						timekeeper.scaling_increment -= 1;
					}
					if((timekeeper.scaling_increment > 1) || (abs(time_error) > 2)) {
						if(overlong) {
							timekeeper.time_scaling -= timekeeper.scaling_increment;
						} else {
							timekeeper.time_scaling += timekeeper.scaling_increment;
						}
					}
					timekeeper.prev_overlong = overlong;

					if(timekeeper.time_scaling < 100) {
						timekeeper.time_scaling = 100;
					} else if(timekeeper.time_scaling > 2000) {
						timekeeper.time_scaling = 2000;
					}
				}
				timekeeper.lastTicks = timekeeper.currentTicks;
				timekeeper.frameCount++;
			}
#endif
			timekeeper.totalCyclesCount -= timekeeper.actual_cycles;
			timekeeper.totalCyclesCount += intended_cycles;
			timekeeper.cycles_since_vsync += intended_cycles;
			if(timekeeper.cycles_since_vsync >= timekeeper.cycles_per_vsync) {
				timekeeper.cycles_since_vsync -= timekeeper.cycles_per_vsync;
				if(system_state.dma_control & DMA_VSYNC_NMI_BIT) {
					cpu_core->NMI();
#if !defined(NDS_BUILD) && !defined(WASM_BUILD)
					if(vsyncProfileArmed) {
						profiler.DeepProfileStart();
						vsyncProfileArmed = false;
						vsyncProfileRunning = true;
					} else if(vsyncProfileRunning) {
						profiler.DeepProfileStop(loadedMemoryMap, SourceMap::singleton);
						vsyncProfileRunning = false;
					}
#endif
				}
#if !defined(NDS_BUILD) && !defined(WASM_BUILD)
				if(!profiler.measure_by_frameflip) {
					profiler.ResetTimers();
					profiler.last_blitter_activity = blitter->pixels_this_frame;
					blitter->pixels_this_frame = 0;
				}
#endif
			}
		} else {
#ifdef NDS_BUILD
				swiWaitForVBlank();
#else
				SDL_Delay(16);
#endif
		}
		blitter->CatchUp();
		

		if(EmulatorConfig::noSound) {
			AudioCoprocessor::fill_audio(AudioCoprocessor::singleton_acp_state, NULL, AudioCoprocessor::singleton_acp_state->samples_per_frame);
		}

#ifdef NDS_BUILD
		// NDS: menu toggle with L or R
		scanKeys();
		uint16_t ndsDown = keysDown();
		if (!ndsMenuOpen && (ndsDown & (KEY_L | KEY_R))) {
			ndsMenuOpen_();
		}

		if (ndsMenuOpen) {
			ndsMenuHandleInput();
			if (ndsMenu.needsRedraw) {
				ndsMenuDraw();
			}
			joysticks->updateNDS(true); // suppress game input
		} else {
			joysticks->updateNDS(false);
			// Check SELECT to reset
			if (ndsDown & KEY_SELECT) {
				resetQueued = 2;
			}
		}
#else
		while( SDL_PollEvent( &e ) != 0 )
        {
#ifndef WASM_BUILD

#ifdef WRAPPER_MODE
			if(true){
#else
			if(SDL_GetMouseFocus() == mainWindow) {
#endif
				ImGui::SetCurrentContext(main_imgui_ctx);
				ImPlot::SetCurrentContext(main_implot_ctx);
				ImGui_ImplSDL2_ProcessEvent(&e);
			}
			for (auto toolWindow : toolWindows) {
				toolWindow->HandleEvent(e);
			}

#ifndef WRAPPER_MODE
			if(ImGui::GetIO().WantCaptureKeyboard && ((e.type == SDL_KEYDOWN) || (e.type == SDL_KEYUP))) {
				continue;
			}
#endif //WRAPPER_MODE
#endif //WASM_BUILD
            //User requests quit
            if( e.type == SDL_QUIT )
            {
               running = false;
            } else if(e.type == SDL_WINDOWEVENT)
			{
				if(e.window.event == SDL_WINDOWEVENT_CLOSE) {
					SDL_Window* closedWindow = SDL_GetWindowFromID(e.window.windowID);
					if(closedWindow == mainWindow) {
						running = false;
					}
				}
			} else if((e.type == SDL_KEYDOWN) || (e.type == SDL_KEYUP)) {
				if(e.key.repeat == 0) {
					if((e.type == SDL_KEYUP) || !checkHotkey(e.key.keysym.sym)) {
						switch(e.key.keysym.sym) {
							case SDLK_LSHIFT:
								lshift = (e.type == SDL_KEYDOWN);
								break;
							case SDLK_RSHIFT:
								rshift = (e.type == SDL_KEYDOWN);
								break;							
							case SDLK_ESCAPE:
								#if !defined(DISABLE_ESC)
								if(e.type == SDL_KEYDOWN) {
									showMenu = !showMenu;
									menuOpening = showMenu;
	#ifdef WRAPPER_MODE
									setMenuMute(showMenu);
	#endif
								}
								#endif
								break;
	#ifndef WRAPPER_MODE
							case SDLK_BACKQUOTE:
								gofast = (e.type == SDL_KEYDOWN);
								break;
							case SDLK_r:
								//TODO add menu item for reset
								if(e.type == SDL_KEYDOWN) {
									if(lshift || rshift) {
										resetQueued = 2;
									} else {
										resetQueued = 1;
									}
								}
								break;
							case SDLK_o:
								if(e.type == SDL_KEYDOWN) {
									const char* rom_file_name = open_rom_dialog();
									if(rom_file_name) {
										LoadRomFile(rom_file_name);
									} else {
	#ifdef TINYFILEDIALOGS_H
										tinyfd_notifyPopup("Alert",
										"No ROM was loaded",
										"warning");
	#endif
									}
								}
								break;
	#endif
							default:
								if(!(showMenu || resetQueued)) {
									joysticks->update(&e);
								}
								break;
						}
					}
				}
            } else {
				if(!(showMenu || resetQueued))
					joysticks->update(&e);
			}
        }
#endif // NDS_BUILD

#ifdef NDS_BUILD
		// Only render on the last frame of a frame-skip batch
		if (ndsFrameCounter == ndsFrameSkip - 1) {
			refreshScreen();
		}
#else
		refreshScreen();
		SDL_UpdateWindowSurface(mainWindow);
#endif

#if !defined(WASM_BUILD) && !defined(NDS_BUILD)
		for (auto& window : toolWindows) {
			window->Draw();
		}

		auto const to_be_removed = std::partition(begin(toolWindows), end(toolWindows), [](auto w){ return w->IsOpen(); });
		std::for_each(to_be_removed, end(toolWindows), [](auto w) {
			delete w;
		});
		toolWindows.erase(to_be_removed, end(toolWindows));
#endif
		
	if(!running) {
#ifdef WASM_BUILD
		emscripten_cancel_main_loop();
#elif !defined(NDS_BUILD)
		for (auto& window : toolWindows) {
			delete window;
		}
		toolWindows.clear();

		ImPlot::DestroyContext(main_implot_ctx);
    	ImGui::DestroyContext(main_imgui_ctx);
#endif
#ifndef NDS_BUILD
    	SDL_DestroyRenderer(mainRenderer);
		SDL_DestroyWindow(mainWindow);
#endif
	}

	if(resetQueued) {
		paused = false;
		if(lshift || rshift || (resetQueued == 2)) {
			randomize_memory();
			randomize_vram();
		}
		cpu_core->Reset();
		cartridge_state.write_mode = false;
		joysticks->Reset();
		resetQueued = 0;
	}
	return running;
}

int main(int argC, char* argV[]) {
	srand(time(NULL));
	cartridge_state.rom = new uint8_t[1 << 21];

	const char* rom_file_name = NULL;

#ifdef NDS_BUILD
	// NDS: initialize hardware
	defaultExceptionHandler();
	Palette::InitRGB15LUT();

	// Set up top screen for bitmap output (main engine)
	videoSetMode(MODE_5_2D);
	vramSetBankA(VRAM_A_MAIN_BG);
	bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);

	// Set up bottom screen for debug console (sub engine)
	videoSetModeSub(MODE_0_2D);
	vramSetBankC(VRAM_C_SUB_BG);
	consoleDemoInit();
	// consoleDemoInit uses the sub engine by default

	printf("GameTank Emulator - NDS\n");

	// Initialize libfat for SD card access
	if (!fatInitDefault()) {
		printf("SD card init failed!\n");
	}

	// Clear DS top screen VRAM to black
	uint16_t* dsVram = (uint16_t*)BG_BMP_RAM(0);
	for (int i = 0; i < NDS_SCREEN_WIDTH * NDS_SCREEN_HEIGHT; i++) {
		dsVram[i] = RGB15(0, 0, 0) | BIT(15);
	}

	// ROM path from command line or default
	for (int argIdx = 1; argIdx < argC; ++argIdx) {
		if ((argV[argIdx])[0] == '-') {
			EmulatorConfig::parseArg(argV[argIdx]);
		} else if (!rom_file_name) {
			rom_file_name = argV[argIdx];
		}
	}
	// rom_file_name stays NULL if not provided — menu will open
#else
#ifdef EMBED_ROM_FILE
	rom_file_name = EMBED_ROM_FILE;
#else
	for(int argIdx = 1; argIdx < argC; ++argIdx) {
		if((argV[argIdx])[0] == '-') {
			EmulatorConfig::parseArg(argV[argIdx]);
		} else if(!rom_file_name) {
			rom_file_name = argV[argIdx];
		}
	}
#endif

#ifdef DEFAULT_ROM_PATH
	if(argC == 1) {
		int execPathLength = wai_getExecutablePath(NULL, 0, NULL);
		if(execPathLength != -1) {
			char* path = (char*)malloc(execPathLength + 1);
			wai_getExecutablePath(path, execPathLength, NULL);
			path[execPathLength] = '\0';
			std::filesystem::path execPath(path);
			free(path);
			std::filesystem::path romPath = execPath.parent_path() / DEFAULT_ROM_PATH;
			std::string romPathStr = (execPath.parent_path() / DEFAULT_ROM_PATH).string();
			rom_file_name = strdup(romPathStr.c_str());
		}
	}
#endif
#endif // NDS_BUILD

	for(int i = 0; i < cartridge_state.size; i++) {
		cartridge_state.rom[i] = 0;
	}

	joysticks = new JoystickAdapter();
	soundcard = new AudioCoprocessor();
	cpu_core = new mos6502(MemoryReadFast, MemoryWrite, CPUStopped, MemorySync);
	cpu_core->Reset();
	cartridge_state.write_mode = false;

#ifdef NDS_BUILD
	vRAM_Surface = NDS_CreateSurface(GT_WIDTH, GT_HEIGHT * 2);
	gRAM_Surface = NULL; // gRAM too big for NDS (2MB)
#else
	SDL_Init(SDL_INIT_VIDEO);
	atexit(SDL_Quit);

	bmpFont = SDL_CreateRGBSurfaceFrom(font_map, 128, 128, 32, 4 * 128, rmask, gmask, bmask, amask);

	vRAM_Surface = SDL_CreateRGBSurface(0, GT_WIDTH, GT_HEIGHT * 2, 32, rmask, gmask, bmask, amask);
	gRAM_Surface = SDL_CreateRGBSurface(0, GT_WIDTH, GT_HEIGHT * 32, 32, rmask, gmask, bmask, amask);

	SDL_SetColorKey(vRAM_Surface, SDL_FALSE, 0);
	SDL_SetColorKey(gRAM_Surface, SDL_FALSE, 0);

	mainWindow = SDL_CreateWindow(WINDOW_TITLE, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	mainRenderer = SDL_CreateRenderer(mainWindow, -1, EmulatorConfig::defaultRendererFlags);
	framebufferTexture = SDL_CreateTexture(mainRenderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, GT_WIDTH, GT_HEIGHT * 2);

#ifndef WASM_BUILD
	main_imgui_ctx = ImGui::CreateContext();
	main_implot_ctx = ImPlot::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigViewportsNoAutoMerge = true;
	io.IniFilename = NULL;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.Fonts->Flags |= ImFontAtlasFlags_NoBakedLines;
	ImGui::StyleColorsDark();
	ImGui_ImplSDL2_InitForSDLRenderer(mainWindow, mainRenderer);
	ImGui_ImplSDLRenderer2_Init(mainRenderer);
#endif

	#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	    rmask = 0xff000000;
	    gmask = 0x00ff0000;
	    bmask = 0x0000ff00;
	    amask = 0x000000ff;
	#else
	    rmask = 0x000000ff;
	    gmask = 0x0000ff00;
	    bmask = 0x00ff0000;
	    amask = 0xff000000;
	#endif
#endif // NDS_BUILD

	blitter = new Blitter(cpu_core, &timekeeper, &system_state, vRAM_Surface);
#ifndef NDS_BUILD
	randomize_memory();
	randomize_vram();
#endif

	if(rom_file_name) {
		if(LoadRomFile(rom_file_name) == -1) {
			rom_file_name = NULL;
		}
	}

	if(!rom_file_name) {
		paused = true;
#ifdef TINYFILEDIALOGS_H
		tinyfd_notifyPopup("Alert",
		"No ROM was loaded",
		"warning");
#endif
	}

#ifdef NDS_BUILD
	// If no ROM loaded, open menu immediately so user can browse
	if (!rom_file_name) {
		ndsMenuOpen_();
		ndsMenuDraw();
	} else {
		// Clear console for clean game display
		printf("\x1b[2J");
	}

	// Enable key repeat for menu navigation
	keysSetRepeat(25, 5);

	// NDS main loop — run multiple emulation frames per VBlank for speed
	while(running) {
		for (int f = 0; f < ndsFrameSkip && running; f++) {
			ndsFrameCounter = f;
			mainloop(0, NULL);
		}
		swiWaitForVBlank();
	}
#elif defined(WASM_BUILD)
	emscripten_request_animation_frame_loop(mainloop, 0);
#else
	SDL_RaiseWindow(mainWindow);
	while(running) {
		mainloop(0, NULL);
	}
	joysticks->SaveBindings();
#endif

#if !defined(WASM_BUILD) && !defined(NDS_BUILD)
	if(savingThread.joinable()) {
		savingThread.join();
	}
#endif
	return 0;
}
