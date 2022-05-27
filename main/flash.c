#include "jdesp.h"
#include "nvs_flash.h"

static nvs_handle_t flash_handle;
static jacscriptmgr_cfg_t cfg;
static bool is_erased;
static uint8_t *max_write;

#define KEY "prog"

void flash_program(void *dst, const void *src, uint32_t len) {
    JD_ASSERT(cfg.program_base != NULL);
    ptrdiff_t diff = (uint8_t *)dst - (uint8_t *)cfg.program_base;
    JD_ASSERT(((uintptr_t)src & 3) == 0);
    JD_ASSERT(0 <= diff && diff + len <= cfg.max_program_size);
    JD_ASSERT((diff & 7) == 0);
    for (unsigned i = 0; i < len; ++i)
        JD_ASSERT(((uint8_t *)dst)[i] == 0xff);
    memcpy(dst, src, len);

    uint8_t *endp = (uint8_t *)dst + len;
    if (!max_write || endp > max_write)
        max_write = endp;
}

void flash_erase(void *page_addr) {
    JD_ASSERT(cfg.program_base != NULL);
    ptrdiff_t diff = (uint8_t *)page_addr - (uint8_t *)cfg.program_base;
    JD_ASSERT(0 <= diff && diff <= cfg.max_program_size - JD_FLASH_PAGE_SIZE);
    JD_ASSERT((diff & (JD_FLASH_PAGE_SIZE - 1)) == 0);
    memset(page_addr, 0xff, JD_FLASH_PAGE_SIZE);

    if (!is_erased) {
        is_erased = 1;
        max_write = cfg.program_base;
        DMESG("removing flash entry");
        nvs_erase_key(flash_handle, KEY);
        nvs_commit(flash_handle);
    }
}

void init_jacscript_manager(void) {
    cfg.max_program_size = 32 * 1024;
    cfg.program_base = jd_alloc(cfg.max_program_size);
    jacscriptmgr_init(&cfg);

    ESP_ERROR_CHECK(nvs_open("jacsflash", NVS_READWRITE, &flash_handle));
    size_t proglen = cfg.max_program_size;
    nvs_get_blob(flash_handle, KEY, cfg.program_base, &proglen);
}

void flash_sync(void) {
    unsigned sz = max_write - (uint8_t *)cfg.program_base;
    DMESG("writing %d bytes to flash", sz);
    nvs_set_blob(flash_handle, KEY, cfg.program_base, sz);
    nvs_commit(flash_handle);
    is_erased = 0;
}