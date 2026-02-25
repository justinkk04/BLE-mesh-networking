#ifndef MESH_NODE_H
#define MESH_NODE_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_ble_mesh_defs.h"

// Mesh node state — persisted to NVS
struct mesh_node_state {
  uint16_t net_idx;
  uint16_t app_idx;
  uint16_t addr;
  uint8_t onoff;
  uint8_t tid;
} __attribute__((packed));

// Global mesh state (extern for nvs_store.c and command.c access)
extern struct mesh_node_state node_state;
extern uint16_t cached_net_idx;
extern uint16_t cached_app_idx;
extern uint8_t dev_uuid[16];

// Initialize BLE Mesh stack, register callbacks, enable provisioning
esp_err_t ble_mesh_init(void);

#endif // MESH_NODE_H
