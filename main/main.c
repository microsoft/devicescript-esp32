#include "jdesp.h"

#include "esp_timer.h"
#include "esp_event.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "services/interfaces/jd_spi.h"

#ifdef CONFIG_IDF_TARGET_ESP32C3
// #define PIN_BOOT_BTN 9
#else
// this makes the board reset
// #define PIN_BOOT_BTN 0
#endif

const char *JD_EVENT = "JD_EVENT";

worker_t main_worker;
uint32_t now;
static TaskHandle_t main_task;
static int loop_pending;
static esp_timer_handle_t main_loop_tick_timer;
uint16_t tim_max_sleep;

static void sync_main_loop_timer(void) {
    static uint16_t max_sleep;
    if (!tim_max_sleep)
        tim_max_sleep = 10000;
    if (max_sleep != tim_max_sleep) {
        esp_timer_stop(main_loop_tick_timer);
        max_sleep = tim_max_sleep;
        CHK(esp_timer_start_periodic(main_loop_tick_timer, max_sleep));
    }
}

int target_in_irq(void) {
    return main_task != NULL && xTaskGetCurrentTaskHandle() != main_task;
}

static void post_loop(void *dummy) {
    if (!loop_pending) {
        loop_pending = 1;
        esp_event_post(JD_EVENT, 1, NULL, 0, 0);
    }
}

void jdesp_wake_main(void) {
    post_loop(NULL);
}

void jd_usb_flush_stdout(void) {
    fflush(stdout);
}

static void loop_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id,
                         void *event_data) {
    if (!main_task) {
        CHK(esp_task_wdt_add(NULL));

        main_task = xTaskGetCurrentTaskHandle();
        // this will call app_init_services(), which may try to send something, so we better run it
        // from here
        jd_init();

        jd_tcpsock_init();

        DMESG("loop init done");
    }

    loop_pending = 0;

    static int n;
    if (n++ > 50) {
        jd_usb_flush_stdout();
        n = 0;
    }

    CHK(esp_task_wdt_reset());

#if defined(PIN_BOOT_BTN)
    if (pin_get(PIN_BOOT_BTN) == 0)
        reboot_to_uf2();
#endif

    jd_process_everything();

    worker_do_work(main_worker);

    jd_tcpsock_process();

    uart_log_dmesg();

    // re-post ourselves immediately if more frames to process
    if (jd_rx_has_frame())
        post_loop(NULL);
    else
        sync_main_loop_timer();
}

void app_init_services(void) {
    devs_service_full_init(devsmgr_init_mem(32 * 1024));

    if (i2c_init() == 0) {
        jd_scan_all();
        // i2cserv_init();
    }

#if JD_WIFI
    jd_wifi_rssi(); // make sure WiFi module links
#endif

    adc_can_read_pin(0); // link ADC
    jd_spi_is_ready(); // link SPI
}

static int log_writefn(void *cookie, const char *data, int size) {
    jd_lstore_append_frag(0, JD_LSTORE_TYPE_LOG, data, size);
    jd_dmesg_write(data, size);
    return size;
}

void app_main(void) {
    // reboot after 5s without watchdog
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = 5000,
        .trigger_panic = true,
    };
    CHK(esp_task_wdt_init(&wdt_cfg));
    // subscribe current task, in case something goes wrong here (unlikely)
    CHK(esp_task_wdt_add(NULL));

    ESP_LOGI("JD", "starting devicescript-esp32 %s", app_get_fw_version());
    DMESG("starting devicescript-esp32 %s", app_get_fw_version());

    ESP_LOGW("JD", "devname %s", dcfg_get_string("devName", NULL));

    extern void jd_rgbext_link(void);
    jd_rgbext_link();

    usb_pre_init();
    jd_seed_random(esp_random());
    init_sdcard();

#if 0
    esp_log_level_set("sdmmc_init", ESP_LOG_VERBOSE);
    esp_log_level_set("sdmmc_cmd", ESP_LOG_VERBOSE);
    esp_log_level_set("sdspi_transaction", ESP_LOG_VERBOSE);
    esp_log_level_set("sdspi_host", ESP_LOG_VERBOSE);
#endif

    _GLOBAL_REENT->_stdout = fwopen(NULL, &log_writefn);
    // enable line buffering for this stream (to be similar to the regular UART-based output)
    static char stdout_buf[128];
    setvbuf(stdout, stdout_buf, _IOLBF, sizeof(stdout_buf));

    usb_init();
    uart_log_init();
    jd_usb_enable_serial();

    main_worker = worker_alloc();

    esp_event_loop_create_default();

    jd_settings_get_bin("no_such_setting", NULL, 0); // force flash init

    tim_init();

    jd_rx_init();
    jd_tx_init();

    uart_init_();

    esp_timer_create_args_t args;
    args.callback = post_loop;
    args.arg = NULL;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "10ms";
    CHK(esp_timer_create(&args, &main_loop_tick_timer));
    sync_main_loop_timer();

    DMESG("app_main mostly done");

    CHK(esp_event_handler_instance_register(JD_EVENT, 1, loop_handler, NULL, NULL));
    loop_pending = 0; // someone might have tried to post it before, but it would be ignored without
                      // loop_handler registered
    post_loop(NULL);  // run the loop for the first time

    // unsubscribe current task before exiting
    // the loop_handler should have subscribed itself by now
    CHK(esp_task_wdt_delete(NULL));
}
