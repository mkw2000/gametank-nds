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

// Inline helper for computing GRAM address - extracted for reuse
static inline uint32_t ComputeGOffset(SystemState* system_state, uint8_t counterGY, uint8_t counterGX) {
    return ((system_state->banking & BANK_GRAM_MASK) << 16) +
           (!!(counterGY & 0x80) << 15) +
           (!!(counterGX & 0x80) << 14);
}

// Process a single blitter cycle - the core state machine
void Blitter::ProcessCycle() {
    //PHASE 0: Decrement Width Counter or Load if INIT
    if(running) {
        --counterW;
    }
    if(init) {
        counterW = params[PARAM_WIDTH] & 0x7F;
    }

    //PHASE 1: Update counters
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

    //PHASE 2: Set RUNNING, reload width counter
    running = running || init;
    if(ROWCOMPLETE) counterW = params[PARAM_WIDTH] & 0x7F;

    //PHASE 3: Framebuffer write strobe
    init = trigger;
    trigger = trigger && !init;

    if(running) {
        // Apply direction flips for GX/GY
        uint8_t gx = XDIR ? ~counterGX : counterGX;
        uint8_t gy = YDIR ? ~counterGY : counterGY;
        gram_mid_bits = (!!(gy & 0x80) * 2) + (!!(gx & 0x80) * 1);

        uint8_t colorbus;
        if(system_state->dma_control & DMA_COLORFILL_ENABLE_BIT) {
            colorbus = ~(params[PARAM_COLOR]);
        } else {
            uint32_t gOffset = ComputeGOffset(system_state, gy, gx);
            colorbus = system_state->gram[((gy & 0x7F) << 7) | (gx & 0x7F) | gOffset];
        }

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

// Fast path: batch process entire rows when blitter is in steady state
void Blitter::ProcessBatch(uint64_t cycles) {
    // Pre-compute constants for the batch
    const bool xDir = XDIR;
    const bool yDir = YDIR;
    const bool transparency = system_state->dma_control & DMA_TRANSPARENCY_BIT;
    const bool colorfill = system_state->dma_control & DMA_COLORFILL_ENABLE_BIT;
    const bool wrapX = system_state->banking & BANK_WRAPX_MASK;
    const bool wrapY = system_state->banking & BANK_WRAPY_MASK;
    const bool vramPage = system_state->banking & BANK_VRAM_MASK;
    const uint8_t fillColor = colorfill ? ~(params[PARAM_COLOR]) : 0;
    const int yShift = vramPage ? 128 : 0;
    const int vOffset = yShift << 7;

    while(cycles > 0 && running && !init) {
        // How many pixels remaining in current row?
        uint8_t rowPixels = counterW;
        if(rowPixels == 0) rowPixels = params[PARAM_WIDTH] & 0x7F;

        // Clamp to available cycles
        uint64_t toProcess = (cycles < rowPixels) ? cycles : rowPixels;
        if(toProcess == 0) {
            // Process one cycle normally (edge cases like row end)
            ProcessCycle();
            cycles--;
            continue;
        }

        // Batch process 'toProcess' pixels in this row
        for(uint64_t i = 0; i < toProcess; i++) {
            // Decrement width counter
            --counterW;

            // Update position counters
            ++counterVX;
            counterGX = GCARRY(counterGX);

            // Compute GX/GY with direction
            uint8_t gx = xDir ? ~counterGX : counterGX;
            uint8_t gy = yDir ? ~counterGY : counterGY;

            // Read color from GRAM or colorfill
            uint8_t colorbus;
            if(colorfill) {
                colorbus = fillColor;
            } else {
                uint32_t gOffset = ComputeGOffset(system_state, gy, gx);
                colorbus = system_state->gram[((gy & 0x7F) << 7) | (gx & 0x7F) | gOffset];
            }

            // Write to VRAM if conditions met
            if((transparency || colorbus != 0)
                && !((counterVX & 0x80) && wrapX)
                && !((counterVY & 0x80) && wrapY)) {
                system_state->vram[((counterVY & 0x7F) << 7) | (counterVX & 0x7F) | vOffset] = colorbus;
                put_pixel32(vram_surface, counterVX & 0x7F, (counterVY & 0x7F) + yShift, Palette::ConvertColor(vram_surface, colorbus));
            }
            ++pixels_this_frame;
        }

        cycles -= toProcess;

        // Check if row completed
        if(counterW == 0) {
            // End of row processing - do what ProcessCycle would do at row end
            ++counterVY;
            counterGY = GCARRY(counterGY);
            --counterH;

            // Reload for next row
            counterW = params[PARAM_WIDTH] & 0x7F;
            counterVX = params[PARAM_VX];
            counterGX = params[PARAM_GX];

            if(COPYDONE) {
                running = false;
                irq = true;
                break;
            }
        }
    }

    // Process remaining cycles one at a time (init/done states, etc)
    while(cycles-- > 0 && (running || init)) {
        ProcessCycle();
    }
}

void Blitter::CatchUp(uint64_t cycles) {
    if(cycles == 0) {
        cycles = timekeeper->totalCyclesCount - last_updated_cycle;
    }
    last_updated_cycle = timekeeper->totalCyclesCount;

    // Use fast batch processing when in steady running state
    // Steady state: running=true, init=false, and not about to finish
    if(running && !init && !trigger) {
        ProcessBatch(cycles);
    } else {
        // Slow path: cycle-by-cycle for init, trigger, or done states
        while(cycles--) {
            ProcessCycle();
        }
    }
}
