/* Heltec WiFi LoRa 32 V3 battery monitor.
 *
 * VBAT is divided by 390k / 100k (factor 4.9) onto GPIO1 (ADC1_CH0), gated by
 * ADC_CTRL on GPIO37. Heltec V3.1 uses active-LOW on GPIO37 while V3.2 uses
 * active-HIGH; probe both levels at init to detect the board revision. */
#include "battery.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum {
    ADC_CTRL_PIN = 37,
    BAT_ADC_SAMPLES = 16,
};

/* 390k + 100k voltage divider -> Vbat = Vadc * 4.9 */
#define VBAT_DIVIDER_NUM 49
#define VBAT_DIVIDER_DEN 10

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t cali_handle;
static bool bat_ready;
static int ctrl_active_level = 0;
static int smooth_mv = -1;

static int read_adc_mv_at_level(int level) {
    gpio_set_level(ADC_CTRL_PIN, level);
    vTaskDelay(pdMS_TO_TICKS(10));

    int sum = 0, valid = 0;
    for (int i = 0; i < BAT_ADC_SAMPLES; i++) {
        int raw = 0;
        if (adc_oneshot_read(adc_handle, ADC_CHANNEL_0, &raw) == ESP_OK) {
            sum += raw;
            valid++;
        }
    }
    if (!valid) return -1;
    int avg_raw = sum / valid;

    int adc_mv = 0;
    if (cali_handle) {
        if (adc_cali_raw_to_voltage(cali_handle, avg_raw, &adc_mv) != ESP_OK) {
            return -1;
        }
    } else {
        /* Fallback for ADC_ATTEN_DB_2_5 (~1250 mV full scale, 12-bit). */
        adc_mv = (avg_raw * 1250) / 4095;
    }
    return adc_mv;
}

esp_err_t battery_init(void) {
    gpio_config_t ctrl = {
        .pin_bit_mask = 1ULL << ADC_CTRL_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    esp_err_t err = gpio_config(&ctrl);
    if (err != ESP_OK) return err;

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    err = adc_oneshot_new_unit(&unit_cfg, &adc_handle);
    if (err != ESP_OK) return err;

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_2_5,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_0, &chan_cfg);
    if (err != ESP_OK) return err;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHANNEL_0,
        .atten = ADC_ATTEN_DB_2_5,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle) != ESP_OK) {
        cali_handle = NULL;
    }
#endif

    /* Auto-detect V3.1 (active LOW) vs V3.2 (active HIGH) ADC_CTRL polarity. */
    int mv_low = read_adc_mv_at_level(0);
    int mv_high = read_adc_mv_at_level(1);
    ctrl_active_level = (mv_high > mv_low + 80) ? 1 : 0;
    gpio_set_level(ADC_CTRL_PIN, !ctrl_active_level);

    int best = ctrl_active_level ? mv_high : mv_low;
    if (best > 0) {
        smooth_mv = (best * VBAT_DIVIDER_NUM + VBAT_DIVIDER_DEN / 2) / VBAT_DIVIDER_DEN;
    }
    bat_ready = true;
    return ESP_OK;
}

int battery_read_mv(void) {
    if (!bat_ready) return -1;
    int adc_mv = read_adc_mv_at_level(ctrl_active_level);
    gpio_set_level(ADC_CTRL_PIN, !ctrl_active_level);
    if (adc_mv < 0) return smooth_mv;

    int vbat_mv = (adc_mv * VBAT_DIVIDER_NUM + VBAT_DIVIDER_DEN / 2) / VBAT_DIVIDER_DEN;
    if (smooth_mv < 0) {
        smooth_mv = vbat_mv;
    } else {
        /* Exponential moving average (alpha = 0.25) to prevent 1% jitter. */
        smooth_mv = (smooth_mv * 3 + vbat_mv + 2) / 4;
    }
    return smooth_mv;
}

int battery_percent(int mv) {
    if (mv < 0) return -1;
    static const struct { int mv; int pct; } table[] = {
        {4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 80},
        {3980,  75}, {3950, 70}, {3910, 65}, {3870, 60}, {3850, 55},
        {3840,  50}, {3820, 45}, {3800, 40}, {3790, 35}, {3770, 30},
        {3750,  25}, {3730, 20}, {3710, 15}, {3690, 10}, {3610,  5},
        {3270,   0}
    };
    const int n = (int)(sizeof(table) / sizeof(table[0]));
    if (mv >= table[0].mv) return 100;
    if (mv <= table[n - 1].mv) return 0;
    for (int i = 0; i < n - 1; i++) {
        if (mv >= table[i + 1].mv) {
            int span = table[i].mv - table[i + 1].mv;
            int delta = mv - table[i + 1].mv;
            return table[i + 1].pct + (delta * (table[i].pct - table[i + 1].pct) + span / 2) / span;
        }
    }
    return 0;
}
