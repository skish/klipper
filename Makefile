# Klipper build system
#
# Copyright (C) 2016  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.

# Output directory
OUT=out/

# Kconfig includes
export HOSTCC             := $(CC)
export CONFIG_SHELL       := sh
export KCONFIG_AUTOHEADER := autoconf.h
export KCONFIG_CONFIG     := $(CURDIR)/.config
-include $(KCONFIG_CONFIG)

# Common command definitions
CC=$(CROSS_PREFIX)gcc
AS=$(CROSS_PREFIX)as
LD=$(CROSS_PREFIX)ld
OBJCOPY=$(CROSS_PREFIX)objcopy
OBJDUMP=$(CROSS_PREFIX)objdump
STRIP=$(CROSS_PREFIX)strip
CPP=cpp
PYTHON=python

# Source files
src-y =
dirs-y = src

# Default compiler flags
cc-option=$(shell if test -z "`$(1) $(2) -S -o /dev/null -xc /dev/null 2>&1`" \
    ; then echo "$(2)"; else echo "$(3)"; fi ;)

CFLAGS-y := -I$(OUT) -Isrc -I$(OUT)board-generic/ -O2 -MD -g \
    -Wall -Wold-style-definition $(call cc-option,$(CC),-Wtype-limits,) \
    -ffunction-sections -fdata-sections
CFLAGS-y += -flto -fwhole-program -fno-use-linker-plugin

LDFLAGS-y := -Wl,--gc-sections -fno-whole-program

CPPFLAGS = -I$(OUT) -P -MD -MT $@

CFLAGS = $(CFLAGS-y)
LDFLAGS = $(LDFLAGS-y)

# Default targets
target-y := $(OUT)klipper.elf

all:

# Run with "make V=1" to see the actual compile commands
ifdef V
Q=
else
Q=@
MAKEFLAGS += --no-print-directory
endif

# Include board specific makefile
include src/Makefile
-include src/$(patsubst "%",%,$(CONFIG_BOARD_DIRECTORY))/Makefile

################ Common build rules

$(OUT)%.o: %.c $(OUT)autoconf.h $(OUT)board-link
	@echo "  Compiling $@"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

################ Main build rules

$(OUT)board-link: $(KCONFIG_CONFIG)
	@echo "  Creating symbolic link $(OUT)board"
	$(Q)mkdir -p $(addprefix $(OUT), $(dirs-y))
	$(Q)touch $@
	$(Q)ln -Tsf $(PWD)/src/$(CONFIG_BOARD_DIRECTORY) $(OUT)board
	$(Q)mkdir -p $(OUT)board-generic
	$(Q)ln -Tsf $(PWD)/src/generic $(OUT)board-generic/board

$(OUT)declfunc.lds: src/declfunc.lds.S
	@echo "  Precompiling $@"
	$(Q)$(CPP) $(CPPFLAGS) -D__ASSEMBLY__ $< -o $@

$(OUT)klipper.o: $(patsubst %.c, $(OUT)src/%.o,$(src-y)) $(OUT)declfunc.lds
	@echo "  Linking $@"
	$(Q)$(CC) $(CFLAGS) -Wl,-r -Wl,-T,$(OUT)declfunc.lds -nostdlib $(patsubst %.c, $(OUT)src/%.o,$(src-y)) -o $@

$(OUT)compile_time_request.o: $(OUT)klipper.o ./scripts/buildcommands.py
	@echo "  Building $@"
	$(Q)$(OBJCOPY) -j '.compile_time_request' -O binary $< $(OUT)klipper.o.compile_time_request
	$(Q)$(PYTHON) ./scripts/buildcommands.py -d $(OUT)klipper.dict $(OUT)klipper.o.compile_time_request $(OUT)compile_time_request.c
	$(Q)$(CC) $(CFLAGS) -c $(OUT)compile_time_request.c -o $@

$(OUT)klipper.elf: $(OUT)klipper.o $(OUT)compile_time_request.o
	@echo "  Linking $@"
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

################ Kconfig rules

define do-kconfig
$(Q)mkdir -p $(OUT)/scripts/kconfig/lxdialog
$(Q)mkdir -p $(OUT)/include/config
$(Q)$(MAKE) -C $(OUT) -f $(CURDIR)/scripts/kconfig/Makefile srctree=$(CURDIR) src=scripts/kconfig obj=scripts/kconfig Q=$(Q) Kconfig=$(CURDIR)/src/Kconfig $1
endef

$(OUT)autoconf.h : $(KCONFIG_CONFIG) ; $(call do-kconfig, silentoldconfig)
$(KCONFIG_CONFIG): src/Kconfig ; $(call do-kconfig, olddefconfig)
%onfig: ; $(call do-kconfig, $@)
help: ; $(call do-kconfig, $@)


################ Generic rules

# Make definitions
.PHONY : all clean distclean FORCE
.DELETE_ON_ERROR:

all: $(target-y)

clean:
	$(Q)rm -rf $(OUT)

distclean: clean
	$(Q)rm -f .config .config.old

-include $(OUT)*.d $(patsubst %,$(OUT)%/*.d,$(dirs-y))
