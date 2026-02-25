/* Mesh configuration: keys, models, composition, and helper functions */

#include "esp_log.h"

#include "mesh_config.h"

#define TAG "MESH_CFG"

uint8_t dev_uuid[16];

// Prevent duplicate provisioning attempts
bool provisioning_in_progress = false;

// Track when local keys are ready for use
bool netkey_ready = false;
bool appkey_ready = false;

// Network/App keys
struct esp_ble_mesh_key prov_key;

// Client models
esp_ble_mesh_client_t config_client;
esp_ble_mesh_client_t onoff_client;

// Configuration server
static esp_ble_mesh_cfg_srv_t config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
#else
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
    .default_ttl = 7,
};

// Models
esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_CFG_CLI(&config_client),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(NULL, &onoff_client),
};

esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, ESP_BLE_MESH_MODEL_NONE),
};

esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

// Provisioner configuration
esp_ble_mesh_prov_t provision = {
    .prov_uuid = dev_uuid,
    .prov_unicast_addr = PROV_OWN_ADDR,
    .prov_start_address = 0x0005, // First node gets address 0x0005
    .prov_attention = 0x00,
    .prov_algorithm = 0x00,
    .prov_pub_key_oob = 0x00,
    .prov_static_oob_val = NULL,
    .prov_static_oob_len = 0x00,
    .flags = 0x00,
    .iv_index = 0x00,
};

// Set common parameters for CONFIG CLIENT messages (uses NetKey only, NOT
// AppKey) Config messages are encrypted with NetKey, so app_idx must be
// ESP_BLE_MESH_KEY_UNUSED
esp_err_t set_config_common(esp_ble_mesh_client_common_param_t *common,
                            uint16_t unicast_addr,
                            esp_ble_mesh_model_t *model,
                            uint32_t opcode) {
  if (!common || !model) {
    return ESP_ERR_INVALID_ARG;
  }

  common->opcode = opcode;
  common->model = model;
  common->ctx.net_idx = prov_key.net_idx;
  common->ctx.app_idx = prov_key.app_idx;
  common->ctx.addr = unicast_addr;
  common->ctx.send_ttl = MSG_SEND_TTL;
  common->msg_timeout = MSG_TIMEOUT;
  common->msg_role = ROLE_PROVISIONER;

  return ESP_OK;
}

// Set common parameters for GENERIC CLIENT messages (uses AppKey)
// Application messages are encrypted with AppKey
esp_err_t set_msg_common(esp_ble_mesh_client_common_param_t *common,
                         mesh_node_info_t *node,
                         esp_ble_mesh_model_t *model, uint32_t opcode) {
  if (!common || !node || !model) {
    return ESP_ERR_INVALID_ARG;
  }

  common->opcode = opcode;
  common->model = model;
  common->ctx.net_idx = prov_key.net_idx;
  common->ctx.app_idx = prov_key.app_idx;
  common->ctx.addr = node->unicast;
  common->ctx.send_ttl = MSG_SEND_TTL;
  common->msg_timeout = MSG_TIMEOUT;
  common->msg_role = ROLE_PROVISIONER;

  return ESP_OK;
}
