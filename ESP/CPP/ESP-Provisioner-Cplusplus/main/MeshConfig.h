/* MeshConfig.h
 *
 * Owns the ESP-IDF BLE Mesh stack objects (composition, models, keys, provision
 * config) and the shared provisioner state flags.  All members are static so
 * that the rest of the codebase can access them without carrying a pointer to
 * a MeshConfig instance.
 *
 * Note: The actual mesh stack structs (root_models, elements, composition,
 * provision) are defined with ESP-IDF macros at file scope in MeshConfig.cpp.
 * They are exposed here as static references.
 */

#pragma once

#include <cstdint>

#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_err.h"

#include "MeshNode.h"

class MeshConfig {
public:
    // -----------------------------------------------------------------------
    // Key material
    // -----------------------------------------------------------------------
    struct KeyMaterial {
        uint16_t net_idx = 0;
        uint16_t app_idx = 0;
        uint8_t  net_key[16] = {};
        uint8_t  app_key[16] = {};
    };

    // -----------------------------------------------------------------------
    // Provisioner state (shared across all modules)
    // -----------------------------------------------------------------------
    static uint8_t     dev_uuid[16];
    static KeyMaterial prov_key;
    static bool        provisioning_in_progress;
    static bool        netkey_ready;
    static bool        appkey_ready;

    // -----------------------------------------------------------------------
    // ESP-IDF client model handles
    // -----------------------------------------------------------------------
    static esp_ble_mesh_client_t config_client;
    static esp_ble_mesh_client_t onoff_client;

    // -----------------------------------------------------------------------
    // Mesh stack objects (defined in MeshConfig.cpp with ESP-IDF macros)
    // -----------------------------------------------------------------------
    static esp_ble_mesh_model_t  root_models[3];
    static esp_ble_mesh_elem_t   elements[1];
    static esp_ble_mesh_comp_t   composition;
    static esp_ble_mesh_prov_t   provision;

    // -----------------------------------------------------------------------
    // Helper: build common params for CONFIG CLIENT messages.
    // Config messages are encrypted with the NetKey (app_idx is KEY_UNUSED).
    // msg_role MUST be ROLE_PROVISIONER or the stack rejects the NetKeyIndex.
    // -----------------------------------------------------------------------
    static esp_err_t setConfigCommon(esp_ble_mesh_client_common_param_t* common,
                                     uint16_t                             unicast_addr,
                                     esp_ble_mesh_model_t*                model,
                                     uint32_t                             opcode);

    // Helper: build common params for GENERIC CLIENT messages (AppKey).
    static esp_err_t setMsgCommon(esp_ble_mesh_client_common_param_t* common,
                                  MeshNode*                            node,
                                  esp_ble_mesh_model_t*                model,
                                  uint32_t                             opcode);
};
