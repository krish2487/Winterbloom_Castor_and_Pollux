#include "gem_adc.h"
#include "gem_clocks.h"
#include "gem_config.h"
#include "gem_gpio.h"
#include "gem_i2c.h"
#include "gem_mcp4728.h"
#include "gem_midi.h"
#include "gem_nvm.h"
#include "gem_pulseout.h"
#include "gem_usb.h"
#include "gem_voice_param_table.h"
#include "gem_voice_params.h"
#include "sam.h"
#include <stdio.h>

static void init_pins() {
    gem_gpio_set_as_input(PIN_BUTTON_PORT, PIN_BUTTON, true);
    // gem_gpio_set_as_output(PIN_STATUS_LED_PORT, PIN_STATUS_LED);
    // gem_gpio_set_as_output(PIN_STATUS_LED_2_PORT, PIN_STATUS_LED_2);
}

static uint32_t adc_results[10];

int main(void) {
    /* Configure clocks. */
    gem_clocks_init();

    /* Initialize NVM */
    gem_nvm_init();

    // Initialize any configuration data and functionality,
    // such as printf() in debug mode.
    gem_config_init();

    /* Initialize I/O pins. */
    init_pins();

    /* Initialize USB. */
    gem_usb_init();

    /* Enable i2c bus for communicating with the DAC. */
    gem_i2c_init();

    /* Configure the ADC and channel scanning. */
    gem_adc_init();
    gem_adc_init_input(&gem_adc_inputs[0]);
    gem_adc_init_input(&gem_adc_inputs[1]);
    gem_adc_init_input(&gem_adc_inputs[2]);
    gem_adc_init_input(&gem_adc_inputs[3]);
    gem_adc_init_input(&gem_adc_inputs[4]);
    gem_adc_init_input(&gem_adc_inputs[5]);
    gem_adc_init_input(&gem_adc_inputs[6]);
    gem_adc_init_input(&gem_adc_inputs[7]);
    gem_adc_init_input(&gem_adc_inputs[8]);
    gem_adc_init_input(&gem_adc_inputs[9]);
    gem_adc_start_scanning(gem_adc_inputs, 10, adc_results);

    /* Configure the timers/PWM generators. */
    gem_pulseout_init();

    /* TEST: settings. */
    struct gem_nvm_settings settings;

    if (!gem_config_get_nvm_settings(&settings)) {
        __wrap_printf("Failed to load settings.\r\n");
    } else {
        __wrap_printf("Loaded settings.\r\n");
        printf("Settings: 0x%x, 0x%x, 0x%x\r\n",
               settings.adc_gain_corr,
               settings.adc_offset_corr,
               settings.led_brightness);
    }

    /* Local variables */
    struct gem_voice_params castor_params;
    struct gem_voice_params pollux_params;

    /* Test */

    gem_pulseout_set_period(0, 1135);
    gem_pulseout_set_duty(0, 0.5f);
    gem_pulseout_set_period(1, 1135);
    gem_pulseout_set_duty(1, 0.5f);
    gem_mcp_4728_write_channels(
        (struct gem_mcp4728_channel){.value = 480},
        (struct gem_mcp4728_channel){.value = 2048},
        (struct gem_mcp4728_channel){.value = 450},
        (struct gem_mcp4728_channel){.value = 2048});

    while (1) {
        gem_usb_task();
        gem_midi_task();

        if (gem_adc_results_ready()) {
            //printf("CV A: %lu, CV A Pot: %lu, CV B: %lu, CV B Pot: %lu, Duty A: %lu, Duty A Pot: %lu, Duty B: %lu, Duty B Pot: %lu, Phase: %lu, Phase Pot: %lu \r\n", adc_results[0], adc_results[1], adc_results[2], adc_results[3], adc_results[4], adc_results[5], adc_results[6], adc_results[7], adc_results[8], adc_results[9]);

            // gem_voice_params_from_adc_code(
            //     gem_voice_param_table, gem_voice_param_table_len, 853, &castor_params);
            // gem_voice_params_from_adc_code(
            //     gem_voice_param_table, gem_voice_param_table_len, 853, &pollux_params);
            
            // gem_pulseout_set_period(0, castor_params.period_reg);
            // gem_pulseout_set_duty(0, 0.5f);
            // gem_pulseout_set_period(1, pollux_params.period_reg);
            // gem_pulseout_set_duty(1, 0.5f);
        
            // gem_mcp_4728_write_channels(450, 4096 - adc_results[5], pollux_params.dac_code, 4096 - adc_results[7]);
        }
    }

    return 0;
}