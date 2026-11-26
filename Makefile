# Mindustry 3DS - native port for Old 3DS / New 3DS (devkitARM + libctru + citro2d)
.SUFFIXES:
ifeq ($(strip $(DEVKITARM)),)
$(error "Set DEVKITARM (install devkitPro with the 3ds-dev group, or use the devkitpro/devkitarm container)")
endif
TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET      := mindustry-3ds
BUILD       := build/arm
SOURCES     := source build/gen
INCLUDES    := source build/gen
ROMFS       := romfs
APP_TITLE   := Mindustry 3DS
APP_DESCRIPTION := Native port of Mindustry v146
APP_AUTHOR  := Anuke / Mindustry 3DS contributors
APP_ICON    := $(TOPDIR)/meta/icon.png
PYTHON      ?= python3
UPSTREAM    ?= upstream/Mindustry

ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS   := -g -Wall -O2 -mword-relocations -ffunction-sections -fdata-sections $(ARCH) $(INCLUDE) -D__3DS__
CXXFLAGS := $(CFLAGS) -std=gnu++17 -fno-rtti -fno-exceptions
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map) -Wl,--gc-sections
LIBS     := -lcitro2d -lcitro3d -lctru -lm
LIBDIRS  := $(CTRULIB)

ifeq ($(MDT_INNER),)
#---------------------------------------------------------------------------------
# outer make: generate assets, then build the ELF/3DSX inside $(BUILD)
#---------------------------------------------------------------------------------
export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)
export LD       := $(CXX)
export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export _3DSXDEPS := $(OUTPUT).smdh
export _3DSXFLAGS := --smdh=$(OUTPUT).smdh --romfs=$(CURDIR)/$(ROMFS)

SRC_CPP := $(notdir $(wildcard source/*.cpp)) content_gen.cpp

.PHONY: all clean assets cia test

all: assets
	@mkdir -p $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile MDT_INNER=1 CPPFILES="$(SRC_CPP)"

# Everything generated from the pinned upstream Mindustry checkout
assets: build/gen/.stamp

build/gen/.stamp: $(wildcard tools/*.py)
	@test -f $(UPSTREAM)/core/src/mindustry/content/Blocks.java || { echo "upstream/Mindustry missing: git submodule update --init --depth 1"; exit 1; }
	$(PYTHON) tools/build_assets.py --upstream $(UPSTREAM) --gen build/gen --gfx build/gfx --romfs $(ROMFS)
	@mkdir -p $(ROMFS)/gfx
	@for t in build/gfx/*.t3s; do n=$$(basename $$t .t3s); echo "tex3ds $$n"; \
		(cd build/gfx && tex3ds -i $$n.t3s -o $(CURDIR)/$(ROMFS)/gfx/$$n.t3x) || exit 1; done
	@touch $@

cia: all
	bash tools/build_cia.sh

test:
	sh tests/run_host.sh

clean:
	@echo cleaning generated files
	@rm -rf build/arm build/gen build/gfx build/host build/gen-host build/romfs-host
	@rm -rf $(ROMFS)/gfx $(ROMFS)/maps $(ROMFS)/lang
	@rm -f $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf $(TARGET).cia

else
#---------------------------------------------------------------------------------
# inner make (runs in $(BUILD))
#---------------------------------------------------------------------------------
OFILES  := $(CPPFILES:.cpp=.o)
DEPENDS := $(OFILES:.o=.d)
.PHONY: all
all: $(OUTPUT).3dsx
$(OUTPUT).3dsx: $(OUTPUT).elf $(_3DSXDEPS)
$(OUTPUT).elf: $(OFILES)
-include $(DEPENDS)
endif
