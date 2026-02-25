#ifndef SENSOR_H
#define SENSOR_H

#include "esp_err.h"
#include <stdbool.h>

// Initialize I2C bus and configure INA260 sensor.
// Returns ESP_OK on success. Sets internal ina260_ok flag.
esp_err_t sensor_init(void);

// Read INA260 bus voltage in volts. Returns 0.0 if sensor not found.
float ina260_read_voltage(void);

// Read INA260 current in milliamps. Returns 0.0 if sensor not found.
float ina260_read_current(void);

// Scan I2C bus and log all found devices. Diagnostic only.
void i2c_scan(void);

// Returns true if INA260 was detected during sensor_init().
bool sensor_is_ready(void);

#endif // SENSOR_H
