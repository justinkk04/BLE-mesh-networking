#ifndef MESH_GATEWAY_H
#define MESH_GATEWAY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_ble_mesh_common_api.h"

#define CID_ESP 0x02E5

// Vendor model definitions (shared with mesh node and provisioner)
#define VND_MODEL_ID_CLIENT 0x0000
#define VND_OP_SEND   ESP_BLE_MESH_MODEL_OP_3(0x00, CID_ESP)
#define VND_OP_STATUS ESP_BLE_MESH_MODEL_OP_3(0x01, CID_ESP)

#define MESH_GROUP_ADDR 0xC000 // Group address for ALL commands
#define VND_SEND_TIMEOUT_MS 5000  // Auto-clear after 5s (match mesh SDK timeout)

extern uint8_t dev_uuid[16];
extern bool vnd_bound;
extern bool vnd_send_busy;
extern uint16_t vnd_send_target_addr;
extern uint16_t cached_net_idx;
extern uint16_t cached_app_idx;
extern uint16_t monitor_target_addr;
extern bool monitor_waiting_response;

esp_err_t send_mesh_onoff(uint16_t target_addr, uint8_t onoff);
esp_err_t send_vendor_command(uint16_t target_addr, const char *cmd, uint16_t len);
esp_err_t mesh_init(void);

#endif /* MESH_GATEWAY_H */
