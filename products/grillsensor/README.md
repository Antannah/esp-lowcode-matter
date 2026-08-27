# Temperature Sensor (I2C Driver) | SHT30 Sensor

## Description

A temperature sensing product using the SHT30 temperature sensor with periodic reporting, factory reset via button, and device state indication through a WS2812 RGB LED.

* **Periodic Temperature Monitoring**: Measures ambient temperature using the I2C Driver SHT30 sensor and reports the data to the system every 10 seconds.
* **Factory Reset via Button**: Holding the user-configurable button for more than 5 seconds and then releasing it triggers a factory reset event.
* **Device State Indication**: The WS2812 RGB LED visually indicates the device’s current state — for example, a blinking LED signifies setup mode.
* **Matter Data Model Specification**:
  * **Device Type**: `Temperature Sensor`

## Hardware Configuration

<img src="../../docs/images/product_temperature_sensor.png" alt="Temperature Sensor SHT30 (I2C Driver)" width="500"/>

The following components are used for this product:

* **Devkit**: [M5Stack Nano C6 Dev Kit](https://shop.m5stack.com/products/m5stack-nanoc6-dev-kit?srsltid=AfmBOooXsbm_fgpDyK1yWqgPOwtjrL3WksxGlhmRKDZFmVj2omLLbWDX)
* **Temperature Sensor**: [SHT30](https://shop.m5stack.com/products/env-iii-unit-with-temperature-humidity-air-pressure-sensor-sht30-qmp6988) (I2C Driver)
* **LED Indicator**: WS2812
* **Button**: onboard boot button or external button.

You can use any **ESP32-C6 DevKit** as long as the pin connections match the specified GPIO assignments.

### Pin Assignment

**Note:** The following pin assignments are used by default.

| Peripheral       | Signal | ESP32-C6 GPIO      |
|------------------|--------|--------------------|
| **I2C - SHT30**  | SDA    | GPIO2              |
| **I2C - SHT30**  | SCL    | GPIO1              |
| **WS2812 LED**   | Data   | GPIO8              |
| **Button**       | Input  | GPIO9              |

> **Note**: These GPIOs can be reconfigured by updating the macro definitions in **app_driver.cpp**: `I2C_SDA_IO`, `I2C_SCL_IO`, `WS2812_CTRL_IO`, `BUTTON_GPIO_NUM`

## Understanding Code

### Initialization Sequence

The `app_driver_init()` function (called from `setup()` in `app_main.cpp`) handles:

* Configuring the button with debouncing
* Setting up the WS2812 LED
* Initializing I2C for the SHT30 sensor
* Creating a system timer that triggers every 10 seconds and calls `app_driver_read_and_report_feature()`

### Core Functions

Every 10 seconds, the timer callback:

* Reads temperature data from the SHT30 temperature sensor
* Reports it to the system via `app_driver_report_temperature()`

### Extending with Other I2C Sensors

To add support for any other I2C sensor, you can follow these steps:

* Initialize it in `app_driver_init()`
* Add reading logic in `app_driver_read_and_report_feature()`
* Implement a function to report its data

## Related Documents

* [Thermostat (Template) | Heating + Cooling](../thermostat/README.md)
* [Temperature Sensor (I2C Driver) | SHT30 Sensor | SSD1315 Display](../temperature_sensor_with_display/README.md)
* [Programmer's Model](../../docs/programmer_model.md)
* [Components](../../components/README.md)
* [Drivers](../../drivers/README.md)
* [Products](../README.md)
