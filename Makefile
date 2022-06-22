.SECONDARY: # this prevents object files from being removed
.DEFAULT_GOAL := all

_IGNORE0 := $(shell test -f Makefile.user || cp sample-Makefile.user Makefile.user)
include Makefile.user

ifeq ($(TARGET),esp32s2)
UF2 = 1
GCC_PREF = xtensa-esp32s2-elf
endif

ifeq ($(TARGET),esp32c3)
UF2 =
GCC_PREF = riscv32-esp-elf
endif

ifeq ($(GCC_PREF),)
$(error Define 'TARGET = esp32s2' or similar in Makefile.user)
endif

all: sdkconfig.defaults check-export refresh-version
	idf --ccache build
ifeq ($(UF2),1)
	$(MAKE) uf2
endif

sdkconfig.defaults: Makefile.user
	cat config/sdkconfig.$(TARGET) config/sdkconfig.common > sdkconfig.defaults

clean:
	rm -rf sdkconfig sdkconfig.defaults build

uf2:
	python3 scripts/uf2conv.py -b 0x0 build/espjd.bin -o build/espjd.uf2 -f ESP32S2

vscode:
	. $$IDF_PATH/export.sh ; idf  --ccache build

check-export:
	@if [ "X$$IDF_TOOLS_EXPORT_CMD" = X ] ; then echo Run: ; echo . $$IDF_PATH/export.sh ; exit 1 ; fi
	@test -f jacdac-c/README.md || git submodule update --init

f: flash
r: flash

flash: all
	idf  --ccache flash --port $(SERIAL_PORT)

mon:
	. $(IDF_PATH)/export.sh ; $(IDF_PATH)/tools/idf_monitor.py --port $(SERIAL_PORT) --baud 115200 build/espjd.elf

mon-2:
	idf monitor

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

dist: uf2
	mkdir -p build/dist
	cp build/espjd.uf2 build/dist/jacscript-esp32s2.uf2
	# also keep ELF file for addr2line
	cp build/espjd.elf build/dist/jacscript-esp32s2.elf

bump:
	sh ./scripts/bump.sh

refresh-version:
	@mkdir -p build
	echo 'const char app_fw_version[] = "v$(FW_VERSION)";' > build/version-tmp.c
	@diff build/version.c build/version-tmp.c >/dev/null 2>/dev/null || \
		(echo "refresh version"; cp build/version-tmp.c build/version.c)
