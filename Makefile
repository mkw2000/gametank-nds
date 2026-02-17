#---------------------------------------------------------------------------------
# GameTank Emulator - Nintendo DS Build
#---------------------------------------------------------------------------------
# Uses devkitARM's standard ds_rules for proper calico/libnds 2.0 support
#
# Build:   make
# Clean:   make clean
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/ds_rules

#---------------------------------------------------------------------------------
TARGET   := gametank-nds
BUILD    := build_nds
SOURCES  := src src/mos6502
INCLUDES := src
ARM7_DIR := arm7
PROJECT_ROOT ?= $(CURDIR)
ARM7_ELF := $(PROJECT_ROOT)/$(ARM7_DIR)/gametank-arm7.elf

# Use custom ARM7 binary instead of default ds7_maine.elf.
_ARM7_ELF := -7 $(ARM7_ELF)

#---------------------------------------------------------------------------------
# Source files — core emulation only, no desktop GUI
#---------------------------------------------------------------------------------
# We explicitly list source files to exclude desktop-only files
CPPFILES_EXPLICIT := \
	gte.cpp \
	blitter.cpp \
	audio_coprocessor.cpp \
	joystick_adapter.cpp \
	palette.cpp \
	emulator_config.cpp \
	font.cpp \
	timekeeper.cpp \
	mos6502.cpp

#---------------------------------------------------------------------------------
# Options for code generation
#---------------------------------------------------------------------------------
ARCH := -march=armv5te -mtune=arm946e-s -mthumb

CFLAGS   := -g -Wall -O2 -ffunction-sections -fdata-sections \
	-fomit-frame-pointer -ffast-math \
	$(ARCH) \
	-DARM9 -DNDS_BUILD \
	-DCPU_6502_STATIC -DCPU_6502_USE_LOCAL_HEADER -DCMOS_INDIRECT_JMP_FIX

# Prefer throughput on ARM9 over code size.
CFLAGS   := $(filter-out -O2,$(CFLAGS))
CFLAGS   += -O3 -flto

CFLAGS   += $(INCLUDE)
CXXFLAGS := $(CFLAGS) -std=c++17 -fno-rtti -fno-exceptions

ASFLAGS  := -g $(ARCH)
# Use ds9-legacy.specs explicitly to fix ENOSYS issues?
LDFLAGS  = -specs=$(DEVKITPRO)/calico/share/ds9-legacy.specs -g $(ARCH) -flto -Wl,-Map,$(notdir $*.map)

# Libraries — ds_rules will auto-add calico
LIBS     := -lfat -lfilesystem -lsysbase -lnds9 -lmm9

LIBDIRS  := $(LIBNDS)

#---------------------------------------------------------------------------------
# Build rules using ds_rules pattern
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export LD := $(CXX)
export PROJECT_ROOT := $(CURDIR)
export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH  := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

# Explicitly listed source files
export OFILES := $(CPPFILES_EXPLICIT:.cpp=.o)

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
	$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
	-I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean

$(BUILD):
	@$(MAKE) --no-print-directory -C $(CURDIR)/$(ARM7_DIR)
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@$(MAKE) --no-print-directory -C $(CURDIR)/$(ARM7_DIR) clean || true
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).nds

#---------------------------------------------------------------------------------
else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).nds: $(OUTPUT).elf $(ARM7_ELF)
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
