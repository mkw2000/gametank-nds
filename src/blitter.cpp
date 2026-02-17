#include "blitter.h"
#ifdef NDS_BUILD
#include "nds_platform.h"
#endif
#ifndef ITCM_CODE
#define ITCM_CODE
#endif

#ifndef NDS_BUILD
static inline void put_pixel32(SDL_Surface* surface, int x, int y, Uint32 pixel) {
    if (!surface) {
        return;
    }
    Uint32* pixels = (Uint32*)surface->pixels;
    pixels[(y * surface->w) + x] = pixel;
}
#endif

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
void ITCM_CODE Blitter::ProcessCycle() {
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
                int vramIndex = ((counterVY & 0x7F) << 7) | (counterVX & 0x7F) | vOffset;
                system_state->vram[vramIndex] = colorbus;
#ifdef NDS_BUILD
                vram_surface[vramIndex] = Palette::ConvertColorRGB15(colorbus);
#else
                put_pixel32(vram_surface, counterVX & 0x7F, (counterVY & 0x7F) + yShift, Palette::ConvertColor(vram_surface, colorbus));
#endif
            }
            ++pixels_this_frame;
        }
    }
}

// Fast path: batch process entire rows when blitter is in steady state
void ITCM_CODE Blitter::ProcessBatch(uint64_t cycles) {
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

#ifdef NDS_BUILD
    // NDS Fast Path: Linear copy (GCARRY set, xDir positive)
    // Most sprites and backgrounds use this mode.
    // We can avoid the expensive coordinate recalculation per pixel.
    if ((system_state->dma_control & DMA_GCARRY_BIT) && !xDir && !wrapX && !wrapY && !colorfill) {
        while (cycles > 0 && running && !init) {
            uint8_t rowPixels = counterW;
            if (rowPixels == 0) rowPixels = params[PARAM_WIDTH] & 0x7F;
            uint64_t toProcess = (cycles < rowPixels) ? cycles : rowPixels;
            if (toProcess == 0) { ProcessCycle(); cycles--; continue; }

            // Linear pointers
            // GX/GY are combined into linear GRAM address
            uint32_t gOffset = ComputeGOffset(system_state, counterGY, counterGX);
            uint32_t gx_linear = ((counterGY & 0x7F) << 7) | (counterGX & 0x7F);
            uint8_t* srcPtr = &system_state->gram[gx_linear | gOffset];
            
            // VX/VY linear VRAM address
            int vramIndex = ((counterVY & 0x7F) << 7) | (counterVX & 0x7F) | vOffset;
            uint8_t* dstPtr = &system_state->vram[vramIndex];
            uint16_t* dst15Ptr = &vram_surface[vramIndex];

            // Run the batch
            for (uint64_t i = 0; i < toProcess; i++) {
                uint8_t colorbus = *srcPtr++;
                if (!transparency || colorbus != 0) {
                    *dstPtr = colorbus;
                    *dst15Ptr = Palette::ConvertColorRGB15(colorbus);
                }
                dstPtr++;
                dst15Ptr++;
            }

            // Update counters
            counterW -= toProcess;
            counterGX += toProcess; // We know GCARRY is set, so simple add
            counterVX += toProcess;
            pixels_this_frame += toProcess;
            cycles -= toProcess;

            // Row end logic
            if (counterW == 0) {
                counterVY++;
                counterGY = GCARRY(counterGY); // Y might still wrap/carry normally
                --counterH;
                
                counterW = params[PARAM_WIDTH] & 0x7F;
                counterVX = params[PARAM_VX];
                counterGX = params[PARAM_GX];

                if (COPYDONE) {
                    running = false;
                    irq = true;
                    break;
                }
            }
        }
        return;
    }
#endif

#ifdef NDS_BUILD
    // NDS Fast Path: Colorfill
    if (colorfill && !wrapX && !wrapY) {
         uint8_t color8 = fillColor;
         uint16_t color16 = Palette::ConvertColorRGB15(color8);
         
         while (cycles > 0 && running && !init) {
            uint8_t rowPixels = counterW;
            if (rowPixels == 0) rowPixels = params[PARAM_WIDTH] & 0x7F;
            uint64_t toProcess = (cycles < rowPixels) ? cycles : rowPixels;
            if (toProcess == 0) { ProcessCycle(); cycles--; continue; }

            int vramIndex = ((counterVY & 0x7F) << 7) | (counterVX & 0x7F) | vOffset;
            uint8_t* dstPtr = &system_state->vram[vramIndex];
            uint16_t* dst15Ptr = &vram_surface[vramIndex];

            for(uint64_t i = 0; i < toProcess; i++) {
                *dstPtr++ = color8;
                *dst15Ptr++ = color16;
            }
            
            counterW -= toProcess;
            counterVX += toProcess;
            counterGX += toProcess; 
            pixels_this_frame += toProcess;
            cycles -= toProcess;

            if (counterW == 0) {
                counterVY++;
                counterGY = GCARRY(counterGY); 
                --counterH;
                
                counterW = params[PARAM_WIDTH] & 0x7F;
                counterVX = params[PARAM_VX];
                counterGX = params[PARAM_GX];
                
                if (COPYDONE) {
                    running = false;
                    irq = true;
                    break;
                }
            }
        }
        return;
    }

    // NDS Medium Path: Standard 1:1 copy without GCARRY flag
    // Many operations are simple copies but don't set the specific GCARRY bit.
    // If no scaling/flipping/wrapping/colorfill is active, we can treat it as linear.
    if (!xDir && !yDir && !wrapX && !wrapY && !colorfill) {
        // We need to ensure scale is actually 1:1. 
        // If GX/VX params are standard, they usually are. 
        // But for safety, let's just use the optimized loop which accounts for this 
        // by manually incrementing the linear pointers.
        
        while (cycles > 0 && running && !init) {
            uint8_t rowPixels = counterW;
            if (rowPixels == 0) rowPixels = params[PARAM_WIDTH] & 0x7F;
            uint64_t toProcess = (cycles < rowPixels) ? cycles : rowPixels;
            if (toProcess == 0) { ProcessCycle(); cycles--; continue; }

            // Base addresses for this row
            uint32_t gOffset = ComputeGOffset(system_state, counterGY, counterGX);
            uint32_t gx_linear = ((counterGY & 0x7F) << 7) | (counterGX & 0x7F);
            uint8_t* srcPtr = &system_state->gram[gx_linear | gOffset];
            
            int vramIndex = ((counterVY & 0x7F) << 7) | (counterVX & 0x7F) | vOffset;
            uint8_t* dstPtr = &system_state->vram[vramIndex];
            uint16_t* dst15Ptr = &vram_surface[vramIndex];
            
            // Check if we can just memcpy? 
            // Only if transparency is off.
            // But usually transparency is ON.
            
            for(uint64_t i = 0; i < toProcess; i++) {
                // Read
                uint8_t colorbus = *srcPtr; // Linear increment implies we just use ++
                srcPtr++; // GX increments by 1
                
                // Write
                if (!transparency || colorbus != 0) {
                    *dstPtr = colorbus;
                    *dst15Ptr = Palette::ConvertColorRGB15(colorbus);
                }
                dstPtr++; // VX increments by 1
                dst15Ptr++;
            }
            
            // Update counters manually as if we ran the state machine
            counterW -= toProcess;
            counterVX += toProcess;
            counterGX += toProcess; // We assume 1:1 here.
            pixels_this_frame += toProcess;
            cycles -= toProcess;

            // Row end logic
            if (counterW == 0) {
                counterVY++;
                counterGY++; // Y increments by 1
                 // Handle Y banking/wrapping if needed, but we checked !wrapY
                 // However, ComputeGOffset handles banking bits 
                --counterH;
                
                counterW = params[PARAM_WIDTH] & 0x7F;
                counterVX = params[PARAM_VX];
                counterGX = params[PARAM_GX];
                
                if (COPYDONE) {
                    running = false;
                    irq = true;
                    // break outer loop to handle completion
                    break;
                }
            }
        }
        return;
    }
#endif

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

            // Write to VRAM if conditions met (ProcessBatch is fast path - no suppress check here)
            if((transparency || colorbus != 0)
                && !((counterVX & 0x80) && wrapX)
                && !((counterVY & 0x80) && wrapY)) {
                int vramIndex = ((counterVY & 0x7F) << 7) | (counterVX & 0x7F) | vOffset;
                system_state->vram[vramIndex] = colorbus;
#ifdef NDS_BUILD
                vram_surface[vramIndex] = Palette::ConvertColorRGB15(colorbus);
#else
                put_pixel32(vram_surface, counterVX & 0x7F, (counterVY & 0x7F) + yShift, Palette::ConvertColor(vram_surface, colorbus));
#endif
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

void ITCM_CODE Blitter::CatchUp(uint64_t cycles) {
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
