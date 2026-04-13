/* MeshConfig.cpp
 *
 * Definitions for all MeshConfig static members, including the ESP-IDF
 * BLE Mesh stack objects that must be initialised with C-style designated
 * initialisers (ESP-IDF macros).  The two helper functions for building
 * common message parameters are also implemented here.
 */

#include "MeshConfig.h"
#include "MeshConstants.h"

#include <cstring>

#include "esp_log.h"

static const char* TAG = "MESH_CFG";

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------

uint8_t                  MeshConfig::dev_uuid[16]              = {};
MeshConfig::KeyMaterial  MeshConfig::prov_key                  = {};
bool                     MeshConfig::provisioning_in_progress  = false;
bool                     MeshConfig::netkey_ready              = false;
bool                     MeshConfig::appkey_ready              = false;

esp_ble_mesh_client_t    MeshConfig::config_client             = {};
esp_ble_mesh_client_t    MeshConfig::onoff_client              = {};

// ---------------------------------------------------------------------------
// Configuration server settings (file-local; used only to initialise the
// CFG_SRV model below)
// ---------------------------------------------------------------------------
static esp_ble_mesh_cfg_srv_t s_config_server = {
    .net_transmit        = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay               = ESP_BLE_MESH_RELAY_DISABLED,
    .relay_retransmit    = ESP_BLE_MESH_TRANSMIT(2, 20),
    .beacon              = ESP_BLE_MESH_BEACON_ENABLED,
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy          = ESP_BLE_MESH_GATT_PROXY_ENABLED,
#else
    .gatt_proxy          = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state        = ESP_BLE_MESH_FRIEND_ENABLED,
#else
    .friend_state        = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
    .default_ttl         = 7,
};

// ---------------------------------------------------------------------------
// Model array: CFG_SRV | CFG_CLI | GEN_ONOFF_CLI
// root_models[1] (CFG_CLI) is the model used for all config operations.
// root_models[2] (GEN_ONOFF_CLI) is used for application-level commands.
// ---------------------------------------------------------------------------
esp_ble_mesh_model_t MeshConfig::root_models[3] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&s_config_server),
    ESP_BLE_MESH_MODEL_CFG_CLI(&MeshConfig::config_client),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(nullptr, &MeshConfig::onoff_client),
};

// ---------------------------------------------------------------------------
// Single element containing all three models
// ---------------------------------------------------------------------------
esp_ble_mesh_elem_t MeshConfig::elements[1] = {
    ESP_BLE_MESH_ELEMENT(0, MeshConfig::root_models, ESP_BLE_MESH_MODEL_NONE),
};

// ---------------------------------------------------------------------------
// Device composition (company ID + element list)
// ---------------------------------------------------------------------------
esp_ble_mesh_comp_t MeshConfig::composition = {
    .cid           = CID_ESP,
    .element_count = ARRAY_SIZE(MeshConfig::elements),
    .elements      = MeshConfig::elements,
};

// ---------------------------------------------------------------------------
// Provisioner configuration
// prov_uuid points to dev_uuid which is filled at runtime by
// ble_mesh_get_dev_uuid() before esp_ble_mesh_init() is called.
// ---------------------------------------------------------------------------
esp_ble_mesh_prov_t MeshConfig::provision = {
    .prov_uuid          = MeshConfig::dev_uuid,
    .prov_unicast_addr  = PROV_OWN_ADDR,
    .prov_start_address = 0x0005, // First provisioned node gets 0x0005
    .prov_attention     = 0x00,
    .prov_algorithm     = 0x00,
    .prov_pub_key_oob   = 0x00,
    .prov_static_oob_val = nullptr,
    .prov_static_oob_len = 0x00,
    .flags              = 0x00,
    .iv_index           = 0x00,
};

// ---------------------------------------------------------------------------
// Helper implementations
// ---------------------------------------------------------------------------

esp_err_t MeshConfig::setConfigCommon(
    esp_ble_mesh_client_common_param_t* common,
    uint16_t                            unicast_addr,
    esp_ble_mesh_model_t*               model,
    uint32_t                            opcode)
{
    if (!common || !model) {
        return ESP_ERR_INVALID_ARG;
    }

    common->opcode        = opcode;
    common->model         = model;
    common->ctx.net_idx   = prov_key.net_idx;
    common->ctx.app_idx   = prov_key.app_idx;
    common->ctx.addr      = unicast_addr;
    common->ctx.send_ttl  = MSG_SEND_TTL;
    common->msg_timeout   = MSG_TIMEOUT;
    common->msg_role      = ROLE_PROVISIONER;

    return ESP_OK;
}

esp_err_t MeshConfig::setMsgCommon(
    esp_ble_mesh_client_common_param_t* common,
    MeshNode*                           node,
    esp_ble_mesh_model_t*               model,
    uint32_t                            opcode)
{
    if (!common || !node || !model) {
        return ESP_ERR_INVALID_ARG;
    }

    common->opcode        = opcode;
    common->model         = model;
    common->ctx.net_idx   = prov_key.net_idx;
    common->ctx.app_idx   = prov_key.app_idx;
    common->ctx.addr      = node->unicast;
    common->ctx.send_ttl  = MSG_SEND_TTL;
    common->msg_timeout   = MSG_TIMEOUT;
    common->msg_role      = ROLE_PROVISIONER;

    return ESP_OK;
}
