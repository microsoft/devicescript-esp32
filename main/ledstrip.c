#include "jdesp.h"
#include "devs_internal.h"
#include "led_strip_encoder.h"
#include "driver/rmt_tx.h"

#define RMT_LED_STRIP_RESOLUTION_HZ                                                                \
    10000000 // 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)

static void setup_strip(uint8_t pin, int mem_block_symbols, rmt_channel_handle_t *led_chan,
                        rmt_encoder_handle_t *led_encoder) {
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT, // select source clock
        .gpio_num = pin,
        .mem_block_symbols = mem_block_symbols,
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth =
            4, // set the number of transactions that can be pending in the background
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, led_chan));

    if (!*led_encoder) {
        led_strip_encoder_config_t encoder_config = {
            .resolution = RMT_LED_STRIP_RESOLUTION_HZ,
        };
        ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_config, led_encoder));
    }

    ESP_ERROR_CHECK(rmt_enable(*led_chan));
}

static void transmit(rmt_channel_handle_t led_chan, rmt_encoder_handle_t led_encoder,
                     const uint8_t *data, unsigned size) {
    rmt_transmit_config_t tx_config = {
        .loop_count = 0, // no transfer loop
    };
    ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, data, size, &tx_config));
}

void jd_rgbext_link(void) {}

static rmt_channel_handle_t rgbext_chan;
static rmt_encoder_handle_t rgbext_encoder;
static uint8_t rgbext_buf[3];

void jd_rgbext_init(int type, uint8_t pin) {
    if (rgbext_chan)
        return;
    if (type == 1) {
        setup_strip(pin, SOC_RMT_MEM_WORDS_PER_CHANNEL, &rgbext_chan, &rgbext_encoder);
    }
}

void jd_rgbext_set(uint8_t r, uint8_t g, uint8_t b) {
    if (rgbext_chan) {
        rgbext_buf[0] = r;
        rgbext_buf[1] = g;
        rgbext_buf[2] = b;
        uint8_t n = dcfg_get_u32("led.num", 1);
        for (uint8_t i = 0; i < n; ++i)
            transmit(rgbext_chan, rgbext_encoder, rgbext_buf, sizeof(rgbext_buf));
    }
}

static rmt_channel_handle_t led_chan;
static rmt_encoder_handle_t led_encoder;
static uint8_t led_pin, led_in_use;
static cb_t led_done_fn;
static uint32_t last_ctx_no;

static void led_strip_done_outside_isr(void *userdata) {
    rmt_disable(led_chan);
    led_in_use = false;
    led_done_fn();
}

static bool led_strip_done(rmt_channel_handle_t tx_chan, const rmt_tx_done_event_data_t *edata,
                           void *user_ctx) {
    JD_ASSERT(led_in_use);
    tim_worker_run(led_strip_done_outside_isr, NULL);
    return false;
}

int devs_led_strip_send(devs_ctx_t *ctx, uint8_t pin, const uint8_t *data, unsigned size,
                        cb_t donefn) {
    if (led_in_use)
        return -100;

    if (!led_chan || led_pin != pin || last_ctx_no != ctx->ctx_seq_no) {
        led_pin = pin;
        last_ctx_no = ctx->ctx_seq_no;
        if (led_chan) {
            // free-up any previously allocated channel
            CHK(rmt_del_channel(led_chan));
        }
        setup_strip(pin, SOC_RMT_MEM_WORDS_PER_CHANNEL, &led_chan, &led_encoder);
        rmt_tx_event_callbacks_t cbs = {.on_trans_done = led_strip_done};
        CHK(rmt_tx_register_event_callbacks(led_chan, &cbs, NULL));
    } else {
        CHK(rmt_enable(led_chan));
    }

    led_in_use = true;
    led_done_fn = donefn;
    transmit(led_chan, led_encoder, data, size);

    return 0;
}