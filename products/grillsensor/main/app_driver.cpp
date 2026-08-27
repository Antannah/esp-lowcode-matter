#include <stdio.h>
#include <string.h>
#include <math.h>
#include <system.h>
#include <low_code.h>
#include <esp_log.h>
#include "soc/soc.h"
#include "soc/apb_saradc_struct.h"
#include "soc/pcr_struct.h"
#include "app_priv.h"

static const char *TAG = "app_driver";

#define PROBE1_CHANNEL 0
#define PROBE2_CHANNEL 1

#define ONESHOT_MODE_BIT      (1U << 0)
#define ONESHOT_CH_SHIFT      1
#define ONESHOT_ATTEN_SHIFT   23
#define ONESHOT_START_BIT     (1U << 25)
#define ONESHOT_DONE_BIT      (1U << 26)
#define ONESHOT_DATA_SHIFT    27

static inline void lp_delay_cycles(uint32_t cycles)
{
    for (uint32_t i = 0; i < cycles; ++i) {
        __asm__ __volatile__("nop");
    }
}

static inline double constrain_val(double val, double min, double max)
{
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

static float calcFantast(int raw_adc_12bit)
{
    if (raw_adc_12bit <= 0) return -40.0f;
    if (raw_adc_12bit >= 4095) return 300.0f;

    double sensorValue10Bit = (double)raw_adc_12bit / 4.0;
    const double p0 = 0.6390482;
    const double p1 = 3.78729949;
    const double p2 = -29.58798407;
    const double p3 = 60.75987107;

    double U = constrain_val(sensorValue10Bit, 1.0, 930.0) / 1024.0;
    double lnU = log(U / (1.0 - U));
    return (float)(p3 + (p2 * lnU) + (p1 * pow(lnU, 2)) + (p0 * pow(lnU, 3)));
}

static void send_temperature_update(uint16_t endpoint_id, float temp_celsius)
{
    low_code_feature_data_t data;
    memset(&data, 0, sizeof(low_code_feature_data_t));

    data.details.endpoint_id = endpoint_id;
    data.details.feature_id = LOW_CODE_FEATURE_ID_TEMPERATURE_SENSOR_VALUE;
    data.value.type = LOW_CODE_VALUE_TYPE_FLOAT;
    data.value.value = (uint8_t *)&temp_celsius;
    data.value.value_len = sizeof(float);

    low_code_feature_update_to_system(&data);
}

static int read_adc_channel_direct(uint8_t channel)
{
    uint32_t reg_val = ONESHOT_MODE_BIT |
                       ((uint32_t)(channel & 0x7) << ONESHOT_CH_SHIFT) |
                       (3U << ONESHOT_ATTEN_SHIFT);
    APB_SARADC.saradc_onetime_sample.val = reg_val;
    lp_delay_cycles(100);

    APB_SARADC.saradc_onetime_sample.val = reg_val | ONESHOT_START_BIT;

    int timeout = 2000;
    while (!(APB_SARADC.saradc_onetime_sample.val & ONESHOT_DONE_BIT) && --timeout > 0) {
        lp_delay_cycles(50);
    }

    uint32_t final_val = APB_SARADC.saradc_onetime_sample.val;
    int raw = (final_val >> ONESHOT_DATA_SHIFT) & 0x0FFF;

    APB_SARADC.saradc_onetime_sample.val = reg_val;
    return raw;
}

int app_driver_init(void)
{
    // SAR-ADC Bus & Clock auf ESP32-C6 aktivieren
    PCR.saradc_clkm_conf.saradc_clkm_en = 1;
    PCR.saradc_conf.saradc_reg_clk_en = 1;
    PCR.saradc_conf.saradc_rst_en = 0;

    ESP_LOGI(TAG, "ADC-Treiber initialisiert.");
    return 0;
}

int app_driver_feature_update(void)
{
    int raw1 = read_adc_channel_direct(PROBE1_CHANNEL);
    int raw2 = read_adc_channel_direct(PROBE2_CHANNEL);

    float temp1 = calcFantast(raw1);
    float temp2 = calcFantast(raw2);

    int t1_int = (int)temp1;
    int t1_dec = (int)(fabsf(temp1 - (float)t1_int) * 10.0f);
    int t2_int = (int)temp2;
    int t2_dec = (int)(fabsf(temp2 - (float)t2_int) * 10.0f);

    ESP_LOGI(TAG, "Messung -> F1: %d.%d C (RAW: %d) | F2: %d.%d C (RAW: %d)",
             t1_int, t1_dec, raw1, t2_int, t2_dec, raw2);

    // Beide Endpoints pushen
    send_temperature_update(1, temp1);
    send_temperature_update(2, temp2);

    return 0;
}

int app_driver_event_handler(low_code_event_t *event)
{
    return 0;
}