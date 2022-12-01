#include "jdesp.h"
#include "led_strip.h"

#ifdef PIN_WS2812B

void led_set_rgb(uint8_t r, uint8_t g, uint8_t b) {
    static led_strip_t *strip;
    if (!strip) {
        strip = led_strip_init(0, PIN_WS2812B, 1);
        JD_ASSERT(strip != NULL);
    }
    strip->set_pixel(strip, 0, r, g, b);
    strip->refresh(strip, 0);
}

#endif