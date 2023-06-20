#include "jdesp.h"
#include "jd_drivers.h"
#include "driver/spi_master.h"
#include "hal/spi_hal.h"
#include "services/interfaces/jd_spi.h"

#define BLOCK_SIZE 4096

static int mappin(uint8_t pin) {
    if (pin == NO_PIN)
        return -1;
    return pin;
}

#define SPI_HOST SPI2_HOST // seems OK on C3, S2 and ESP32

static spi_device_handle_t spi;
static spi_transaction_t trans;
static bool spi_in_use;

static void jd_spi_done_cb_outside_isr(spi_transaction_t *transp) {
    JD_ASSERT(spi_in_use);
    spi_transaction_t *rtrans;
    int ret = spi_device_get_trans_result(spi, &rtrans, 0);
    JD_ASSERT(ret == 0);
    JD_ASSERT(rtrans == transp);
    cb_t cb = transp->user;
    spi_in_use = false;
    cb();
}

static void jd_spi_done_cb(spi_transaction_t *transp) {
    JD_ASSERT(transp == &trans);
    if (transp->user == NULL)
        return; // polling transaction
    JD_ASSERT(spi_in_use);
    tim_worker_run((TaskFunction_t)jd_spi_done_cb_outside_isr, transp);
}

int jd_spi_init(const jd_spi_cfg_t *cfg) {
    if (spi_in_use)
        return -100;

    if (spi) {
        spi_bus_remove_device(spi);
        spi = NULL;
        spi_bus_free(SPI_HOST);
    }

    spi_bus_config_t buscfg = {
        .miso_io_num = mappin(cfg->miso),
        .mosi_io_num = mappin(cfg->mosi),
        .sclk_io_num = mappin(cfg->sck),
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BLOCK_SIZE,
    };

    DMESG("SPI init: miso=%d mosi=%d sck=%d hz=%u", buscfg.miso_io_num, buscfg.mosi_io_num,
          buscfg.sclk_io_num, (unsigned)cfg->hz);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = cfg->hz,
        .mode = cfg->mode,
        .spics_io_num = -1,
        .queue_size = 2,
        .post_cb = jd_spi_done_cb,
    };

    int r;

    r = spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (r)
        return r;

    r = spi_bus_add_device(SPI_HOST, &devcfg, &spi);
    if (r) {
        spi = NULL;
        spi_bus_free(SPI_HOST);
        return r;
    }

    return 0;
}

bool jd_spi_is_ready(void) {
    return spi != NULL && !spi_in_use;
}

unsigned jd_spi_max_block_size(void) {
    return BLOCK_SIZE;
}

static void call_fn(void *f) {
    cb_t ff = f;
    if (ff)
        ff();
}

int jd_spi_xfer(const void *txdata, void *rxdata, unsigned numbytes, cb_t done_fn) {
    // JD_ASSERT(!target_in_irq()); this can in fact be invoked from done_fn()...
    JD_ASSERT(done_fn != NULL);

    if (!jd_spi_is_ready())
        return -1;

    if (numbytes > BLOCK_SIZE)
        return -2;

    if (numbytes == 0)
        goto sync_ok;

    JD_ASSERT(txdata || rxdata);

    memset(&trans, 0, sizeof(trans));
    trans.length = 8 * numbytes;
    trans.tx_buffer = txdata;
    trans.rx_buffer = rxdata;
    trans.user = done_fn;

    int ret;

    if (numbytes <= 4) {
        trans.user = NULL;
        ret = spi_device_polling_transmit(spi, &trans);
        if (ret == 0)
            goto sync_ok;
        return ret;
    } else {
        spi_in_use = true;
        ret = spi_device_queue_trans(spi, &trans, 0);
        if (ret != 0)
            spi_in_use = false;
        return ret;
    }

sync_ok:
    tim_worker_run(call_fn, done_fn);
    return 0;
}
