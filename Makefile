#---------------------------------------------------------------------------------
# VN3DS - devkitARM / libctru build (based on the devkitPro 3DS template)
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET      := VN3DS
BUILD       := build
SOURCES     := source/core source/ctr source/third_party/faad2 source/third_party/wavpack
DATA        := data
INCLUDES    := source/third_party/faad2 source/third_party/wavpack
GRAPHICS    :=
GFXBUILD    := $(BUILD)
ROMFS       := romfs

VERSION_MAJOR   := 1
VERSION_MINOR   := 0
VERSION_MICRO   := 0
APP_VERSION     := $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_MICRO)

APP_TITLE       := VN3DS
APP_DESCRIPTION := VNDS visual novel player (DS, Vita, zip layouts)
APP_AUTHOR      := Pedro8b
ICON            := icon.png

#---------------------------------------------------------------------------------
ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

LIBDEFS  := -DHAVE_INTTYPES_H=1 -DHAVE_MEMCPY=1 -DHAVE_STRING_H=1 -DHAVE_STRINGS_H=1 \
            -DHAVE_SYS_STAT_H=1 -DHAVE_SYS_TYPES_H=1 -DHAVE_LRINTF=1 '-DPACKAGE_VERSION="2.11.1"' \
            -DAPPLY_DRC -DENABLE_LEGACY -DENABLE_DSD

CFLAGS   := -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH) $(LIBDEFS) \
            -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable
CFLAGS   += $(INCLUDE) -D__3DS__ '-DAPP_VERSION="$(APP_VERSION)"'
CXXFLAGS := $(CFLAGS) -std=gnu++17 -fno-rtti -fno-exceptions
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS     := -lcitro2d -lcitro3d -lctru -lvorbisidec -logg -llua5.1 -lz -lm
LIBDIRS  := $(PORTLIBS) $(CTRULIB)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES    := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD   := $(CXX)
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES   := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES   := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS := $(if $(NO_SMDH),,$(OUTPUT).smdh)
export APP_ICON := $(TOPDIR)/$(ICON)
ifeq ($(strip $(NO_SMDH)),)
export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif
ifneq ($(ROMFS),)
export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: all clean

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx : $(OUTPUT).elf $(_3DSXDEPS)
$(OUTPUT).elf  : $(OFILES)

%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d
#---------------------------------------------------------------------------------
endif
