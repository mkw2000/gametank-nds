#pragma once
#ifdef NDS_BUILD

#include <nds.h>
#include <fat.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

//=============================================================================
// SDL type stubs
// The GameTank emulator core writes to SDL_Surface pixel buffers and uses
// various SDL types throughout. Rather than rewriting every file, we provide
// minimal struct/type stubs that let the core code compile unchanged.
//=============================================================================

typedef uint32_t Uint32;
typedef uint16_t Uint16;
typedef uint8_t Uint8;

// SDL_AudioFormat / SDL_AudioDeviceID stubs
typedef uint32_t SDL_AudioFormat;
typedef int SDL_AudioDeviceID;

// Audio format constants referenced in audio_coprocessor.cpp
#define AUDIO_S8        0x8008
#define AUDIO_U8        0x0008
#define AUDIO_S16LSB    0x8010
#define AUDIO_S16MSB    0x9010
#define AUDIO_S16SYS    AUDIO_S16LSB
#define AUDIO_U16LSB    0x0010
#define AUDIO_U16MSB    0x1010
#define AUDIO_U16SYS    AUDIO_U16LSB
#define AUDIO_S32LSB    0x8020
#define AUDIO_S32MSB    0x9020
#define AUDIO_F32LSB    0x8120
#define AUDIO_F32MSB    0x9120

// Minimal SDL_PixelFormat stub
struct SDL_PixelFormat {
    uint8_t BitsPerPixel;
    uint8_t BytesPerPixel;
    uint32_t Rmask, Gmask, Bmask, Amask;
};

// Minimal SDL_Surface stub — emulator writes 32-bit RGBA pixels into these
struct SDL_Surface {
    int w, h;
    int pitch;
    void* pixels;
    SDL_PixelFormat* format;
};

// SDL_Rect stub
struct SDL_Rect {
    int x, y, w, h;
};

// SDL_Event stub — needed by joystick_adapter.h even though we won't use it on DS
struct SDL_Event {
    uint32_t type;
    struct { uint32_t windowID; uint8_t event; } window;
    struct { struct { int32_t sym; } keysym; uint8_t repeat; } key;
    struct { int32_t which; } jdevice;
    struct { uint8_t value; } jhat;
    struct { uint8_t button; } jbutton;
    struct { uint8_t axis; int16_t value; } jaxis;
};

// Dummy SDL event/key types
typedef int32_t SDL_Keycode;
typedef int32_t SDL_JoystickID;
struct SDL_Joystick;

// Minimal event type constants (won't be used at runtime on DS)
#define SDL_QUIT            0x100
#define SDL_WINDOWEVENT     0x200
#define SDL_KEYDOWN         0x300
#define SDL_KEYUP           0x301
#define SDL_JOYDEVICEADDED  0x605
#define SDL_JOYDEVICEREMOVED 0x606
#define SDL_JOYBUTTONDOWN   0x603
#define SDL_JOYBUTTONUP     0x604
#define SDL_JOYHATMOTION    0x602
#define SDL_JOYAXISMOTION   0x600
#define SDL_WINDOWEVENT_CLOSE 0

// Dummy SDLK constants for keys referenced in gte.cpp
#define SDLK_LSHIFT     0x400000e1
#define SDLK_RSHIFT     0x400000e5
#define SDLK_ESCAPE     0x0000001b
#define SDLK_BACKQUOTE  0x00000060
#define SDLK_r          0x00000072
#define SDLK_o          0x0000006f
#define SDLK_m          0x0000006d
#define SDLK_F6         0x4000003f
#define SDLK_F7         0x40000040
#define SDLK_F8         0x40000041
#define SDLK_F9         0x40000042
#define SDLK_F10        0x40000043
#define SDLK_F11        0x40000044
#define SDLK_F12        0x40000045

// SDL_MapRGB replacement — pack into 32-bit ARGB
static inline Uint32 SDL_MapRGB(SDL_PixelFormat* fmt, uint8_t r, uint8_t g, uint8_t b) {
    (void)fmt;
    return (0xFF << 24) | (r << 16) | (g << 8) | b;
}

// SDL color key stubs
#define SDL_TRUE 1
#define SDL_FALSE 0
static inline void SDL_SetColorKey(SDL_Surface* s, int flag, Uint32 key) {
    (void)s; (void)flag; (void)key;
}

// Surface creation/destruction
static inline SDL_Surface* NDS_CreateSurface(int w, int h) {
    SDL_Surface* s = (SDL_Surface*)malloc(sizeof(SDL_Surface));
    s->w = w;
    s->h = h;
    s->pitch = w * 4;
    s->pixels = malloc(w * h * 4);
    s->format = (SDL_PixelFormat*)malloc(sizeof(SDL_PixelFormat));
    s->format->BitsPerPixel = 32;
    s->format->BytesPerPixel = 4;
    s->format->Rmask = 0x00FF0000;
    s->format->Gmask = 0x0000FF00;
    s->format->Bmask = 0x000000FF;
    s->format->Amask = 0xFF000000;
    memset(s->pixels, 0, w * h * 4);
    return s;
}

static inline void NDS_FreeSurface(SDL_Surface* s) {
    if (s) {
        free(s->pixels);
        free(s->format);
        free(s);
    }
}

// File existence check (replaces std::filesystem::exists)
static inline bool file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// DS-specific screen constants
#define NDS_SCREEN_WIDTH  256
#define NDS_SCREEN_HEIGHT 192

// Convert 32-bit ARGB pixel to DS 15-bit RGB15
static inline uint16_t argb_to_rgb15(uint32_t argb) {
    uint8_t r = (argb >> 16) & 0xFF;
    uint8_t g = (argb >> 8) & 0xFF;
    uint8_t b = argb & 0xFF;
    return RGB15(r >> 3, g >> 3, b >> 3) | BIT(15);
}

#endif // NDS_BUILD
