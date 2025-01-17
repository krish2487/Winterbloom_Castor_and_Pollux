#include "gem_periodic_waveform.h"
#include "gem_systick.h"

/* Constants and macros. */

#define TICKS_PER_SECOND F16(1000)


/* Public functions */


void GemPeriodicWaveform_init(struct GemPeriodicWaveform* waveform, gem_periodic_waveform_function function, fix16_t frequency) {
    waveform->function = function;
    waveform->frequency = frequency;
    waveform->phase = F16(0);
    waveform->value = F16(0);
    waveform->_last_update = gem_get_ticks();
}


void GemPeriodicWaveform_step(struct GemPeriodicWaveform* waveform) {
    uint32_t now = gem_get_ticks();
    uint32_t delta = now - waveform->_last_update;

    if (delta > 0) {
        waveform->phase += fix16_mul(fix16_div(waveform->frequency, TICKS_PER_SECOND), fix16_from_int(delta));

        if (waveform->phase > F16(1.0))
            waveform->phase = fix16_sub(waveform->phase, F16(1.0));

        waveform->_last_update = now;
    }

    waveform->value = waveform->function(waveform->phase);
}
