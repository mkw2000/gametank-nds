#include "blitter.h"

Uint32 get_pixel32( SDL_Surface *surface, int x, int y )
{
    //Convert the pixels to 32 bit
    Uint32 *pixels = (Uint32 *)surface->pixels;

    //Get the requested pixel
    return pixels[ ( y * surface->w ) + x ];
}

void put_pixel32( SDL_Surface *surface, int x, int y, Uint32 pixel )
{
    if(!surface) return;
    //Convert the pixels to 32 bit
    Uint32 *pixels = (Uint32 *)surface->pixels;

    //Set the pixel
    pixels[ ( y * surface->w ) + x ] = pixel;
}

void Blitter::SetParam(uint8_t address, uint8_t value) {
    if((address % DMA_PARAMS_COUNT) == PARAM_TRIGGER) {
        trigger = value & 1;
        irq = false;
        cpu_core->ClearIRQ();
        if(trigger) {
            uint32_t cycles = ((params[Blitter::PARAM_HEIGHT] & 0x7F) * (params[Blitter::PARAM_WIDTH] & 0x7F));
            cpu_core->ScheduleIRQ(cycles, &(system_state->dma_control_irq));
            if(instant_mode) {
                CatchUp(cycles);
            }
        }
    } else {
        params[address % DMA_PARAMS_COUNT] = value;
    }
}

#define ROWCOMPLETE (counterW == 0)
#define XRELOAD (ROWCOMPLETE || init)
#define COPYDONE (counterH == 0)
#define XDIR (!!(params[PARAM_WIDTH] & 0x80))
#define YDIR (!!(params[PARAM_HEIGHT] & 0x80))
#define GCARRY(val) ((system_state->dma_control & DMA_GCARRY_BIT) ? val+1 : ((val & 0xF0) | ((val+1) & 0x0F)))

// Process a single blitter cycle - extracted for clarity and to enable batching
inline void Blitter::ProcessCycle(uint8_t &colorbus) {
    //PHASE 0: Decrement Width Counter or Load if INIT
    if(running) {
        --counterW;
    }

    if(init) {
        counterW = params[PARAM_WIDTH] & 0x7F;
    }

    //PHASE 1: Update position counters
    if(running) {
        ++counterVX;
        counterGX = GCARRY(counterGX);

        if(ROWCOMPLETE) {
            ++counterVY;
            counterGY = GCARRY(counterGY);
            --counterH;
        }
    }

    if(XRELOAD) {
        counterVX = params[PARAM_VX];
        counterGX = params[PARAM_GX];
    }

    if(init) {
        counterVY = params[PARAM_VY];
        counterGY = params[PARAM_GY];
        counterH = params[PARAM_HEIGHT] & 0x7F;
    }

    if(COPYDONE) {
        running = false;
        irq = true;
    }

    //PHASE 2: Reload width counter, set RUNNING
    running = running || init;
    if(ROWCOMPLETE) counterW = params[PARAM_WIDTH] & 0x7F;

    //PHASE 3: Framebuffer write strobe
    init = trigger;
    trigger = trigger && !init;

    if(running) {
        bool xdir = XDIR;
        bool ydir = YDIR;

        counterGX = xdir ? ~counterGX : counterGX;
        counterGY = ydir ? ~counterGY : counterGY;
        gram_mid_bits = ((counterGY & 0x80) ? 2 : 0) + ((counterGX & 0x80) ? 1 : 0);

        if(system_state->dma_control & DMA_COLORFILL_ENABLE_BIT) {
            colorbus = ~(params[PARAM_COLOR]);
        } else {
            uint32_t gOffset = ((system_state->banking & BANK_GRAM_MASK) << 16) +
                    (!!(counterGY & 0x80) << 15) +
                    (!!(counterGX & 0x80) << 14);
            colorbus = system_state->gram[((counterGY & 0x7F) << 7) | (counterGX & 0x7F) | gOffset];
        }

        counterGX = xdir ? ~counterGX : counterGX;
        counterGY = ydir ? ~counterGY : counterGY;

        if(system_state->dma_control & DMA_COPY_ENABLE_BIT) {
            if(((system_state->dma_control & DMA_TRANSPARENCY_BIT) || (colorbus != 0))
                && !((counterVX & 0x80) && (system_state->banking & BANK_WRAPX_MASK))
                && !((counterVY & 0x80) && system_state->banking & BANK_WRAPY_MASK)) {
                int yShift = (system_state->banking & BANK_VRAM_MASK) ? 128 : 0;
                int vOffset = yShift << 7;
                system_state->vram[((counterVY & 0x7F) << 7) | (counterVX & 0x7F) | vOffset] = colorbus;
                put_pixel32(vram_surface, counterVX & 0x7F, (counterVY & 0x7F) + yShift, Palette::ConvertColor(vram_surface, colorbus));
            }
            ++pixels_this_frame;
        }
    }
}

void Blitter::CatchUp(uint32_t cycles) {
    if(cycles == 0) {
        cycles = timekeeper->totalCyclesCount - last_updated_cycle;
    }
    last_updated_cycle = timekeeper->totalCyclesCount;

    uint8_t colorbus;
    while(cycles--) {
        ProcessCycle(colorbus);
    }
}
