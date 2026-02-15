/**
 * BLE Service Header for DC Monitor Node
 * Custom GATT service for sensor data and remote commands
 */

#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include <stdint.h>

// Custom Service UUID: DC Monitor Service
#define DC_MONITOR_SERVICE_UUID         0xDC01

// Characteristic UUIDs
#define SENSOR_DATA_CHAR_UUID           0xDC02  // Read/Notify - sensor readings
#define COMMAND_CHAR_UUID               0xDC03  // Write - commands from gateway

// Maximum sizes
#define SENSOR_DATA_MAX_LEN             128     // Sensor data + status messages
#define COMMAND_MAX_LEN                 32      // Commands: DUTY:50, RAMP, MONITOR, STOP, READ

// Initialize the BLE stack and start advertising
void ble_service_init(void);

// Update sensor data (called when UART data received from Pico)
void ble_update_sensor_data(const char *data, uint16_t len);

// Callback type for when gateway sends a command
typedef void (*command_callback_t)(const char *cmd, uint16_t len);
void ble_set_command_callback(command_callback_t cb);

#endif // BLE_SERVICE_H
