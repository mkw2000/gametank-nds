#!/bin/bash
rm -rf build_nds gametank-nds.elf gametank-nds.nds
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/opt/devkitpro/devkitARM
export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH
make
