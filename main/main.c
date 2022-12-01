#include "jdesp.h"

#include "esp_timer.h"
#include "esp_event.h"
#include "esp_task_wdt.h"

#ifdef CONFIG_IDF_TARGET_ESP32C3
// #define PIN_BOOT_BTN 9
#else
#define PIN_BOOT_BTN 0
#endif

const char *JD_EVENT = "JD_EVENT";

worker_t main_worker;
uint32_t now;
static TaskHandle_t main_task;
static int loop_pending;
static esp_timer_handle_t main_loop_tick_timer;

// 33, 34
#define DET_PINS(a, b) (((a) + 1) * 3 + ((b) + 1))

#define BOARD_48 DET_PINS(PIN_PULL_NONE, PIN_PULL_NONE)
#define BOARD_207_V4_2 DET_PINS(PIN_PULL_DOWN, PIN_PULL_UP)
#define BOARD_207_V4_3 DET_PINS(PIN_PULL_DOWN, PIN_PULL_DOWN)

#define BOARD_FLAG_PWR_ACTIVE_HI 0x00000001

typedef struct {
    const char *name;
    uint32_t dev_class;
    uint32_t flags;
} board_info_t;

const board_info_t board_infos[9] = {
#ifdef NO_JACSCRIPT

#if defined(CONFIG_IDF_TARGET_ESP32C3)
    [0] = {"MSR Brain ESP32-C3 Cloud Connector 216 v4.5", 0x39b608d4, BOARD_FLAG_PWR_ACTIVE_HI},
#else
    [BOARD_48] = {"JacdacIoT Cloud Connector 48 v0.2", 0x30a3c887, 0},
    [BOARD_207_V4_2] = {"JM Brain S2-mini Cloud Connector 207 v4.2", 0x33b166ba, 0},
    [BOARD_207_V4_3] = {"JM Brain S2-mini Cloud Connector 207 v4.3", 0x33b166ba,
                        BOARD_FLAG_PWR_ACTIVE_HI},
#endif

#else

#if defined(CONFIG_IDF_TARGET_ESP32C3)
    [0] = {"MSR Brain ESP32-C3 Jacscript 216 v4.5", 0x33e239e5, BOARD_FLAG_PWR_ACTIVE_HI},
#else
    [BOARD_48] = {"JacdacIoT Jacscript 48 v0.2", 0x3de1398b, 0},
    [BOARD_207_V4_2] = {"JM Brain S2-mini Jacscript 207 v4.2", 0x322e0e64, 0},
    [BOARD_207_V4_3] = {"JM Brain S2-mini Jacscript 207 v4.3", 0x322e0e64,
                        BOARD_FLAG_PWR_ACTIVE_HI},
#endif

#endif

};
static int board_type;

void get_i2c_pins(uint8_t *sda, uint8_t *scl) {
    if (board_type == BOARD_48) {
        *sda = 9;
        *scl = 10;
    } else {
        *sda = *scl = 0xff;
    }
}

const char *app_get_dev_class_name(void) {
    return board_infos[board_type].name;
}

uint32_t app_get_device_class(void) {
    return board_infos[board_type].dev_class;
}

#if !defined(CONFIG_IDF_TARGET_ESP32C3)
static int detect_pin(int pin) {
    pin_setup_input(pin, PIN_PULL_DOWN);
    target_wait_us(100);
    int v1 = pin_get(pin);
    pin_setup_input(pin, PIN_PULL_UP);
    target_wait_us(100);
    int v2 = pin_get(pin);
    if (v1 != v2)
        return PIN_PULL_NONE;
    // it's externally pulled, no need to leave the internal pull on
    pin_setup_analog_input(pin);
    return v1 ? PIN_PULL_UP : PIN_PULL_DOWN;
}
#endif

static void setup_pins(void) {
#if defined(CONFIG_IDF_TARGET_ESP32C3)
    board_type = 0;
#else
    pin_setup_input(PIN_BOOT_BTN, PIN_PULL_UP);
    board_type = DET_PINS(detect_pin(33), detect_pin(34));
#endif
    DMESG("board type: %s", board_infos[board_type].name);
}

int target_in_irq(void) {
    return xTaskGetCurrentTaskHandle() != main_task;
}

FILE *orig_stdout;
FILE *lstore_stdout;

void flush_dmesg(void) {
    char pref[32];
    const char *suff = LOG_RESET_COLOR "\n";

    static char *dmesg_copy;
    if (!dmesg_copy)
        dmesg_copy = malloc(DEVICE_DMESG_BUFFER_SIZE + sizeof(pref) + 10);

    uint32_t len;

    char *text_start = dmesg_copy + sizeof(pref);

    target_disable_irq();
    len = codalLogStore.ptr;
    memcpy(text_start, codalLogStore.buffer, len);
    codalLogStore.ptr = 0;
    codalLogStore.buffer[0] = 0;
    target_enable_irq();

    if (len >= 1 && text_start[len - 1] == '\n') {
        len--;
    }

    if (len > 0) {
        int multi = memchr(text_start, '\n', len) != NULL;
        jd_sprintf(pref, sizeof(pref), LOG_COLOR(LOG_COLOR_CYAN) "DM (%d):%c", esp_log_timestamp(),
                   multi ? '\n' : ' ');
        int lpref = strlen(pref);
        char *trg = text_start - lpref;
        memcpy(trg, pref, lpref);
        int totallen = len + lpref;
        int lsuff = strlen(suff);
        memcpy(trg + totallen, suff, lsuff);
        totallen += lsuff;
        jd_lstore_append_frag(0, JD_LSTORE_TYPE_DMESG, text_start, len);
        jd_usb_write_serial(trg, totallen);
        fwrite(trg, 1, totallen, orig_stdout);
    }
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

    CHK(esp_task_wdt_reset());

#if defined(CONFIG_IDF_TARGET_ESP32S2)
    if (pin_get(PIN_BOOT_BTN) == 0)
        reboot_to_uf2();
#endif

    jd_process_everything();

    worker_do_work(main_worker);

    // TODO move this to a separate thread
    flush_dmesg();
    jd_lstore_process();

    jd_tcpsock_process();

    // re-post ourselves immediately if more frames to process
    if (jd_rx_has_frame())
        post_loop(NULL);
}

#ifdef PIN_PWR_EN
static power_config_t pwr_cfg = {
    .pin_fault = PIN_PWR_FAULT, // active low
    .pin_en = PIN_PWR_EN,
    .pin_pulse = NO_PIN,
    .en_active_high = 0,
    .fault_ignore_ms = 100, // there 4.7uF cap that takes time to charge
};
#endif

void accelerometer_data_transform(int32_t sample[3]) {}

void app_init_services(void) {
#ifdef PIN_PWR_EN
    if (board_infos[board_type].flags & BOARD_FLAG_PWR_ACTIVE_HI)
        pwr_cfg.en_active_high = 1;
    power_init(&pwr_cfg);
#endif
#ifndef NO_JACSCRIPT
    jd_role_manager_init();
    devicescriptmgr_init_mem(32 * 1024);
#endif
    wifi_init();
    wsskhealth_init();
    devscloud_init(&wssk_cloud);
#ifndef NO_JACSCRIPT
    tsagg_init(&wssk_cloud);
#endif

    if (i2c_init() == 0) {
        jd_scan_all();
    }
}

static int log_writefn(void *cookie, const char *data, int size) {
    jd_lstore_append_frag(0, JD_LSTORE_TYPE_LOG, data, size);
    jd_usb_write_serial(data, size);
    return fwrite(data, 1, size, orig_stdout);
}

void app_main() {
    // reboot after 5s without watchdog
    CHK(esp_task_wdt_init(5, true));
    // subscribe current task, in case something goes wrong here (unlikely)
    CHK(esp_task_wdt_add(NULL));

    ESP_LOGI("JD", "starting devicescript-esp32 %s", app_get_fw_version());
    DMESG("starting devicescript-esp32 %s", app_get_fw_version());

    usb_pre_init();
    jd_seed_random(esp_random());
    init_sdcard();

#ifdef LED_SET_RGB
    LED_SET_RGB(0, 0, 0);
#endif

#if 0
    esp_log_level_set("sdmmc_init", ESP_LOG_VERBOSE);
    esp_log_level_set("sdmmc_cmd", ESP_LOG_VERBOSE);
    esp_log_level_set("sdspi_transaction", ESP_LOG_VERBOSE);
    esp_log_level_set("sdspi_host", ESP_LOG_VERBOSE);
#endif

    orig_stdout = stdout;
    lstore_stdout = _GLOBAL_REENT->_stdout = fwopen(NULL, &log_writefn);
    // enable line buffering for this stream (to be similar to the regular UART-based output)
    static char stdout_buf[128];
    setvbuf(stdout, stdout_buf, _IOLBF, sizeof(stdout_buf));

    jd_usb_enable_serial();

    main_worker = worker_alloc();

    esp_event_loop_create_default();

    setup_pins();

    jd_settings_get_bin("no_such_setting", NULL, 0); // force flash init

    tim_init();

    jd_rx_init();
    jd_tx_init();

    uart_init_();
    usb_init();

    esp_timer_create_args_t args;
    args.callback = post_loop;
    args.arg = NULL;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "10ms";
    CHK(esp_timer_create(&args, &main_loop_tick_timer));
    CHK(esp_timer_start_periodic(main_loop_tick_timer, 10000));

    DMESG("app_main mostly done");

    CHK(esp_event_handler_instance_register(JD_EVENT, 1, loop_handler, NULL, NULL));
    loop_pending = 0; // someone might have tried to post it before, but it would be ignored without
                      // loop_handler registered
    post_loop(NULL);  // run the loop for the first time

    // unsubscribe current task before exiting
    // the loop_handler should have subscribed itself by now
    CHK(esp_task_wdt_delete(NULL));
}
