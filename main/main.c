#include "jdesp.h"

#include "esp_timer.h"
#include "esp_event.h"

#define PIN_BOOT_BTN 0

const char *JD_EVENT = "JD_EVENT";

worker_t fg_worker, main_worker;
uint32_t now;
static TaskHandle_t main_task;
static int loop_pending;
static esp_timer_handle_t main_loop_tick_timer;

static void setup_pins(void) {
    pin_setup_input(PIN_BOOT_BTN, PIN_PULL_UP);
}

int target_in_irq(void) {
    return xTaskGetCurrentTaskHandle() != main_task;
}

void flush_dmesg(void) {
    char *dmesgCopy = malloc(sizeof(codalLogStore));

    uint32_t len;

    target_disable_irq();
    len = codalLogStore.ptr;
    memcpy(dmesgCopy, codalLogStore.buffer, len);
    codalLogStore.ptr = 0;
    codalLogStore.buffer[0] = 0;
    target_enable_irq();

    if (len) {
        if (dmesgCopy[len - 1] == '\n')
            len--;
        dmesgCopy[len] = 0;
        if (strchr(dmesgCopy, '\n'))
            ESP_LOGW("JD", "DMESG:\n%s", dmesgCopy);
        else
            ESP_LOGW("JD", "DMESG: %s", dmesgCopy);
    }
    free(dmesgCopy);
}

static void post_loop(void *dummy) {
    if (!loop_pending) {
        loop_pending = 1;
        esp_event_post(JD_EVENT, 1, NULL, 0, 0);
    }
}

static void loop_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id,
                         void *event_data) {
    if (!main_task) {
        main_task = xTaskGetCurrentTaskHandle();
        // this will call app_init_services(), which may try to send something, so we better run it
        // from here
        jd_init();
    }

    loop_pending = 0;

    if (pin_get(PIN_BOOT_BTN) == 0)
        reboot_to_uf2();

    jd_process_everything();

    worker_do_work(main_worker);

    flush_dmesg();

    // re-post ourselves immediately if more frames to process
    if (jd_rx_has_frame())
        post_loop(NULL);
}

static const power_config_t pwr_cfg = {
    .pin_fault = PIN_PWR_FAULT, // active low
    .pin_en = PIN_PWR_EN,
    .pin_pulse = NO_PIN,
    .en_active_high = 0,
    .fault_ignore_ms = 100, // there 4.7uF cap that takes time to charge
};

void app_init_services(void) {
    power_init(&pwr_cfg);
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

void app_main() {
    ESP_LOGI("JD", "starting jacscript-esp32 %s", app_fw_version);
    DMESG("starting jacscript-esp32 %s", app_fw_version);

    fg_worker = worker_start("jd_fg", 2048);
    main_worker = worker_alloc();

    esp_event_loop_create_default();

    setup_pins();

    flash_init();

    tim_init();
    uart_init();
    hf2_init();

    CHK(esp_event_handler_instance_register(JD_EVENT, 1, loop_handler, NULL, NULL));

    esp_timer_create_args_t args;
    args.callback = post_loop;
    args.arg = NULL;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "10ms";
    CHK(esp_timer_create(&args, &main_loop_tick_timer));
    CHK(esp_timer_start_periodic(main_loop_tick_timer, 10000));

    post_loop(NULL); // run the loop for the first time
}
