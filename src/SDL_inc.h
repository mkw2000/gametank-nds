#ifdef NDS_BUILD
#include "nds_platform.h"
#elif defined(_WIN32)
#include <SDL.h>
#elif __APPLE__
#include "SDL.h"
#else
#include <SDL2/SDL.h>
#endif