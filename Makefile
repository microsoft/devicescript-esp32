.SECONDARY: # this prevents object files from being removed
.DEFAULT_GOAL := all
CLI = node devicescript/cli/devicescript 
JDC = devicescript/runtime/jacdac-c

IDF = idf.py

_IGNORE0 := $(shell test -f Makefile.user || cp sample-Makefile.user Makefile.user)
include Makefile.user

MON_PORT ?= $(SERIAL_PORT)

BL_OFF = $(shell grep CONFIG_BOOTLOADER_OFFSET_IN_FLASH= sdkconfig | sed -e 's/.*=//')

ifeq ($(TARGET),esp32s2)
GCC_PREF = xtensa-esp32s2-elf
endif

ifeq ($(TARGET),esp32c3)
GCC_PREF = riscv32-esp-elf
endif

BOARD ?= $(shell basename `ls boards/$(TARGET)/*.board.json | head -1` .board.json)

ifeq ($(GCC_PREF),)
$(error Define 'TARGET = esp32s2' or similar in Makefile.user)
endif

prep: devicescript/cli/built/devicescript-cli.cjs sdkconfig.defaults refresh-version 

all: build patch

.PHONY: build
build: check-export prep
	$(IDF) --ccache build
	$(MAKE) combine

sdkconfig.defaults: Makefile.user
	@if test -f sdkconfig ; then \
		if grep -q 'CONFIG_IDF_TARGET="$(TARGET)"' sdkconfig ; then echo target OK ; \
		else echo cleaning target... ; rm -rf build sdkconfig ; $(MAKE) refresh-version ; fi ; \
	fi
	cat boards/$(TARGET)/sdkconfig.$(TARGET) boards/sdkconfig.common > sdkconfig.defaults
	@mkdir -p build
	echo "idf_build_set_property(COMPILE_OPTIONS "$(COMPILE_OPTIONS)" APPEND)" > build/options.cmake

combine:
	esptool.py --chip $(TARGET) merge_bin \
		-o build/combined.bin \
		--target-offset $(BL_OFF) \
		$(BL_OFF) build/bootloader/bootloader.bin \
		0x8000 build/partition_table/partition-table.bin \
		0x10000 build/espjd.bin

patch:
	mkdir -p dist
	$(CLI) binpatch --bin build/combined.bin --elf build/espjd.elf --generic boards/$(TARGET)/*.board.json

clean:
	rm -rf sdkconfig sdkconfig.defaults build

vscode:
	. $$IDF_PATH/export.sh ; $(IDF)  --ccache build

check-export:
	@if [ "X$$IDF_TOOLS_EXPORT_CMD" = X ] ; then echo Run: ; echo . $$IDF_PATH/export.sh ; exit 1 ; fi
	@test -f $(JDC)/jacdac/README.md || git submodule update --init --recursive

devicescript/cli/built/devicescript-cli.cjs:
	cd devicescript && yarn
	cd devicescript && yarn build-fast

f: flash
r: flash

flash: all
	esptool.py --chip $(TARGET) -p $(SERIAL_PORT) write_flash \
		$(BL_OFF) dist/devicescript-$(TARGET)-$(BOARD)-$(BL_OFF).bin

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

FW_VERSION = $(shell sh $(JDC)/scripts/git-version.sh)

bump:
	sh ./scripts/bump.sh

refresh-version:
	@mkdir -p build
	echo 'const char app_fw_version[] = "v$(FW_VERSION)";' > build/version-tmp.c
	@diff build/version.c build/version-tmp.c >/dev/null 2>/dev/null || \
		(echo "refresh version"; cp build/version-tmp.c build/version.c)
