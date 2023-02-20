.SECONDARY: # this prevents object files from being removed
.DEFAULT_GOAL := all
CLI = node devicescript/cli/devicescript 
JDC = devicescript/runtime/jacdac-c
BUILD = build
IDF = idf.py

_IGNORE0 := $(shell test -f Makefile.user || cp sample-Makefile.user Makefile.user)
include Makefile.user

MON_PORT ?= $(SERIAL_PORT)
ESPTOOL ?= esptool.py

BL_OFF = $(shell grep CONFIG_BOOTLOADER_OFFSET_IN_FLASH= sdkconfig | sed -e 's/.*=//')

ifeq ($(TARGET),esp32s2)
GCC_PREF = xtensa-esp32s2-elf
endif

ifeq ($(TARGET),esp32c3)
GCC_PREF = riscv32-esp-elf
endif

ifeq ($(TARGET),esp32)
GCC_PREF = xtensa-esp32-elf
endif

BOARD ?= $(shell basename `ls boards/$(TARGET)/*.board.json | head -1` .board.json)

ifeq ($(GCC_PREF),)
$(error Define 'TARGET = esp32s2' or similar in Makefile.user)
endif

prep: devicescript/cli/built/devicescript-cli.cjs sdkconfig.defaults refresh-version 

all: inner-build patch

inner-build: check-export prep
	$(IDF) --ccache build
	$(MAKE) combine

ci-build:
	$(MAKE) inner-build
	mv $(BUILD) build-$(TARGET)

ci-patch:
	$(MAKE) patch BUILD=build-$(TARGET)

sdkconfig.defaults: Makefile.user
	@if test -f sdkconfig ; then \
		if grep -q 'CONFIG_IDF_TARGET="$(TARGET)"' sdkconfig ; then echo target OK ; \
		else echo cleaning target... ; rm -rf $(BUILD) sdkconfig ; $(MAKE) refresh-version ; fi ; \
	fi
	cat boards/$(TARGET)/sdkconfig.$(TARGET) boards/sdkconfig.common > sdkconfig.defaults
	@mkdir -p $(BUILD)
	echo "idf_build_set_property(COMPILE_OPTIONS "$(COMPILE_OPTIONS)" APPEND)" > $(BUILD)/options.cmake

combine:
	$(ESPTOOL) --chip $(TARGET) merge_bin \
		-o $(BUILD)/combined.bin \
		--target-offset $(BL_OFF) \
		$(BL_OFF) $(BUILD)/bootloader/bootloader.bin \
		0x8000 $(BUILD)/partition_table/partition-table.bin \
		0x10000 $(BUILD)/espjd.bin

patch:
	mkdir -p dist
	$(CLI) binpatch --slug microsoft/jacdac-esp32 --bin $(BUILD)/combined.bin --elf $(BUILD)/espjd.elf --generic boards/$(TARGET)/*.board.json

clean:
	rm -rf sdkconfig sdkconfig.defaults $(BUILD)

vscode:
	. $$IDF_PATH/export.sh ; $(IDF) --ccache build

check-export:
	@if [ "X$$IDF_TOOLS_EXPORT_CMD" = X ] ; then echo Run: ; echo . $$IDF_PATH/export.sh ; exit 1 ; fi
	@test -f $(JDC)/jacdac/README.md || git submodule update --init --recursive

devicescript/cli/built/devicescript-cli.cjs:
	cd devicescript && yarn
	cd devicescript && yarn build-fast

f: flash
r: flash

flash: all
	$(ESPTOOL) --chip $(TARGET) -p $(SERIAL_PORT) write_flash \
		$(BL_OFF) dist/devicescript-$(TARGET)-$(BOARD)-$(BL_OFF).bin

mon:
	. $(IDF_PATH)/export.sh ; $(IDF_PATH)/tools/idf_monitor.py --port $(MON_PORT) --baud 115200 $(BUILD)/espjd.elf

monf:
	. $(IDF_PATH)/export.sh ; $(IDF_PATH)/tools/idf_monitor.py --port $(SERIAL_PORT) --baud 1500000 $(BUILD)/espjd.elf

monu:
	. $(IDF_PATH)/export.sh ; $(IDF_PATH)/tools/idf_monitor.py --port $(SERIAL_PORT) --baud 115200 $(BUILD)/espjd.elf

mon-2:
	$(IDF) monitor

prep-gdb:
	echo > $(BUILD)/gdbinit
	echo "target remote :3333" >> $(BUILD)/gdbinit
	echo "set remote hardware-watchpoint-limit 2"  >> $(BUILD)/gdbinit

gdb: prep-gdb
	echo "mon halt"  >> $(BUILD)/gdbinit
	$(GCC_PREF)-gdb -x $(BUILD)/gdbinit $(BUILD)/espjd.elf

rst:
	echo "mon reset halt"  >> $(BUILD)/gdbinit
	echo "flushregs"  >> $(BUILD)/gdbinit
	echo "thb app_main"  >> $(BUILD)/gdbinit
	echo "c"  >> $(BUILD)/gdbinit
	$(GCC_PREF)-gdb -x $(BUILD)/gdbinit $(BUILD)/espjd.elf

FW_VERSION = $(shell sh $(JDC)/scripts/git-version.sh)

bump:
	sh ./scripts/bump.sh

refresh-version:
	@mkdir -p $(BUILD)
	echo 'const char app_fw_version[] = "v$(FW_VERSION)";' > $(BUILD)/version-tmp.c
	@diff $(BUILD)/version.c $(BUILD)/version-tmp.c >/dev/null 2>/dev/null || \
		(echo "refresh version"; cp $(BUILD)/version-tmp.c $(BUILD)/version.c)
