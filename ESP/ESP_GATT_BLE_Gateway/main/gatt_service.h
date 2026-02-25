#ifndef GATT_SERVICE_H
#define GATT_SERVICE_H

#include <stdint.h>
#include "esp_err.h"
#include "host/ble_hs.h"

#define DC_MONITOR_SERVICE_UUID 0xDC01
#define SENSOR_DATA_CHAR_UUID 0xDC02 // Read/Notify - data from mesh
#define COMMAND_CHAR_UUID 0xDC03     // Write - commands from Pi 5

#define SENSOR_DATA_MAX_LEN 128
#define COMMAND_MAX_LEN 64
#define GATT_MAX_PAYLOAD 20

extern uint16_t gatt_conn_handle;

void gatt_notify_sensor_data(const char *data, uint16_t len);
esp_err_t gatt_register_services(void);
void gatt_start_advertising(void);

#endif /* GATT_SERVICE_H */
