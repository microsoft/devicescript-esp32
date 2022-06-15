#include "jdesp.h"

#include "sdmmc_cmd.h"

#include "driver/sdspi_host.h"
#include "driver/sdmmc_host.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"

static const char *TAG = "sd";

#define PIN_NUM_MISO 37
#define PIN_NUM_MOSI 35
#define PIN_NUM_CLK 36
#define PIN_NUM_CS 38

#if CONFIG_IDF_TARGET_ESP32S2
#define SPI_DMA_CHAN host.slot
#elif CONFIG_IDF_TARGET_ESP32C3
#define SPI_DMA_CHAN SPI_DMA_CH_AUTO
#else
#define SPI_DMA_CHAN 1
#endif

void init_sdcard(void) {
    esp_err_t ret;

    sdmmc_card_t *card;
    ESP_LOGI(TAG, "Initializing SD card");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    CHK(spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CHAN));

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    BYTE pdrv = FF_DRV_NOT_USED;
    CHK(ff_diskio_get_drive(&pdrv));
    JD_ASSERT(pdrv == 0);

    card = jd_alloc(sizeof(sdmmc_card_t));

    CHK(host.init());

    CHK(sdspi_host_init_device(&slot_config, &host.slot));

    ret = sdmmc_card_init(&host, card);
    if (ret != 0) {
        jd_free(card);
        ESP_LOGW(TAG, "Failed to initialize SD card");
        return;
    }

    ff_diskio_register_sdmmc(pdrv, card);

    ESP_LOGI(TAG, "SD card initialized");

    jd_lstore_init();
}
