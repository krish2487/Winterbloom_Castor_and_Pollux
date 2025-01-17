#include "gem_adc.h"
#include "fix16.h"
#include "gem_config.h"
#include "gem_gpio.h"
#include "sam.h"

/* Inputs to scan. */
static const struct GemADCInput* _inputs;
static size_t _num_inputs;

/* Results from input scanning */
static volatile uint32_t* _results;

/* Scanning state */
static volatile size_t _current_input = 0;
static volatile bool _results_ready = false;

/* Private forward declarations. */
static void _gem_adc_scan();

/* Public methods. */

void gem_adc_init(int16_t offset_error, uint16_t gain_error) {
    /* Enable the APB clock for the ADC. */
    PM->APBCMASK.reg |= PM_APBCMASK_ADC;

    /* Enable GCLK1 for the ADC */
    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_CLKEN | GEM_ADC_GCLK | GCLK_CLKCTRL_ID_ADC;

    /* Wait for bus synchronization. */
    while (GCLK->STATUS.bit.SYNCBUSY) {};

    /* Reset the ADC. */
    ADC->CTRLA.bit.ENABLE = 0;
    while (ADC->STATUS.bit.SYNCBUSY) {};
    ADC->CTRLA.bit.SWRST = 1;
    while (ADC->CTRLA.bit.SWRST || ADC->STATUS.bit.SYNCBUSY) {};

    uint32_t bias = (*((uint32_t*)ADC_FUSES_BIASCAL_ADDR) & ADC_FUSES_BIASCAL_Msk) >> ADC_FUSES_BIASCAL_Pos;
    uint32_t linearity =
        (*((uint32_t*)ADC_FUSES_LINEARITY_0_ADDR) & ADC_FUSES_LINEARITY_0_Msk) >> ADC_FUSES_LINEARITY_0_Pos;
    linearity |= ((*((uint32_t*)ADC_FUSES_LINEARITY_1_ADDR) & ADC_FUSES_LINEARITY_1_Msk) >> ADC_FUSES_LINEARITY_1_Pos)
                 << 5;

    /* Wait for bus synchronization. */
    while (ADC->STATUS.bit.SYNCBUSY) {};

    /* Write the calibration data. */
    ADC->CALIB.reg = ADC_CALIB_BIAS_CAL(bias) | ADC_CALIB_LINEARITY_CAL(linearity);

    /* Wait for bus synchronization. */
    while (ADC->STATUS.bit.SYNCBUSY) {};

    ADC->CTRLB.reg |= GEM_ADC_PRESCALER;

    ADC->AVGCTRL.reg |= GEM_ADC_SAMPLE_NUM | GEM_ADC_SAMPLE_ADJRES;

#if GEM_ADC_SAMPLE_NUM == ADC_AVGCTRL_SAMPLENUM_1
    ADC->CTRLB.reg |= ADC_CTRLB_RESSEL_12BIT;
#else
    // 16-bit result needed for multisampling
    ADC->CTRLB.reg |= ADC_CTRLB_RESSEL_16BIT;
#endif

    /* Configure the measurement parameters. */

#ifndef GEM_ADC_USE_EXTERNAL_REF
    /*
    - Use the internal VCC reference. This is 1/2 of what's on VCCA.
        since VCCA is typically 3.3v, this is 1.65v.
    - GAIN_DIV2 means that the input voltage is halved. This is important
        because the voltage reference is 1/2 of VCCA. So if you want to
        measure 0-3.3v, you need to halve the input as well.
    */
    ADC->REFCTRL.reg |= ADC_REFCTRL_REFSEL_INTVCC1;
    ADC->INPUTCTRL.reg |= ADC_INPUTCTRL_GAIN_DIV2 | ADC_INPUTCTRL_MUXNEG_GND;
#else
    ADC->REFCTRL.reg |= ADC_REFCTRL_REFSEL_AREFA;
    ADC->INPUTCTRL.reg |= ADC_INPUTCTRL_GAIN_1X | ADC_INPUTCTRL_MUXNEG_GND;
#endif

    /* Enable the reference buffer to increase accuracy (at the cost of speed). */
    ADC->REFCTRL.bit.REFCOMP = 1;

    /* Enable offset and error correction. */
    ADC->OFFSETCORR.reg = ADC_OFFSETCORR_OFFSETCORR(offset_error);
    ADC->GAINCORR.reg = ADC_GAINCORR_GAINCORR(gain_error);
    ADC->CTRLB.bit.CORREN = 1;

    /* Wait for bus synchronization. */
    while (ADC->STATUS.bit.SYNCBUSY) {};

    /* Enable the ADC. */
    ADC->CTRLA.bit.ENABLE = 1;

    /* NOTE: The datasheet says to throw away the first reading, however,
        since Gemini does a *lot* of reads and uses averaging, this
        isn't really necessary.
    */
}

void gem_adc_set_error_correction(uint16_t gain, uint16_t offset) {
    ADC->CTRLA.bit.ENABLE = 0;
    while (ADC->STATUS.bit.SYNCBUSY) {};

    ADC->OFFSETCORR.reg = ADC_OFFSETCORR_OFFSETCORR(offset);
    ADC->GAINCORR.reg = ADC_GAINCORR_GAINCORR(gain);

    ADC->CTRLA.bit.ENABLE = 1;
    while (ADC->STATUS.bit.SYNCBUSY) {};
}

void gem_adc_init_input(const struct GemADCInput* input) {
    gem_gpio_set_as_input(input->port, input->pin, false);
    gem_gpio_set_mux(input->port, input->pin, GEM_PMUX_B);
}

uint16_t gem_adc_read_sync(const struct GemADCInput* input) {
    /* Disable interrupts - this stops any scanning. */
    NVIC_DisableIRQ(ADC_IRQn);

    /* Set the positive mux to the input pin */
    ADC->INPUTCTRL.bit.MUXPOS = input->ain;
    while (ADC->STATUS.bit.SYNCBUSY) {};

    /* Start the ADC using a software trigger. */
    ADC->SWTRIG.bit.START = 1;

    /* Wait for the result ready flag to be set. */
    while (ADC->INTFLAG.bit.RESRDY == 0) {};

    /* Clear the flag. */
    ADC->INTFLAG.reg = ADC_INTFLAG_RESRDY;

    /* Do it twice to ensure no weirdness with the measurement. */
    ADC->SWTRIG.bit.START = 1;
    while (ADC->INTFLAG.bit.RESRDY == 0) {};
    ADC->INTFLAG.reg = ADC_INTFLAG_RESRDY;

    /* Read the value. */
    return ADC->RESULT.reg;
}

void gem_adc_start_scanning(const struct GemADCInput* inputs, size_t num_inputs, uint32_t* results) {
    _inputs = inputs;
    _num_inputs = num_inputs;
    _current_input = 0;
    _results_ready = false;
    _results = results;

    NVIC_SetPriority(ADC_IRQn, 1);
    NVIC_EnableIRQ(ADC_IRQn);

    _gem_adc_scan();
}

void gem_adc_stop_scanning() { NVIC_DisableIRQ(ADC_IRQn); }

bool gem_adc_results_ready() {
    if (_results_ready) {
        _results_ready = false;
        return true;
    } else {
        return false;
    }
}

struct GemADCErrors
gem_adc_calculate_errors(uint32_t low_measured, uint32_t low_expected, uint32_t high_measured, uint32_t high_expected) {
    struct GemADCErrors result;
    result.gain = fix16_div(fix16_from_int(high_expected - low_expected), fix16_from_int(high_measured - low_measured));
    result.offset = fix16_sub(fix16_mul(result.gain, fix16_from_int(low_measured)), fix16_from_int(low_expected));
    return result;
};

fix16_t gem_adc_correct_errors(const fix16_t value, const struct GemADCErrors errors) {
    return fix16_mul(fix16_sub(value, errors.offset), errors.gain);
}

uint16_t gem_adc_correct_errors_u_int16(const uint16_t value, const struct GemADCErrors errors) {
    int32_t result = fix16_to_int(gem_adc_correct_errors(fix16_from_int(value), errors));
    if (result < 0)
        result = 0;
    if (result > UINT16_MAX)
        result = UINT16_MAX;
    return (uint16_t)(result);
}

/* Private methods & interrupt handlers. */

static void _gem_adc_scan() {
    struct GemADCInput input = _inputs[_current_input];

    /* Swap out the input pin. */
    ADC->INPUTCTRL.bit.MUXPOS = input.ain;

    /* Enable the interrupt for result ready. */
    ADC->INTENSET.bit.RESRDY = 1;

    /* Wait for synchronization. */
    while (ADC->STATUS.bit.SYNCBUSY) {};

    /* Start the ADC using a software trigger. */
    ADC->SWTRIG.bit.START = 1;
}

void ADC_Handler(void) {
    /* Should always be the result ready flag. */
    if (!ADC->INTFLAG.bit.RESRDY) {
#ifdef DEBUG
        while (1) {}
#endif
        ADC->INTFLAG.reg = ADC_INTFLAG_RESETVALUE;
        return;
    }

    /* Clear the interrupt flag. */
    ADC->INTFLAG.reg = ADC_INTFLAG_RESRDY;

    /* Store the result */
    _results[_current_input] = ADC->RESULT.reg;

    /* Scan the next input */
    _current_input = _current_input + 1;
    if (_current_input == _num_inputs) {
        _current_input = 0;
        _results_ready = true;
    }

    _gem_adc_scan();
}
