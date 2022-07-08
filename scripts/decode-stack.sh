#!/bin/sh

while read LINE ; do
  for w in $LINE ; do
    echo $w
  done
done | riscv32-esp-elf-addr2line -pf -e build/espjd.elf | grep -v '?? ??:0'
