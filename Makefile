all: check-export
	idf  --ccache build

check-export:
	@if [ "X$$IDF_TOOLS_EXPORT_CMD" = X ] ; then echo Run: ; echo . $$IDF_PATH/export.sh ; exit 1 ; fi

f: flash
r: flash

flash:
	idf  --ccache flash

mon:
	idf monitor

prep-gdb:
	echo > build/gdbinit
	echo "target remote :3333" >> build/gdbinit
	echo "set remote hardware-watchpoint-limit 2"  >> build/gdbinit

gdb: prep-gdb
	echo "mon halt"  >> build/gdbinit
	xtensa-esp32-elf-gdb -x build/gdbinit build/espjd.elf

rst:
	echo "mon reset halt"  >> build/gdbinit
	echo "flushregs"  >> build/gdbinit
	echo "thb app_main"  >> build/gdbinit
	echo "c"  >> build/gdbinit
	xtensa-esp32-elf-gdb -x build/gdbinit build/espjd.elf
