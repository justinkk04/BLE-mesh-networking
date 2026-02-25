/* Mesh configuration: keys, models, composition, and helper functions */

#ifndef MESH_CONFIG_H
#define MESH_CONFIG_H

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"

#include "ble_mesh_example_init.h"

#include "node_registry.h"

#define CID_ESP 0x02E5

// Vendor model IDs (shared with gateway and mesh node)
#define VND_MODEL_ID_CLIENT 0x0000
#define VND_MODEL_ID_SERVER 0x0001

#define MESH_GROUP_ADDR 0xC000 // Group address for ALL commands

#define PROV_OWN_ADDR 0x0001 // Provisioner's own address

#define MSG_SEND_TTL 3
#define MSG_TIMEOUT 0

#define COMP_DATA_PAGE_0 0x00

#define APP_KEY_IDX 0x0000
#define APP_KEY_OCTET 0x12

// Network/App keys
#define NET_KEY_OCTET 0x11

// UUID prefix to match - devices must start with this
#define UUID_MATCH_PREFIX                                                      \
  { 0xdd, 0xdd }
#define UUID_MATCH_LEN 2

extern uint8_t dev_uuid[16];

// Prevent duplicate provisioning attempts
extern bool provisioning_in_progress;

// Track when local keys are ready for use
extern bool netkey_ready;
extern bool appkey_ready;

extern struct esp_ble_mesh_key {
  uint16_t net_idx;
  uint16_t app_idx;
  uint8_t net_key[16];
  uint8_t app_key[16];
} prov_key;

// Client models
extern esp_ble_mesh_client_t config_client;
extern esp_ble_mesh_client_t onoff_client;

// Models
extern esp_ble_mesh_model_t root_models[];

// Elements and composition
extern esp_ble_mesh_elem_t elements[];
extern esp_ble_mesh_comp_t composition;

// Provisioner configuration
extern esp_ble_mesh_prov_t provision;

// Set common parameters for CONFIG CLIENT messages (uses NetKey only, NOT
// AppKey) Config messages are encrypted with NetKey, so app_idx must be
// ESP_BLE_MESH_KEY_UNUSED
esp_err_t set_config_common(esp_ble_mesh_client_common_param_t *common,
                            uint16_t unicast_addr,
                            esp_ble_mesh_model_t *model,
                            uint32_t opcode);

// Set common parameters for GENERIC CLIENT messages (uses AppKey)
// Application messages are encrypted with AppKey
esp_err_t set_msg_common(esp_ble_mesh_client_common_param_t *common,
                         mesh_node_info_t *node,
                         esp_ble_mesh_model_t *model, uint32_t opcode);

#endif /* MESH_CONFIG_H */
