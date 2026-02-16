#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "emulator_config.h"

bool EmulatorConfig::noSound = false;
bool EmulatorConfig::noJoystick = false;
bool EmulatorConfig::noSave = false;
#ifdef NDS_BUILD
Uint32 EmulatorConfig::defaultRendererFlags = 0;
#else
Uint32 EmulatorConfig::defaultRendererFlags = SDL_RENDERER_ACCELERATED;
#endif
char *EmulatorConfig::xorFile = NULL;

void EmulatorConfig::parseArg(const char* arg) {
    if(strcmp(arg, "--nosound") == 0) {
        noSound = true;
        return;
    }

#ifndef NDS_BUILD
    if(strcmp(arg, "--softrender") == 0) {
        defaultRendererFlags = SDL_RENDERER_SOFTWARE;
        return;
    }
#endif

    if(strcmp(arg, "--nojoystick") == 0) {
        noJoystick = true;
        return;
    }

    const char *xorFilePrefix = "--xorFile=";
    if(strncmp(arg, xorFilePrefix, strlen(xorFilePrefix)) == 0) {
      // TODO memory allocated here, need to clean up
      const char* src = arg + strlen(xorFilePrefix);
      xorFile = (char*)malloc(strlen(src) + 1);
      if(xorFile) strcpy(xorFile, src);
      return;
    }


    printf("Unrecognized option %s\n", arg);
}
