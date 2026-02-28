#ifndef MESH_NODE_H
#define MESH_NODE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_ble_mesh_defs.h"

// ============== Shared Definitions ==============
#define CID_ESP 0x02E5

// Vendor model IDs (shared with gateway and provisioner)
#define VND_MODEL_ID_SERVER 0x0001
#define VND_MODEL_ID_CLIENT 0x0000

// Vendor opcodes
#define VND_OP_SEND   ESP_BLE_MESH_MODEL_OP_3(0x00, CID_ESP)
#define VND_OP_STATUS ESP_BLE_MESH_MODEL_OP_3(0x01, CID_ESP)

#define MESH_GROUP_ADDR 0xC000
#define VND_SEND_TIMEOUT_MS 5000

// Mesh node state - persisted to NVS
struct mesh_node_state {
  uint16_t net_idx;
  uint16_t app_idx;
  uint16_t addr;
  uint8_t onoff;
  uint8_t tid;
  uint8_t vnd_bound_flag; // Persisted: vendor client model bound to AppKey
} __attribute__((packed));

// Global mesh state (extern for other modules)
extern struct mesh_node_state node_state;
extern uint16_t cached_net_idx;
extern uint16_t cached_app_idx;
extern uint8_t dev_uuid[16];

// Vendor client state (used by command_parser, monitor, etc.)
extern bool vnd_bound;
extern bool vnd_send_busy;
extern uint16_t vnd_send_target_addr;
extern uint16_t monitor_target_addr;
extern bool monitor_waiting_response;

// Initialize BLE Mesh stack, register callbacks, enable provisioning
esp_err_t ble_mesh_init(void);

// Send OnOff command to a mesh node (fallback path)
esp_err_t send_mesh_onoff(uint16_t target_addr, uint8_t onoff);

// Send vendor command to a mesh node or group
esp_err_t send_vendor_command(uint16_t target_addr, const char *cmd, uint16_t len);

#endif // MESH_NODE_H
