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
            uint32_t gOffset = cached_gram_base +
                    ((counterGY & 0x80) << 8) +  // !!(counterGY & 0x80) << 15 = (counterGY & 0x80) << 8
                    ((counterGX & 0x80) << 7);   // !!(counterGX & 0x80) << 14 = (counterGX & 0x80) << 7
            colorbus = system_state->gram[((counterGY & 0x7F) << 7) | (counterGX & 0x7F) | gOffset];
        }

        counterGX = xdir ? ~counterGX : counterGX;
        counterGY = ydir ? ~counterGY : counterGY;

        if(system_state->dma_control & DMA_COPY_ENABLE_BIT) {
            bool transparency = (system_state->dma_control & DMA_TRANSPARENCY_BIT) || (colorbus != 0);
            bool offscreen_x = (counterVX & 0x80) && cached_wrap_x;
            bool offscreen_y = (counterVY & 0x80) && cached_wrap_y;

            if(transparency && !offscreen_x && !offscreen_y) {
                int yShift = cached_vram_offset ? 128 : 0;
                int vOffset = yShift << 7;
                system_state->vram[((counterVY & 0x7F) << 7) | (counterVX & 0x7F) | vOffset] = colorbus;
                put_pixel32(vram_surface, counterVX & 0x7F, (counterVY & 0x7F) + yShift, Palette::ConvertColor(vram_surface, colorbus));
            }
            ++pixels_this_frame;
        }
    }
}

void Blitter::CatchUp(uint64_t cycles) {
    if(cycles == 0) {
        cycles = timekeeper->totalCyclesCount - last_updated_cycle;
    }
    last_updated_cycle = timekeeper->totalCyclesCount;

    uint8_t colorbus;

    // Fast path: batch process when in stable running state
    // This handles the common case where the blitter is actively copying pixels
    while(cycles > 0) {
        // Check if we can batch process (stable running state, no trigger pending)
        if(running && !init && !trigger && (counterW > 0) && (counterH > 0)) {
            // Calculate how many pixels we can batch
            // Process until end of current row or out of cycles
            uint32_t batchSize = counterW;
            if(batchSize > cycles) {
                batchSize = cycles;
            }

            // Batch process pixels in this row
            // Pre-calculate constant values for this batch
            bool xdir = XDIR;
            bool ydir = YDIR;
            uint8_t startVX = counterVX;
            uint8_t startGX = counterGX;
            uint8_t startGY = counterGY;
            uint8_t vy = counterVY & 0x7F;
            uint8_t gy = counterGY & 0x7F;
            int yShift = cached_vram_offset ? 128 : 0;
            int vOffset = yShift << 7;
            bool doTransparency = system_state->dma_control & DMA_TRANSPARENCY_BIT;
            bool doCopy = system_state->dma_control & DMA_COPY_ENABLE_BIT;
            bool colorFill = system_state->dma_control & DMA_COLORFILL_ENABLE_BIT;
            uint8_t fillColor = ~(params[PARAM_COLOR]);
            uint32_t gramBase = cached_gram_base + ((ydir ? ~gy : gy) & 0x80 ? 0x8000 : 0);

            for(uint32_t i = 0; i < batchSize; i++) {
                // Calculate positions
                uint8_t vx = (startVX + i + 1) & 0x7F;
                uint8_t gx = xdir ? ~(startGX + i + 1) : (startGX + i + 1);
                uint8_t gy_eff = ydir ? ~gy : gy;

                if(colorFill) {
                    colorbus = fillColor;
                } else {
                    uint32_t gOffset = cached_gram_base +
                            ((gy_eff & 0x80) << 8) +
                            ((gx & 0x80) << 7);
                    colorbus = system_state->gram[((gy & 0x7F) << 7) | (gx & 0x7F) | gOffset];
                }

                if(doCopy) {
                    bool transparency = doTransparency || (colorbus != 0);
                    bool offscreen = ((vx & 0x80) && cached_wrap_x) || ((vy & 0x80) && cached_wrap_y);

                    if(transparency && !offscreen) {
                        system_state->vram[(vy << 7) | vx | vOffset] = colorbus;
                        put_pixel32(vram_surface, vx, vy + yShift, Palette::ConvertColor(vram_surface, colorbus));
                    }
                    ++pixels_this_frame;
                }
            }

            // Update state after batch
            counterVX = (startVX + batchSize) & 0xFF;
            counterGX = (startGX + batchSize) & 0xFF;
            counterW -= batchSize;
            cycles -= batchSize;

            // Check if we completed the row
            if(counterW == 0) {
                // Row complete - need to update Y counters in next iteration
                counterW = params[PARAM_WIDTH] & 0x7F;
                counterVY++;
                counterGY = GCARRY(counterGY);
                counterH--;
                if(COPYDONE) {
                    running = false;
                    irq = true;
                }
            }
        } else {
            // Slow path: process one cycle at a time for complex states
            ProcessCycle(colorbus);
            --cycles;
        }
    }
}
