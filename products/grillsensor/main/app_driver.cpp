#include <stdio.h>
#include <string.h>
#include <math.h>
#include <system.h>
#include <low_code.h>
#include <esp_log.h>
#include "esp_adc/adc_oneshot.h"
#include "app_priv.h"

static const char *TAG = "app_driver";

// Seeed Studio XIAO ESP32-C6 Pinout & ADC-Kanäle:
#define PROBE1_CHANNEL      0   // D0 / GPIO 0 (Fühler 1: Kerntemperatur)
#define PROBE2_CHANNEL      1   // D1 / GPIO 1 (Fühler 2: Garraum)
#define BAT_ADC_CHANNEL     2   // D2 / GPIO 2 (Batteriespannung über 1M / 1M Teiler)

// D3 / GPIO 21 versorgt die Pull-Up-Widerstände geschaltet nur während der Messung (Power-Gating)
#define SENSOR_PWR_GPIO     21

// HF-Antennenumschaltung (Seeed Studio XIAO ESP32-C6 Onboard RF Switch)
#define ANT_CTRL_EN_GPIO    3   // LOW = aktiviert RF Switch Control
#define ANT_SEL_GPIO        14  // LOW = Interne Keramikantenne, HIGH = Externe U.FL Antenne
#define USE_EXTERNAL_ANT    1   // 1 = Externe U.FL / Stabantenne, 0 = Interne Keramikantenne

// Einschwingzeit nach Aktivieren von SENSOR_PWR_GPIO (RC-Filter: 47k * 100nF = ~4.7 ms -> 5*tau ~ 25 ms)
#define SENSOR_SETTLING_TIME_MS 25

// Delta- und Heartbeat-Reporting Schwellwerte Temperatur
#define DELTA_TEMP_THRESHOLD        0.5f    // Sendet bei Temperaturänderung >= 0.5 °C
#define TEMP_HEARTBEAT_INTERVAL_MS  60000   // Sendet spätestens alle 60 Sekunden einen Heartbeat

// Delta- und Heartbeat-Reporting Schwellwerte Batterie (Matter Power Source Cluster 0x002F)
#define BAT_DELTA_MV_THRESHOLD      50      // Sendet bei Spannungsänderung >= 50 mV (0.05 V)
#define BAT_HEARTBEAT_INTERVAL_MS   600000  // Sendet spätestens alle 10 Minuten einen Heartbeat

// 12-Bit ADC Grenzwerte für IKEA Fantast NTC (entspricht 1.0 bzw. 930.0 in 10-Bit)
#define ADC_12BIT_MIN_RAW   4
#define ADC_12BIT_MAX_RAW   3720

static adc_oneshot_unit_handle_t s_adc1_handle = NULL;

struct sensor_endpoint_state_t {
    float last_reported_temp;
    uint32_t last_reported_time_ms;
    bool has_reported;
};

static sensor_endpoint_state_t s_sensor_states[2] = {
    { 0.0f, 0, false },
    { 0.0f, 0, false }
};

static uint32_t s_last_bat_voltage_mv = 0;
static uint32_t s_last_bat_report_time_ms = 0;
static bool s_bat_has_reported = false;

static inline void lp_delay_cycles(uint32_t cycles)
{
    for (uint32_t i = 0; i < cycles; ++i) {
        __asm__ __volatile__("nop");
    }
}

static float calcFantast(int raw_adc_12bit)
{
    static const double p0 = 0.6390482;
    static const double p1 = 3.78729949;
    static const double p2 = -29.58798407;
    static const double p3 = 60.75987107;

    if (raw_adc_12bit <= ADC_12BIT_MIN_RAW) return -40.0f;
    if (raw_adc_12bit >= ADC_12BIT_MAX_RAW) return 300.0f;

    double U = (double)raw_adc_12bit / 4096.0;
    double lnU = log(U / (1.0 - U));
    return (float)(p3 + (p2 * lnU) + (p1 * pow(lnU, 2)) + (p0 * pow(lnU, 3)));
}

static uint32_t calc_battery_voltage_mv(int raw_adc_12bit)
{
    // Bei 11dB Attenuation: 0..4095 entspricht ca. 0..3100 mV am Pin
    // Teilerfaktor 1M / 1M = 2.0 -> V_BAT = V_PIN * 2 = raw * 6200 / 4095
    if (raw_adc_12bit <= 0) return 0;
    return (uint32_t)(((uint64_t)raw_adc_12bit * 6200U) / 4095U);
}

static void send_temperature_update(uint16_t endpoint_id, float temp_celsius)
{
    // Matter Temperature Measurement erwartet int16_t in 0.01 °C (z. B. 21.50 °C -> 2150)
    int16_t temperature = (int16_t)roundf(temp_celsius * 100.0f);

    low_code_feature_data_t data;
    memset(&data, 0, sizeof(low_code_feature_data_t));

    data.details.endpoint_id = endpoint_id;
    data.details.feature_id = LOW_CODE_FEATURE_ID_TEMPERATURE_SENSOR_VALUE;
    data.value.type = LOW_CODE_VALUE_TYPE_INTEGER;
    data.value.value = (uint8_t *)&temperature;
    data.value.value_len = sizeof(int16_t);

    low_code_feature_update_to_system(&data);
}

static void send_battery_voltage_update(uint32_t voltage_mv)
{
    low_code_feature_data_t data;
    memset(&data, 0, sizeof(low_code_feature_data_t));

    // Endpoint 0 (Root Node) - Matter Power Source Cluster (0x002F) -> BatVoltage (0x000B)
    data.details.endpoint_id = 0;
    data.details.feature_id = LOW_CODE_FEATURE_ID_UNHANDLED;
    data.details.low_level.matter.cluster_id = 0x002F;   // Power Source Cluster
    data.details.low_level.matter.attribute_id = 0x000B; // BatVoltage (in mV)

    data.value.type = LOW_CODE_VALUE_TYPE_UNSIGNED_INTEGER;
    data.value.value = (uint8_t *)&voltage_mv;
    data.value.value_len = sizeof(uint32_t);

    low_code_feature_update_to_system(&data);
}

static void process_and_report_sensor(uint16_t endpoint_id, float current_temp, uint32_t now_ms)
{
    int idx = (endpoint_id == 1) ? 0 : 1;
    sensor_endpoint_state_t *state = &s_sensor_states[idx];

    bool delta_exceeded = !state->has_reported || (fabsf(current_temp - state->last_reported_temp) >= DELTA_TEMP_THRESHOLD);
    bool heartbeat_expired = (now_ms - state->last_reported_time_ms) >= TEMP_HEARTBEAT_INTERVAL_MS;

    if (delta_exceeded || heartbeat_expired) {
        send_temperature_update(endpoint_id, current_temp);
        state->last_reported_temp = current_temp;
        state->last_reported_time_ms = now_ms;
        state->has_reported = true;

        ESP_LOGI(TAG, "Endpoint %u -> Temperatur-Update: %.2f °C (Grund: %s)",
                 endpoint_id, current_temp, delta_exceeded ? "Delta >= 0.5 °C" : "Heartbeat 60s");
    }
}

static void process_and_report_battery(uint32_t voltage_mv, uint32_t now_ms)
{
    uint32_t delta = (voltage_mv > s_last_bat_voltage_mv) ?
                     (voltage_mv - s_last_bat_voltage_mv) :
                     (s_last_bat_voltage_mv - voltage_mv);

    bool delta_exceeded = !s_bat_has_reported || (delta >= BAT_DELTA_MV_THRESHOLD);
    bool heartbeat_expired = (now_ms - s_last_bat_report_time_ms) >= BAT_HEARTBEAT_INTERVAL_MS;

    if (delta_exceeded || heartbeat_expired) {
        send_battery_voltage_update(voltage_mv);
        s_last_bat_voltage_mv = voltage_mv;
        s_last_bat_report_time_ms = now_ms;
        s_bat_has_reported = true;

        ESP_LOGI(TAG, "Endpoint 0 -> Batterie-Update: %lu mV (Grund: %s)",
                 (unsigned long)voltage_mv, delta_exceeded ? "Delta >= 50 mV" : "Heartbeat 10 min");
    }
}

static int read_adc_channel_direct(uint8_t channel)
{
    if (!s_adc1_handle) {
        return 0;
    }
    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc1_handle, (adc_channel_t)channel, &raw);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_read CH%u fehlgeschlagen: %s", channel, esp_err_to_name(err));
        return 0;
    }
    return raw;
}

static void app_driver_timer_cb(system_timer_handle_t timer_handle, void *user_data)
{
    app_driver_feature_update();
}

int app_driver_init(void)
{
    // 1. Offiziellen ESP-IDF ADC-Oneshot-Treiber für ADC_UNIT_1 initialisieren
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_config1, &s_adc1_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit fehlgeschlagen: %s", esp_err_to_name(ret));
    } else {
        adc_oneshot_chan_cfg_t chan_config = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        adc_oneshot_config_channel(s_adc1_handle, (adc_channel_t)PROBE1_CHANNEL, &chan_config);
        adc_oneshot_config_channel(s_adc1_handle, (adc_channel_t)PROBE2_CHANNEL, &chan_config);
        adc_oneshot_config_channel(s_adc1_handle, (adc_channel_t)BAT_ADC_CHANNEL, &chan_config);
    }

    // 2. Seeed Studio XIAO ESP32-C6 Antennenschalter konfigurieren
    system_set_pin_mode(ANT_CTRL_EN_GPIO, OUTPUT);
    system_digital_write(ANT_CTRL_EN_GPIO, LOW);  // RF Switch aktivieren
    system_set_pin_mode(ANT_SEL_GPIO, OUTPUT);
    system_digital_write(ANT_SEL_GPIO, USE_EXTERNAL_ANT ? HIGH : LOW); // Externe / Interne Antenne

    // 3. Sensor-Power-Gating testweise dauerhaft auf HIGH (dauerhaft 3.3V an D3)
    system_set_pin_mode(SENSOR_PWR_GPIO, OUTPUT);
    system_digital_write(SENSOR_PWR_GPIO, HIGH);

    ESP_LOGI(TAG, "XIAO ESP32-C6 Treiber init: PWR-Pin=D3(GPIO%d), Fühler1=D0(CH0), Fühler2=D1(CH1), Bat=D2(CH2), Antenne=%s",
             SENSOR_PWR_GPIO, USE_EXTERNAL_ANT ? "Extern (U.FL)" : "Intern (Keramik)");

    // 4. Periodischen Timer alle 5 Sekunden (5000 ms) starten
    system_timer_handle_t timer = system_timer_create(app_driver_timer_cb, NULL, 5000, true);
    if (timer) {
        system_timer_start(timer);
    } else {
        ESP_LOGE(TAG, "Fehler beim Erstellen des Sensor-Timers!");
    }

    return 0;
}

int app_driver_feature_update(void)
{
    // Power-Gating testweise deaktiviert: D3 bleibt dauerhaft auf HIGH (3.3V)
    // 1. ADC-Kanäle abtasten: D0 (CH0), D1 (CH1), D2 (CH2)
    int raw1 = read_adc_channel_direct(PROBE1_CHANNEL);
    int raw2 = read_adc_channel_direct(PROBE2_CHANNEL);
    int raw_bat = read_adc_channel_direct(BAT_ADC_CHANNEL);

    // 5. Messwerte berechnen
    float temp1 = calcFantast(raw1);
    float temp2 = calcFantast(raw2);
    uint32_t bat_mv = calc_battery_voltage_mv(raw_bat);

    int t1_int = (int)temp1;
    int t1_dec = (int)(fabsf(temp1 - (float)t1_int) * 10.0f);
    int t2_int = (int)temp2;
    int t2_dec = (int)(fabsf(temp2 - (float)t2_int) * 10.0f);

    ESP_LOGI(TAG, "Messung -> F1: %d.%d °C (RAW: %d) | F2: %d.%d °C (RAW: %d) | Batterie: %lu mV (RAW: %d)",
             t1_int, t1_dec, raw1, t2_int, t2_dec, raw2, (unsigned long)bat_mv, raw_bat);

    // 6. Delta- und Heartbeat-Reporting ausführen
    uint32_t now = system_get_time();
    process_and_report_sensor(1, temp1, now);
    process_and_report_sensor(2, temp2, now);
    process_and_report_battery(bat_mv, now);

    return 0;
}

int app_driver_event_handler(low_code_event_t *event)
{
    return 0;
}
