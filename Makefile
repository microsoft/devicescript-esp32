.SECONDARY: # this prevents object files from being removed
.DEFAULT_GOAL := all

IDF = idf.py

_IGNORE0 := $(shell test -f Makefile.user || cp sample-Makefile.user Makefile.user)
include Makefile.user

MON_PORT ?= $(SERIAL_PORT)

ifeq ($(TARGET),esp32s2-nojacs)
TARGET := esp32s2
TARGET_SUFF := -nojacs
COMPILE_OPTIONS += -DNO_JACSCRIPT=1
endif

ifeq ($(TARGET),esp32s2)
GCC_PREF = xtensa-esp32s2-elf
UF2 = 1
combine:
	python3 scripts/uf2conv.py -b 0x0 build/espjd.bin -o build/espjd.uf2 -f ESP32S2
endif

ifeq ($(TARGET),esp32c3)
UF2 =
GCC_PREF = riscv32-esp-elf
combine:
	esptool.py --chip $(TARGET) merge_bin \
		-o build/combined.bin \
		0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/espjd.bin
endif

ifeq ($(GCC_PREF),)
$(error Define 'TARGET = esp32s2' or similar in Makefile.user)
endif

prep: sdkconfig.defaults refresh-version 

all: check-export prep
	@if test -f sdkconfig ; then \
		if grep -q 'CONFIG_IDF_TARGET="$(TARGET)"' sdkconfig ; then echo target OK ; \
		else echo cleaning target... ; rm -rf build sdkconfig ; $(MAKE) refresh-version ; fi ; \
	fi
	$(IDF) --ccache build
	$(MAKE) combine

sdkconfig.defaults: Makefile.user
	cat config/sdkconfig.$(TARGET) config/sdkconfig.common > sdkconfig.defaults
	@mkdir -p build
	echo "idf_build_set_property(COMPILE_OPTIONS "$(COMPILE_OPTIONS)" APPEND)" > build/options.cmake

clean:
	rm -rf sdkconfig sdkconfig.defaults build

vscode:
	. $$IDF_PATH/export.sh ; $(IDF)  --ccache build

check-export:
	@if [ "X$$IDF_TOOLS_EXPORT_CMD" = X ] ; then echo Run: ; echo . $$IDF_PATH/export.sh ; exit 1 ; fi
	@test -f devicescript/runtime/jacdac-c/jacdac/README.md || git submodule update --init --recursive

f: flash
r: flash

flash: all
ifeq ($(UF2),1)
	$(IDF) --ccache flash --port $(SERIAL_PORT)
else
	esptool.py --chip $(TARGET) -p $(SERIAL_PORT) write_flash 0x0 build/combined.bin
endif

mon:
	. $(IDF_PATH)/export.sh ; $(IDF_PATH)/tools/idf_monitor.py --port $(MON_PORT) --baud 115200 build/espjd.elf

monu:
	. $(IDF_PATH)/export.sh ; $(IDF_PATH)/tools/idf_monitor.py --port $(SERIAL_PORT) --baud 115200 build/espjd.elf

mon-2:
	$(IDF) monitor

prep-gdb:
	echo > build/gdbinit
	echo "target remote :3333" >> build/gdbinit
	echo "set remote hardware-watchpoint-limit 2"  >> build/gdbinit

gdb: prep-gdb
	echo "mon halt"  >> build/gdbinit
	$(GCC_PREF)-gdb -x build/gdbinit build/espjd.elf

rst:
	echo "mon reset halt"  >> build/gdbinit
	echo "flushregs"  >> build/gdbinit
	echo "thb app_main"  >> build/gdbinit
	echo "c"  >> build/gdbinit
	$(GCC_PREF)-gdb -x build/gdbinit build/espjd.elf

FW_VERSION = $(shell sh jacdac-c/scripts/git-version.sh)

.PHONY: dist
dist: all
	mkdir -p dist
ifeq ($(UF2),1)
	cp build/espjd.uf2 dist/devicescript-$(TARGET)$(TARGET_SUFF).uf2
else
	cp build/combined.bin dist/devicescript-$(TARGET)$(TARGET_SUFF)-0x0.bin
endif
	# also keep ELF file for addr2line
	cp build/espjd.elf dist/devicescript-$(TARGET)$(TARGET_SUFF).elf

bump:
	sh ./scripts/bump.sh

refresh-version:
	@mkdir -p build
	echo 'const char app_fw_version[] = "v$(FW_VERSION)";' > build/version-tmp.c
	@diff build/version.c build/version-tmp.c >/dev/null 2>/dev/null || \
		(echo "refresh version"; cp build/version-tmp.c build/version.c)
