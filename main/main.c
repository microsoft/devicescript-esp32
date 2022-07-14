#include "jdesp.h"

#include "esp_timer.h"
#include "esp_event.h"
#include "esp_task_wdt.h"

#ifdef CONFIG_IDF_TARGET_ESP32C3
#define PIN_BOOT_BTN 9
#else
#define PIN_BOOT_BTN 0
#endif

const char *JD_EVENT = "JD_EVENT";

worker_t fg_worker, main_worker;
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
    uint32_t flags;
} board_info_t;

const board_info_t board_infos[9] = {
#if defined(CONFIG_IDF_TARGET_ESP32C3)
    [0] = {"Adafruit QT-Py-C3", 0},
#else
    [BOARD_48] = {"JacdacIoT 48", 0},
    [BOARD_207_V4_2] = {"JM Brain S2-mini 207 v4.2", 0},
    [BOARD_207_V4_3] = {"JM Brain S2-mini 207 v4.3", BOARD_FLAG_PWR_ACTIVE_HI},
#endif
};
static int board_type;

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
    pin_setup_input(PIN_BOOT_BTN, PIN_PULL_UP);
#if defined(CONFIG_IDF_TARGET_ESP32C3)
    board_type = 0;
#else
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

    static char *dmesgCopy;
    if (!dmesgCopy)
        dmesgCopy = malloc(DEVICE_DMESG_BUFFER_SIZE + sizeof(pref) + 10);

    uint32_t len;

    target_disable_irq();
    len = codalLogStore.ptr;
    memcpy(dmesgCopy + sizeof(pref), codalLogStore.buffer, len);
    codalLogStore.ptr = 0;
    codalLogStore.buffer[0] = 0;
    target_enable_irq();

    if (len > 1) {
        int multi = memchr(dmesgCopy + sizeof(pref), '\n', len - 1) != NULL;
        jd_sprintf(pref, sizeof(pref), LOG_COLOR(LOG_COLOR_CYAN) "DM (%d):%c", esp_log_timestamp(),
                   multi ? '\n' : ':');
        int lpref = strlen(pref);
        char *trg = dmesgCopy + sizeof(pref) - lpref;
        memcpy(trg, pref, lpref);
        int totallen = len + lpref;
        int lsuff = strlen(LOG_RESET_COLOR);
        memcpy(trg + totallen, LOG_RESET_COLOR, lsuff);
        totallen += lsuff;
        jd_lstore_append_frag(0, JD_LSTORE_TYPE_DMESG, dmesgCopy + sizeof(pref), len);
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
        stdout = lstore_stdout;

        CHK(esp_task_wdt_add(NULL));

        main_task = xTaskGetCurrentTaskHandle();
        // this will call app_init_services(), which may try to send something, so we better run it
        // from here
        jd_init();

        DMESG("loop init done");
    }

    loop_pending = 0;

    CHK(esp_task_wdt_reset());

    if (pin_get(PIN_BOOT_BTN) == 0)
        reboot_to_uf2();

    jd_process_everything();

    worker_do_work(main_worker);

    // TODO move this to a separate thread
    flush_dmesg();
    jd_lstore_process();

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

void app_init_services(void) {
#ifdef PIN_PWR_EN
    if (board_infos[board_type].flags & BOARD_FLAG_PWR_ACTIVE_HI)
        pwr_cfg.en_active_high = 1;
    power_init(&pwr_cfg);
#endif
    jd_role_manager_init();
    init_jacscript_manager();
    wifi_init();
    azureiothub_init();
    jacscloud_init(&azureiothub_cloud);
    tsagg_init(&azureiothub_cloud);
}

static void flash_init() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
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

    ESP_LOGI("JD", "starting jacscript-esp32 %s", app_fw_version);
    DMESG("starting jacscript-esp32 %s", app_fw_version);

    jd_seed_random(esp_random());
    init_sdcard();

    orig_stdout = stdout;
    lstore_stdout = stdout = fwopen(NULL, &log_writefn);
    // enable line buffering for this stream (to be similar to the regular UART-based output)
    static char stdout_buf[128];
    setvbuf(stdout, stdout_buf, _IOLBF, sizeof(stdout_buf));

    fg_worker = worker_start("jd_fg", 2048);
    main_worker = worker_alloc();

    esp_event_loop_create_default();

    setup_pins();

    flash_init();

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
