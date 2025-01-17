#include "gem_led_animation.h"
#include "fix16.h"
#include "gem_colorspace.h"
#include "gem_config.h"
#include "gem_dotstar.h"
#include "gem_random.h"
#include "gem_systick.h"
#include "gem_waveforms.h"
#include <stdint.h>

struct GemLEDTweakData gem_led_tweak_data = {.lfo_value = F16(0)};

static const uint32_t _hue_offsets[GEM_DOTSTAR_COUNT] = {
    65355 / GEM_DOTSTAR_COUNT * 2,
    65355 / GEM_DOTSTAR_COUNT * 2,
    65355 / GEM_DOTSTAR_COUNT * 6,
    0,
    65355 / GEM_DOTSTAR_COUNT,
    65355 / GEM_DOTSTAR_COUNT * 4,
    65355 / GEM_DOTSTAR_COUNT * 3,
};
static enum GemLEDAnimationMode _mode = GEM_LED_MODE_NORMAL;
static uint32_t _last_update;
static fix16_t _phase_a = F16(0);
static uint32_t _hue_accum;
static uint8_t _sparkles[GEM_DOTSTAR_COUNT];

/* Forward declarations. */

static void _gem_led_animation_step_normal(uint32_t delta);
static void _gem_led_animation_step_hard_sync(uint32_t delta);
static void _gem_led_animation_step_calibration(uint32_t ticks);
static void _gem_led_animation_step_tweak(uint32_t ticks);

/* Public functions. */

void gem_led_animation_init() { _last_update = gem_get_ticks(); }

void gem_led_animation_set_mode(enum GemLEDAnimationMode mode) { _mode = mode; }

void gem_led_animation_step() {
    uint32_t ticks = gem_get_ticks();
    uint32_t delta = ticks - _last_update;
    if (delta < GEM_ANIMATION_INTERVAL) {
        return;
    }

    _last_update = ticks;

    switch (_mode) {
        case GEM_LED_MODE_NORMAL:
            _gem_led_animation_step_normal(delta);
            break;
        case GEM_LED_MODE_HARD_SYNC:
            _gem_led_animation_step_hard_sync(delta);
            break;
        case GEM_LED_MODE_CALIBRATION:
            _gem_led_animation_step_calibration(ticks);
            break;
        case GEM_LED_MODE_TWEAK:
            _gem_led_animation_step_tweak(delta);
            break;
        default:
            break;
    }
    gem_dotstar_update();
}

/* Private functions. */

static void _gem_led_animation_step_normal(uint32_t delta) {
    _phase_a += fix16_div(fix16_from_int(delta), F16(2200.0));
    if (_phase_a > F16(1.0))
        _phase_a = fix16_sub(_phase_a, F16(1.0));

    _hue_accum += delta * 5;

    for (size_t i = 0; i < GEM_DOTSTAR_COUNT; i++) {
        fix16_t phase_offset = fix16_div(fix16_from_int(i), F16(GEM_DOTSTAR_COUNT));
        fix16_t sin_a = gem_sine_norm(_phase_a + phase_offset);
        uint8_t value = 20 + fix16_to_int(fix16_mul(sin_a, F16(235)));
        uint16_t hue = (_hue_accum + _hue_offsets[i]) % UINT16_MAX;
        uint32_t color;

        if (gem_random32() % 400 == 0)
            _sparkles[i] = 255;

        if (_sparkles[i] == 0) {
            color = gem_colorspace_hsv_to_rgb(hue, 255, value);
        } else {
            color = gem_colorspace_hsv_to_rgb(hue, 255 - _sparkles[i], value);
            if (_sparkles[i] <= delta / 4)
                _sparkles[i] = 0;
            else
                _sparkles[i] -= delta / 4;
        }

        gem_dotstar_set32(i, color);
    }
}

static void _gem_led_animation_step_hard_sync(uint32_t delta) {
    _phase_a += fix16_div(fix16_from_int(delta), F16(2200.0));
    if (_phase_a > F16(1.0))
        _phase_a = fix16_sub(_phase_a, F16(1.0));

    _hue_accum += delta * 5;

    for (size_t i = 0; i < GEM_DOTSTAR_COUNT; i++) {
        fix16_t phase_offset = fix16_div(fix16_from_int(i), F16(GEM_DOTSTAR_COUNT));
        fix16_t sin_a = gem_sine_norm(_phase_a + phase_offset);
        uint8_t value = 20 + fix16_to_int(fix16_mul(sin_a, F16(235)));
        uint16_t hue = _hue_accum % UINT16_MAX;
        uint32_t color;

        if (gem_random32() % 400 == 0)
            _sparkles[i] = 255;

        if (_sparkles[i] == 0) {
            color = gem_colorspace_hsv_to_rgb(hue, 255, value);
        } else {
            color = gem_colorspace_hsv_to_rgb(hue, 255 - _sparkles[i], value);
            if (_sparkles[i] <= delta / 4)
                _sparkles[i] = 0;
            else
                _sparkles[i] -= delta / 4;
        }

        gem_dotstar_set32(i, color);
    }
}

static void _gem_led_animation_step_calibration(uint32_t ticks) {
    for (uint8_t i = 0; i < GEM_DOTSTAR_COUNT; i++) {
        fix16_t bright_time = fix16_div(fix16_from_int(ticks / 2), F16(5000.0));
        fix16_t sinv = gem_sine(bright_time);
        fix16_t sinadj = fix16_div(fix16_add(sinv, F16(1.0)), F16(2.0));
        uint8_t value = fix16_to_int(fix16_mul(F16(255.0), sinadj));
        uint32_t color = gem_colorspace_hsv_to_rgb(0, 255 - value / 2, value);
        gem_dotstar_set32(i, color);
    }
}

static void _gem_led_animation_step_tweak(uint32_t delta) {
    _hue_accum += delta;

    for (uint8_t i = 0; i < GEM_DOTSTAR_COUNT; i++) { gem_dotstar_set32(i, 0); }

    if (gem_led_tweak_data.castor_pwm) {
        gem_dotstar_set(0, 0, 255, 255);
        gem_dotstar_set(1, 0, 255, 255);
    }

    if (gem_led_tweak_data.pollux_pwm) {
        gem_dotstar_set(2, 255, 0, 255);
        gem_dotstar_set(3, 255, 0, 255);
    }

    fix16_t lfoadj = fix16_div(fix16_add(gem_led_tweak_data.lfo_value, F16(1.0)), F16(2.0));
    uint8_t lfo_value = fix16_to_int(fix16_mul(F16(255.0), lfoadj));
    gem_dotstar_set32(4, gem_colorspace_hsv_to_rgb(UINT16_MAX / 12 * 2, 255, lfo_value));
    gem_dotstar_set32(5, gem_colorspace_hsv_to_rgb(UINT16_MAX / 12 * 2, 255, lfo_value));
    gem_dotstar_set32(6, gem_colorspace_hsv_to_rgb(UINT16_MAX / 12 * 2, 255, lfo_value));
}
