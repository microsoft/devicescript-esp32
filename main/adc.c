#include "jdesp.h"
#include "esp_adc/adc_oneshot.h"
#include "hal/adc_hal.h"

#define LOG_TAG "adc"
#include "devs_logging.h"

#define ADC_ATTEN ADC_ATTEN_DB_11
#define ADC_BITS ADC_BITWIDTH_DEFAULT

#if CONFIG_IDF_TARGET_ESP32
#define CH_OFFSET 32
static uint8_t channels[] = {
    ADC1_GPIO32_CHANNEL, ADC1_GPIO33_CHANNEL, ADC1_GPIO34_CHANNEL, ADC1_GPIO35_CHANNEL,
    ADC1_GPIO36_CHANNEL, ADC1_GPIO37_CHANNEL, ADC1_GPIO38_CHANNEL, ADC1_GPIO39_CHANNEL,
};

#elif CONFIG_IDF_TARGET_ESP32S2
static uint8_t channels[] = {
    0xff,
    ADC1_GPIO1_CHANNEL,
    ADC1_GPIO2_CHANNEL,
    ADC1_GPIO3_CHANNEL,
    ADC1_GPIO4_CHANNEL,
    ADC1_GPIO5_CHANNEL,
    ADC1_GPIO6_CHANNEL,
    ADC1_GPIO7_CHANNEL,
    ADC1_GPIO8_CHANNEL,
    ADC1_GPIO9_CHANNEL,
    ADC1_GPIO10_CHANNEL,
};

#elif CONFIG_IDF_TARGET_ESP32C3
static uint8_t channels[] = {
    ADC1_GPIO0_CHANNEL, ADC1_GPIO1_CHANNEL, ADC1_GPIO2_CHANNEL,
    ADC1_GPIO3_CHANNEL, ADC1_GPIO4_CHANNEL,
};

#elif CONFIG_IDF_TARGET_ESP32S3
static uint8_t channels[] = {
    0xff,
    ADC1_GPIO1_CHANNEL,
    ADC1_GPIO2_CHANNEL,
    ADC1_GPIO3_CHANNEL,
    ADC1_GPIO4_CHANNEL,
    ADC1_GPIO5_CHANNEL,
    ADC1_GPIO6_CHANNEL,
    ADC1_GPIO7_CHANNEL,
    ADC1_GPIO8_CHANNEL,
    ADC1_GPIO9_CHANNEL,
    ADC1_GPIO10_CHANNEL,
};

#else
#error "unknown ESP32"
#endif

static uint32_t inited_channels;
static adc_oneshot_unit_handle_t adc1_handle;

static int adc_ch(uint8_t pin) {
#ifdef CH_OFFSET
    if (pin < CH_OFFSET)
        return -1;
    pin -= CH_OFFSET;
#endif
    if (pin < sizeof(channels) && channels[pin] != 0xff)
        return channels[pin];
    return -1;
}

static void adc_init(void) {
    if (adc1_handle)
        return;

    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
}

bool adc_can_read_pin(uint8_t pin) {
    return adc_ch(pin) != -1;
}

uint16_t adc_read_pin(uint8_t pin) {
    int ch = adc_ch(pin);
    if (ch < 0)
        return 0;
    if (!(inited_channels & (1 << ch))) {
        adc_init();
        inited_channels |= 1 << ch;
        adc_oneshot_chan_cfg_t config = {
            .bitwidth = ADC_BITS,
            .atten = ADC_ATTEN,
        };
        CHK(adc_oneshot_config_channel(adc1_handle, ch, &config));
    }

    int res;
    CHK(adc_oneshot_read(adc1_handle, ch, &res));
    return res << (16 - ADC_BITS);
}

#if JD_CONFIG_TEMPERATURE
#include "driver/temperature_sensor.h"
int32_t adc_read_temp(void) {
    static temperature_sensor_handle_t temp_handle;
    if (!temp_handle) {
        temperature_sensor_config_t temp_sensor = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
        CHK(temperature_sensor_install(&temp_sensor, &temp_handle));
    }

    CHK(temperature_sensor_enable(temp_handle));
    float tsens_out;
    CHK(temperature_sensor_get_celsius(temp_handle, &tsens_out));
    CHK(temperature_sensor_disable(temp_handle));

    return (int)tsens_out;
}
#endif