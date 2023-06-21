#include "jdesp.h"
#include "esp_partition.h"
#include "esp_flash.h"
#include "spi_flash_mmap.h"

uint32_t flash_size, flash_base;
int32_t flash_offset;

void flash_init(void) {
    const esp_partition_t *part = esp_partition_find_first(0x8A, 0x01, NULL);
    JD_ASSERT(part != NULL);
    JD_ASSERT((part->address & 0xffff) == 0);
    JD_ASSERT((part->size & (JD_FLASH_PAGE_SIZE - 1)) == 0);

    const void *rd_part;
    spi_flash_mmap_handle_t map;
    CHK(spi_flash_mmap(part->address, part->size, SPI_FLASH_MMAP_DATA, &rd_part, &map));

    flash_size = part->size;
    flash_base = (uint32_t)rd_part;
    flash_offset = part->address - flash_base;

    DMESG("fstor at %x -> %p (%ukB)", (unsigned)part->address, rd_part,
          (unsigned)(flash_size >> 10));
}

static uint32_t flash_addr(void *addr) {
    JD_ASSERT(flash_offset != 0);
    return (uint32_t)addr + flash_offset;
}

void flash_erase(void *page_addr) {
    CHK(esp_flash_erase_region(NULL, flash_addr(page_addr), JD_FLASH_PAGE_SIZE));
}

void flash_program(void *dst, const void *src, uint32_t len) {
    CHK(esp_flash_write(NULL, src, flash_addr(dst), len));
}

void flash_sync(void) {}
