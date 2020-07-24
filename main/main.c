#include "jdesp.h"

#define PIN_LOG0 GPIO_NUM_23
#define PIN_LOG1 GPIO_NUM_18
#define PIN_LOG2 GPIO_NUM_26
#define PIN_LOG3 GPIO_NUM_27

#define PIN_LED_B GPIO_NUM_4
#define PIN_LED_G GPIO_NUM_2
#define PIN_LED_R GPIO_NUM_0

static uint8_t lines[] = {PIN_LOG0, PIN_LOG1, PIN_LOG2, PIN_LOG3}; // not const, so it goes in RAM
static xQueueHandle frame_queue;
static uint64_t led_off_time;

void setup_output(int pin) {
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[pin], PIN_FUNC_GPIO);
}

static void setup_pins(void) {
    for (int i = 0; i < sizeof(lines); i++)
        setup_output(lines[i]);

    setup_output(PIN_LED_R);
    setup_output(PIN_LED_G);
    setup_output(PIN_LED_B);
}

IRAM_ATTR void log_pin_set(int line, int v) {
    if ((unsigned)line < sizeof(lines)) {
        if (v) {
            GPIO.out_w1ts = (1 << lines[line]);
        } else {
            GPIO.out_w1tc = (1 << lines[line]);
        }
    }
}

IRAM_ATTR void log_pin_pulse(int line, int times) {
    if ((unsigned)line < sizeof(lines)) {
        while (times--) {
            GPIO.out_w1ts = (1 << lines[line]);
            GPIO.out_w1tc = (1 << lines[line]);
        }
    }
}

void led_set(int state) {
    gpio_set_level(PIN_LED_B, state);
}

void led_blink(int us) {
    led_off_time = tim_get_micros() + us;
    led_set(1);
}

static void flush_dmesg(void) {
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
            ESP_LOGI("JD", "DMESG:\n%s", dmesgCopy);
        else
            ESP_LOGI("JD", "DMESG: %s", dmesgCopy);
    }
    free(dmesgCopy);
}

uint32_t now;

void jd_app_handle_packet(jd_packet_t *pkt) {
    now = tim_get_micros();
    ostream_process_ack(pkt);
}

void jd_app_handle_command(jd_packet_t *pkt) {
    if (pkt->service_number == JD_SERVICE_NUMBER_STREAM)
        istream_handle_pkt(pkt);
}

static void jdloop(void *_dummy) {
    while (1) {
        jd_frame_t *fr = NULL;
        int qdelay = 1;

        if (led_off_time) {
            int timeLeft = led_off_time - tim_get_micros();
            if (timeLeft <= 0) {
                led_off_time = 0;
                led_set(0);
            } else if (timeLeft < 10000) {
                qdelay = 0;
            }
        }

        if (xQueueReceive(frame_queue, &fr, qdelay)) {
            jd_services_process_frame(fr);
            free(fr);
        }

        now = tim_get_micros();
        jd_services_tick();

        flush_dmesg();
    }
}

void app_init_services(void) {
    wifi_init();
    jdtcp_init();
}

void app_main() {
    ESP_LOGI("JD", "starting...");

    DMESG("app main");

    setup_pins();

    frame_queue = xQueueCreate(10, sizeof(jd_frame_t *));

    uart_init();
    tim_init();
    jd_init();

    log_pin_pulse(0, 1);

    xTaskCreatePinnedToCore(jdloop, "jdloop", 2 * 1024, NULL, 3, NULL, APP_CPU_NUM);
}

uint64_t jd_device_id(void) {
    static uint64_t addr;
    if (!addr) {
        uint8_t mac[6];
        esp_efuse_mac_get_default(mac);
        addr = ((uint64_t)0xff << 56) | ((uint64_t)mac[5] << 48) | ((uint64_t)mac[4] << 40) |
               ((uint64_t)mac[3] << 32) | ((uint64_t)mac[2] << 24) | ((uint64_t)mac[1] << 16) |
               ((uint64_t)mac[0] << 8) | ((uint64_t)0xfe << 0);
    }
    return addr;
}

void jd_rx_init(void) {}

int jd_rx_frame_received(jd_frame_t *frame) {
    jd_frame_t *copy = malloc(JD_FRAME_SIZE(frame));
    memcpy(copy, frame, JD_FRAME_SIZE(frame));
    if (!xQueueSendToBack(frame_queue, &copy, 0)) {
        free(copy);
        return -1;
    }
    return 0;
}

void jd_alloc_stack_check(void) {}

void jd_alloc_init(void) {}

void *jd_alloc(uint32_t size) {
    return calloc(size, 1);
}

void *jd_alloc_emergency_area(uint32_t size) {
    return calloc(size, 1);
}

void target_reset() {
    esp_restart();
}