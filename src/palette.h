#pragma once
#include "SDL_inc.h"
#include "gametank_palette.h"

#define PALETTE_SELECT_OLD 0
#define PALETTE_SELECT_CAPTURE 256
#define PALETTE_SELECT_SCALED 512
#define PALETTE_SELECT_HDMI 768

extern int palette_select;

typedef struct RGB_Color {
	uint8_t r, g, b;
} RGB_Color;

class Palette {
public:
#ifndef NDS_BUILD
    static Uint32 ConvertColor(SDL_Surface* target, uint8_t index);
#endif
    static inline uint16_t ConvertColorRGB15(uint8_t index) {
        return gt_palette_rgb555[index + palette_select];
    }
};
