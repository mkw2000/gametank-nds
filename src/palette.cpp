#include "palette.h"
#include "gametank_palette.h"
#ifdef NDS_BUILD
#include "nds_platform.h"
#endif

//Offset into palette multi table
// 0 - old inaccurate table
// 256 - straight from capture card
// 512 - scaled capture
int palette_select = PALETTE_SELECT_CAPTURE;

Uint32 Palette::ConvertColor(SDL_Surface* target, uint8_t index) {
    //if(index == 0) return SDL_MapRGB(target->format, 0, 0, 0);
	RGB_Color c = ((RGB_Color*)gt_palette_vals)[index + palette_select];
	Uint32 res = SDL_MapRGB(target->format, c.r, c.g, c.b);
	if(res == SDL_MapRGB(target->format, 0, 0, 0))
		return SDL_MapRGB(target->format, 1, 1, 1);
	return res;
}

#ifdef NDS_BUILD
uint16_t Palette::rgb15_lut[1024];

void Palette::InitRGB15LUT() {
    for(int i = 0; i < 1024; i++) {
        RGB_Color c = ((RGB_Color*)gt_palette_vals)[i];
        rgb15_lut[i] = RGB15(c.r >> 3, c.g >> 3, c.b >> 3) | BIT(15);
    }
}

uint16_t Palette::ConvertColorRGB15(uint8_t index) {
	return rgb15_lut[index + palette_select];
}
#endif