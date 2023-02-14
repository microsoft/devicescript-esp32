#include "jdesp.h"

#include "sdmmc_cmd.h"

#include "hal/gpio_ll.h"
#include "driver/sdspi_host.h"
#include "driver/sdmmc_host.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"

static const char *TAG = "sd";

static uint8_t pin_mosi, pin_miso, pin_sck, pin_cs;

#define SET_SCK() gpio_ll_set_level(&GPIO, pin_sck, 1)
#define CLR_SCK() gpio_ll_set_level(&GPIO, pin_sck, 0)
#define GET_MISO() gpio_ll_get_level(&GPIO, pin_miso)

#if CONFIG_IDF_TARGET_ESP32S2
#define SPI_DMA_CHAN host.slot
#elif CONFIG_IDF_TARGET_ESP32C3
#define SPI_DMA_CHAN SPI_DMA_CH_AUTO
#else
#define SPI_DMA_CHAN 1
#endif

#define SPI_TX_BIT(n)                                                                              \
    gpio_ll_set_level(&GPIO, pin_mosi, b & (1 << n));                                              \
    SET_SCK();                                                                                     \
    CLR_SCK()

#define SPI_RX_BIT(n)                                                                              \
    SET_SCK();                                                                                     \
    if (GET_MISO())                                                                                \
        b |= (1 << n);                                                                             \
    CLR_SCK()

void spi_bb_init(void) {
    pin_setup_analog_input(pin_sck);
    pin_setup_output(pin_sck);
    pin_setup_analog_input(pin_mosi);
    pin_setup_output(pin_mosi);
    pin_setup_analog_input(pin_miso);
    pin_setup_input(pin_miso, PIN_PULL_UP);

    pin_setup_analog_input(pin_cs);
    pin_setup_output(pin_cs);
}

void spi_bb_tx(const void *data, unsigned len) {
    const uint8_t *p = data;
    while (len--) {
        uint32_t b = *p++;
        SPI_TX_BIT(7);
        SPI_TX_BIT(6);
        SPI_TX_BIT(5);
        SPI_TX_BIT(4);
        SPI_TX_BIT(3);
        SPI_TX_BIT(2);
        SPI_TX_BIT(1);
        SPI_TX_BIT(0);
    }
}

void spi_bb_set_cs(int val) {
    pin_set(pin_cs, val);
}

int spi_bb_get_miso(void) {
    return pin_get(pin_miso);
}

void spi_bb_rx(void *data, unsigned len) {
    uint8_t *p = data;
    // keep MOSI high
    gpio_ll_set_level(&GPIO, pin_mosi, 1);
    while (len--) {
        uint32_t b = 0;
        SPI_RX_BIT(7);
        SPI_RX_BIT(6);
        SPI_RX_BIT(5);
        SPI_RX_BIT(4);
        SPI_RX_BIT(3);
        SPI_RX_BIT(2);
        SPI_RX_BIT(1);
        SPI_RX_BIT(0);
        *p++ = b;
    }
}

void panic_dump_dmesg(void);

void init_sdcard(void) {
    esp_err_t ret;

    sdmmc_card_t *card;

    pin_miso = dcfg_get_pin("sd.pinMISO");
    pin_mosi = dcfg_get_pin("sd.pinMOSI");
    pin_sck = dcfg_get_pin("sd.pinSCK");
    pin_cs = dcfg_get_pin("sd.pinCS");

    if (pin_miso == NO_PIN || pin_mosi == NO_PIN || pin_sck == NO_PIN || pin_cs == NO_PIN) {
        ESP_LOGI(TAG, "skipping SD card - no config");
        return;
    }

#if defined(CONFIG_IDF_TARGET_ESP32C3)
    // 9 is a boot pin connected to button - we should not actively drive it high
    JD_ASSERT(pin_cs != 9 && pin_sck != 9 && pin_mosi != 9);
#endif

    ESP_LOGI(TAG, "Initializing SD card");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = pin_mosi,
        .miso_io_num = pin_miso,
        .sclk_io_num = pin_sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    CHK(spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CHAN));

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = pin_cs;
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
