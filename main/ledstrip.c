#include "jdesp.h"
#include "led_strip.h"

static led_strip_t *strip;

void jd_rgbext_link(void) {}

void jd_rgbext_init(int type, uint8_t pin) {
    if (strip)
        return;
    DMESG("*** rgb %d %d", type, pin);
    if (type == 1) {
        strip = led_strip_init(0, pin, 1);
        DMESG("npx on %d", pin);
        JD_ASSERT(strip != NULL);
    }
}

void jd_rgbext_set(uint8_t r, uint8_t g, uint8_t b) {
    if (strip) {
        strip->set_pixel(strip, 0, r, g, b);
        strip->refresh(strip, 0);
    }
}
