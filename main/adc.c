#include "jdesp.h"
#include "driver/temp_sensor.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "hal/adc_hal.h"

#define LOG_TAG "adc"
#include "devs_logging.h"

#define ADC_EXAMPLE_ATTEN ADC_ATTEN_DB_11

#if CONFIG_IDF_TARGET_ESP32
#define ADC_EXAMPLE_CALI_SCHEME ESP_ADC_CAL_VAL_EFUSE_VREF
#elif CONFIG_IDF_TARGET_ESP32S2
#define ADC_EXAMPLE_CALI_SCHEME ESP_ADC_CAL_VAL_EFUSE_TP
#elif CONFIG_IDF_TARGET_ESP32C3
#define ADC_EXAMPLE_CALI_SCHEME ESP_ADC_CAL_VAL_EFUSE_TP
#elif CONFIG_IDF_TARGET_ESP32S3
#define ADC_EXAMPLE_CALI_SCHEME ESP_ADC_CAL_VAL_EFUSE_TP_FIT
#endif

static bool use_calibration;
static uint32_t inited_channels;
static esp_adc_cal_characteristics_t adc1_chars;

#if defined(CONFIG_IDF_TARGET_ESP32C3)
static uint8_t channels[] = {
    0xff,
    ADC1_GPIO1_CHANNEL,
    ADC1_GPIO2_CHANNEL,
    ADC1_GPIO3_CHANNEL,
    ADC1_GPIO4_CHANNEL,
    ADC1_GPIO5_CHANNEL,
};
#endif

static int adc_ch(uint8_t pin) {
    if (pin < sizeof(channels) && channels[pin] != 0xff)
        return channels[pin];
    return -1;
}

static void adc_init(void) {
    static bool inited;
    if (inited)
        return;
    inited = true;

    adc1_config_width(ADC_WIDTH_BIT_DEFAULT);

    int ret = esp_adc_cal_check_efuse(ADC_EXAMPLE_CALI_SCHEME);
    if (ret) {
        LOG("error reading calibration data: %d", ret);
    } else {
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_EXAMPLE_ATTEN, ADC_WIDTH_BIT_DEFAULT, 0,
                                 &adc1_chars);
        use_calibration = true;
        LOG("calibration OK");
    }
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
        adc1_config_channel_atten(ch, ADC_EXAMPLE_ATTEN);
    }

    int res = adc1_get_raw(ch);
    if (use_calibration) {
        unsigned mv = esp_adc_cal_raw_to_voltage(res, &adc1_chars);
        return mv;
    } else {
        return res;
    }
}

int32_t adc_read_temp(void) {
    static bool inited;
    if (!inited) {
        inited = true;
        temp_sensor_config_t temp_sensor = TSENS_CONFIG_DEFAULT();
        temp_sensor_get_config(&temp_sensor);
        temp_sensor.dac_offset = TSENS_DAC_DEFAULT;
        temp_sensor_set_config(temp_sensor);
        temp_sensor_start();
    }
    float r;
    temp_sensor_read_celsius(&r);
    return (int)r;
}
