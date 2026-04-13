/* BLEMeshProvisioner.cpp
 *
 * Implements BLE Mesh stack initialisation and all provisioning/config/generic
 * client event callbacks.
 */

#include "BLEMeshProvisioner.h"
#include "CompositionParser.h"
#include "MeshConfig.h"
#include "MeshConstants.h"

#include <cinttypes>
#include <cstring>

#include "esp_log.h"

static const char* TAG = "PROV";

// ---------------------------------------------------------------------------
// Singleton instance pointer (static member definition)
// ---------------------------------------------------------------------------
BLEMeshProvisioner* BLEMeshProvisioner::instance_ = nullptr;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
BLEMeshProvisioner::BLEMeshProvisioner()
    : registry_(),
      binder_(registry_)
{
    instance_ = this;
}

// ---------------------------------------------------------------------------
// Public: initialise the mesh stack
// ---------------------------------------------------------------------------
esp_err_t BLEMeshProvisioner::init() {
    // Fill key material
    MeshConfig::prov_key.net_idx = ESP_BLE_MESH_KEY_PRIMARY;
    MeshConfig::prov_key.app_idx = APP_KEY_IDX;
    memset(MeshConfig::prov_key.net_key, NET_KEY_OCTET,
           sizeof(MeshConfig::prov_key.net_key));
    memset(MeshConfig::prov_key.app_key, APP_KEY_OCTET,
           sizeof(MeshConfig::prov_key.app_key));

    // Register C-compatible callback functions
    esp_ble_mesh_register_prov_callback(provisioningCallback);
    esp_ble_mesh_register_config_client_callback(configClientCallback);
    esp_ble_mesh_register_generic_client_callback(genericClientCallback);

    // Initialise the mesh stack with our composition and provision config
    esp_err_t err = esp_ble_mesh_init(&MeshConfig::provision,
                                      &MeshConfig::composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Mesh init failed: %d", err);
        return err;
    }

    // Ensure client model pointers are set (may not be filled by macros)
    MeshConfig::config_client.model = &MeshConfig::root_models[1]; // CFG_CLI
    MeshConfig::onoff_client.model  = &MeshConfig::root_models[2]; // GEN_ONOFF_CLI

    // Set UUID prefix filter: only provision devices starting with 0xdd 0xdd
    const uint8_t match[UUID_MATCH_LEN] = { UUID_MATCH_BYTE0, UUID_MATCH_BYTE1 };
    err = esp_ble_mesh_provisioner_set_dev_uuid_match(match, sizeof(match),
                                                      0x0, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set UUID match failed: %d", err);
        return err;
    }

    // Enable provisioner on both PB-ADV and PB-GATT
    // Keys are added after this call completes (in PROV_ENABLE_COMP_EVT).
    err = esp_ble_mesh_provisioner_prov_enable(
        static_cast<esp_ble_mesh_prov_bearer_t>(ESP_BLE_MESH_PROV_ADV |
                                                ESP_BLE_MESH_PROV_GATT));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Enable provisioner failed: %d", err);
        return err;
    }

    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "  BLE Mesh Provisioner Ready");
    ESP_LOGI(TAG, "  Scanning for devices with UUID: 0x%02x 0x%02x ...",
             UUID_MATCH_BYTE0, UUID_MATCH_BYTE1);
    ESP_LOGI(TAG, "===========================================");

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Private: handle unprovisioned device advertisement
// ---------------------------------------------------------------------------
void BLEMeshProvisioner::onUnprovAdvPacket(
    uint8_t               dev_uuid[16],
    uint8_t               addr[BD_ADDR_LEN],
    esp_ble_mesh_addr_type_t addr_type,
    uint16_t              oob_info,
    uint8_t               adv_type,
    esp_ble_mesh_prov_bearer_t bearer)
{
    // Only one provisioning session at a time
    if (MeshConfig::provisioning_in_progress) {
        return;
    }

    // Keys must be ready before we can provision
    if (!MeshConfig::netkey_ready || !MeshConfig::appkey_ready) {
        ESP_LOGW(TAG, "Keys not ready yet (net=%d app=%d), ignoring unprov device",
                 MeshConfig::netkey_ready, MeshConfig::appkey_ready);
        return;
    }

    ESP_LOGI(TAG, "========== UNPROVISIONED DEVICE FOUND ==========");
    ESP_LOGI(TAG, "Address: %s (type: %d)", bt_hex(addr, BD_ADDR_LEN), addr_type);
    ESP_LOGI(TAG, "UUID: %s", bt_hex(dev_uuid, 16));
    ESP_LOGI(TAG, "Bearer: %s",
             (bearer & ESP_BLE_MESH_PROV_ADV) ? "PB-ADV" : "PB-GATT");
    ESP_LOGI(TAG, "================================================");

    esp_ble_mesh_unprov_dev_add_t add_dev = {};
    memcpy(add_dev.addr,  addr,     BD_ADDR_LEN);
    add_dev.addr_type = addr_type;
    memcpy(add_dev.uuid,  dev_uuid, 16);
    add_dev.oob_info  = oob_info;
    add_dev.bearer    = bearer;

    MeshConfig::provisioning_in_progress = true;

    const esp_err_t err = esp_ble_mesh_provisioner_add_unprov_dev(
        &add_dev,
        ADD_DEV_RM_AFTER_PROV_FLAG |
        ADD_DEV_START_PROV_NOW_FLAG |
        ADD_DEV_FLUSHABLE_DEV_FLAG);

    if (err) {
        ESP_LOGE(TAG, "Add unprovisioned device failed: %d", err);
        MeshConfig::provisioning_in_progress = false;
    } else {
        ESP_LOGI(TAG, "Started provisioning device...");
    }
}

// ---------------------------------------------------------------------------
// Private: provisioning complete
// ---------------------------------------------------------------------------
esp_err_t BLEMeshProvisioner::onProvisioningComplete(
    int                        node_idx,
    const esp_ble_mesh_octet16_t uuid,
    uint16_t                   unicast,
    uint8_t                    elem_num,
    uint16_t                   net_idx)
{
    ESP_LOGI(TAG, "========== PROVISIONING COMPLETE ==========");
    ESP_LOGI(TAG, "Node index:      %d",       node_idx);
    ESP_LOGI(TAG, "Unicast address: 0x%04x",   unicast);
    ESP_LOGI(TAG, "Element count:   %d",        elem_num);
    ESP_LOGI(TAG, "NetKey index:    0x%04x",   net_idx);
    ESP_LOGI(TAG, "Device UUID:     %s",        bt_hex(uuid, 16));
    ESP_LOGI(TAG, "============================================");

    // Assign a human-readable name
    char name[16] = {};
    snprintf(name, sizeof(name), "NODE-%d", node_idx);
    const esp_err_t name_err =
        esp_ble_mesh_provisioner_set_node_name(node_idx, name);
    if (name_err) {
        ESP_LOGE(TAG, "Set node name failed: %d", name_err);
    }

    // Store in registry
    esp_err_t err = registry_.storeNode(uuid, unicast, elem_num, node_idx);
    if (err) {
        ESP_LOGE(TAG, "Store node info failed");
        return ESP_FAIL;
    }

    MeshNode* node = registry_.getNode(unicast);
    if (!node) {
        ESP_LOGE(TAG, "Get node info failed after store");
        return ESP_FAIL;
    }

    // Request composition data to discover which models the node has.
    // Config messages use the NetKey (not AppKey).
    ESP_LOGI(TAG, "Sending COMP_DATA_GET to 0x%04x using net_idx 0x%04x",
             unicast, MeshConfig::prov_key.net_idx);

    esp_ble_mesh_client_common_param_t common  = {};
    esp_ble_mesh_cfg_client_get_state_t get_st = {};

    MeshConfig::setConfigCommon(&common, unicast,
                                &MeshConfig::root_models[1],
                                ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET);
    get_st.comp_data_get.page = COMP_DATA_PAGE_0;

    err = esp_ble_mesh_config_client_get_state(&common, &get_st);
    if (err) {
        ESP_LOGE(TAG, "Get composition data failed: %d", err);
        return ESP_FAIL;
    }

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Private: link open / close helpers
// ---------------------------------------------------------------------------
void BLEMeshProvisioner::onLinkOpen(esp_ble_mesh_prov_bearer_t bearer) {
    ESP_LOGI(TAG, "Provisioning link opened (%s)",
             bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
}

void BLEMeshProvisioner::onLinkClose(esp_ble_mesh_prov_bearer_t bearer,
                                      uint8_t reason) {
    ESP_LOGI(TAG, "Provisioning link closed (%s), reason: 0x%02x",
             bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT", reason);
}

// ===========================================================================
// Static callback: Provisioning events
// ===========================================================================
void BLEMeshProvisioner::provisioningCallback(
    esp_ble_mesh_prov_cb_event_t  event,
    esp_ble_mesh_prov_cb_param_t* param)
{
    BLEMeshProvisioner& self = *instance_;

    switch (event) {
    // -------------------------------------------------------------------
    case ESP_BLE_MESH_PROVISIONER_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "Provisioner enabled, err_code: %d",
                 param->provisioner_prov_enable_comp.err_code);
        if (param->provisioner_prov_enable_comp.err_code == ESP_OK) {
            // Primary NetKey (0x0000) is auto-created by esp_ble_mesh_init().
            // We only need to add our AppKey to it.
            MeshConfig::netkey_ready = true;

            const esp_err_t err = esp_ble_mesh_provisioner_add_local_app_key(
                MeshConfig::prov_key.app_key,
                MeshConfig::prov_key.net_idx,
                MeshConfig::prov_key.app_idx);

            if (err == ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "AppKey already exists (restored from NVS)");
                MeshConfig::appkey_ready = true;
            } else if (err != ESP_OK) {
                ESP_LOGE(TAG, "Add local AppKey failed: %d", err);
            } else {
                ESP_LOGI(TAG, "AppKey add requested, waiting for callback...");
            }
        }
        break;

    // -------------------------------------------------------------------
    case ESP_BLE_MESH_PROVISIONER_PROV_DISABLE_COMP_EVT:
        ESP_LOGI(TAG, "Provisioner disabled, err_code: %d",
                 param->provisioner_prov_disable_comp.err_code);
        break;

    // -------------------------------------------------------------------
    case ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT:
        self.onUnprovAdvPacket(
            param->provisioner_recv_unprov_adv_pkt.dev_uuid,
            param->provisioner_recv_unprov_adv_pkt.addr,
            param->provisioner_recv_unprov_adv_pkt.addr_type,
            param->provisioner_recv_unprov_adv_pkt.oob_info,
            param->provisioner_recv_unprov_adv_pkt.adv_type,
            param->provisioner_recv_unprov_adv_pkt.bearer);
        break;

    // -------------------------------------------------------------------
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_OPEN_EVT:
        self.onLinkOpen(param->provisioner_prov_link_open.bearer);
        break;

    // -------------------------------------------------------------------
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_CLOSE_EVT:
        self.onLinkClose(param->provisioner_prov_link_close.bearer,
                         param->provisioner_prov_link_close.reason);
        MeshConfig::provisioning_in_progress = false;
        break;

    // -------------------------------------------------------------------
    case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT:
        self.onProvisioningComplete(
            param->provisioner_prov_complete.node_idx,
            param->provisioner_prov_complete.device_uuid,
            param->provisioner_prov_complete.unicast_addr,
            param->provisioner_prov_complete.element_num,
            param->provisioner_prov_complete.netkey_idx);
        break;

    // -------------------------------------------------------------------
    case ESP_BLE_MESH_PROVISIONER_ADD_UNPROV_DEV_COMP_EVT:
        ESP_LOGI(TAG, "Add unprov device complete, err_code: %d",
                 param->provisioner_add_unprov_dev_comp.err_code);
        break;

    // -------------------------------------------------------------------
    case ESP_BLE_MESH_PROVISIONER_SET_DEV_UUID_MATCH_COMP_EVT:
        ESP_LOGI(TAG, "UUID match set, err_code: %d",
                 param->provisioner_set_dev_uuid_match_comp.err_code);
        break;

    // -------------------------------------------------------------------
    case ESP_BLE_MESH_PROVISIONER_SET_NODE_NAME_COMP_EVT:
        if (param->provisioner_set_node_name_comp.err_code == ESP_OK) {
            const char* name = esp_ble_mesh_provisioner_get_node_name(
                param->provisioner_set_node_name_comp.node_index);
            ESP_LOGI(TAG, "Node %d named: %s",
                     param->provisioner_set_node_name_comp.node_index,
                     name ? name : "NULL");
        }
        break;

    // -------------------------------------------------------------------
    case ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT:
        ESP_LOGI(TAG, "Add local AppKey complete, err_code: %d",
                 param->provisioner_add_app_key_comp.err_code);
        if (param->provisioner_add_app_key_comp.err_code == ESP_OK) {
            MeshConfig::prov_key.app_idx =
                param->provisioner_add_app_key_comp.app_idx;
            MeshConfig::appkey_ready = true;
            ESP_LOGI(TAG, "AppKey ready (idx 0x%04x)",
                     MeshConfig::prov_key.app_idx);

            // Bind the AppKey to our local Generic OnOff Client model
            const esp_err_t err =
                esp_ble_mesh_provisioner_bind_app_key_to_local_model(
                    PROV_OWN_ADDR,
                    MeshConfig::prov_key.app_idx,
                    ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI,
                    ESP_BLE_MESH_CID_NVAL);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Bind local model AppKey failed: %d", err);
            }
        }
        break;

    // -------------------------------------------------------------------
    case ESP_BLE_MESH_PROVISIONER_BIND_APP_KEY_TO_MODEL_COMP_EVT:
        ESP_LOGI(TAG, "Bind AppKey to local model complete, err_code: %d",
                 param->provisioner_bind_app_key_to_model_comp.err_code);
        break;

    // -------------------------------------------------------------------
    case ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_NET_KEY_COMP_EVT:
        // The primary NetKey is auto-created; this event should not fire
        // under normal operation but is logged here for diagnostics.
        ESP_LOGI(TAG, "Add local NetKey complete, err_code: %d",
                 param->provisioner_add_net_key_comp.err_code);
        if (param->provisioner_add_net_key_comp.err_code == ESP_OK) {
            MeshConfig::prov_key.net_idx =
                param->provisioner_add_net_key_comp.net_idx;
            MeshConfig::netkey_ready = true;
            ESP_LOGI(TAG, "NetKey ready (idx 0x%04x)",
                     MeshConfig::prov_key.net_idx);

            const esp_err_t err = esp_ble_mesh_provisioner_add_local_app_key(
                MeshConfig::prov_key.app_key,
                MeshConfig::prov_key.net_idx,
                MeshConfig::prov_key.app_idx);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Add local AppKey failed: %d", err);
            }
        } else {
            ESP_LOGE(TAG, "NetKey add failed!");
        }
        break;

    // -------------------------------------------------------------------
    default:
        break;
    }
}

// ===========================================================================
// Static callback: Config client events
// ===========================================================================
void BLEMeshProvisioner::configClientCallback(
    esp_ble_mesh_cfg_client_cb_event_t  event,
    esp_ble_mesh_cfg_client_cb_param_t* param)
{
    BLEMeshProvisioner& self    = *instance_;
    const uint32_t      opcode  = param->params->opcode;
    const uint16_t      addr    = param->params->ctx.addr;

    ESP_LOGI(TAG,
             "Config client event: 0x%02x, opcode: 0x%04" PRIx32 ", addr: 0x%04x",
             event, opcode, addr);

    // On error, try to skip ahead in the bind chain rather than stopping
    if (param->error_code) {
        ESP_LOGE(TAG,
                 "Config message failed, opcode: 0x%04" PRIx32 ", error: %d",
                 opcode, param->error_code);

        if (opcode == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND ||
            opcode == ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD) {
            MeshNode* node = self.registry_.getNode(addr);
            if (node) {
                ESP_LOGW(TAG, "Config op failed, trying next step...");
                self.binder_.bindNextModel(node);
            }
        }
        return;
    }

    MeshNode* node = self.registry_.getNode(addr);
    if (!node) {
        ESP_LOGE(TAG, "Node 0x%04x not found in registry", addr);
        return;
    }

    switch (event) {
    // -------------------------------------------------------------------
    case ESP_BLE_MESH_CFG_CLIENT_GET_STATE_EVT:
        if (opcode == ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET) {
            ESP_LOGI(TAG, "Got composition data from 0x%04x", addr);

            CompositionParser::parse(
                node, param->status_cb.comp_data_status.composition_data);

            // Distribute the AppKey to the node
            esp_ble_mesh_client_common_param_t  common = {};
            esp_ble_mesh_cfg_client_set_state_t set    = {};

            MeshConfig::setConfigCommon(&common, addr,
                                        &MeshConfig::root_models[1],
                                        ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD);
            set.app_key_add.net_idx = MeshConfig::prov_key.net_idx;
            set.app_key_add.app_idx = MeshConfig::prov_key.app_idx;
            memcpy(set.app_key_add.app_key,
                   MeshConfig::prov_key.app_key, 16);

            const esp_err_t err =
                esp_ble_mesh_config_client_set_state(&common, &set);
            if (err) {
                ESP_LOGE(TAG, "AppKey Add failed: %d", err);
            }
        }
        break;

    // -------------------------------------------------------------------
    case ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT:
        if (opcode == ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD) {
            ESP_LOGI(TAG, "AppKey added to node 0x%04x", addr);
            self.binder_.bindNextModel(node);

        } else if (opcode == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND) {
            const uint16_t model_id   =
                param->status_cb.model_app_status.model_id;
            const uint16_t company_id =
                param->status_cb.model_app_status.company_id;

            if (company_id == ESP_BLE_MESH_CID_NVAL) {
                // SIG model
                if (model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV) {
                    node->srv_bound = true;
                    ESP_LOGI(TAG, "OnOff Server bound on 0x%04x", addr);
                } else if (model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI) {
                    node->cli_bound = true;
                    ESP_LOGI(TAG, "OnOff Client bound on 0x%04x", addr);
                }
            } else if (company_id == CID_ESP) {
                // Vendor model
                if (model_id == VND_MODEL_ID_SERVER) {
                    node->vnd_srv_bound = true;
                    ESP_LOGI(TAG, "Vendor Server bound on 0x%04x", addr);
                } else if (model_id == VND_MODEL_ID_CLIENT) {
                    node->vnd_cli_bound = true;
                    ESP_LOGI(TAG, "Vendor Client bound on 0x%04x", addr);
                }
            }
            self.binder_.bindNextModel(node);

        } else if (opcode == ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD) {
            ESP_LOGI(TAG, "Group subscription added on 0x%04x", addr);
            node->vnd_srv_subscribed = true;
            self.binder_.bindNextModel(node); // will log FULLY CONFIGURED
        }
        break;

    // -------------------------------------------------------------------
    case ESP_BLE_MESH_CFG_CLIENT_TIMEOUT_EVT:
        ESP_LOGW(TAG, "Config client timeout for opcode 0x%04" PRIx32, opcode);
        break;

    // -------------------------------------------------------------------
    default:
        break;
    }
}

// ===========================================================================
// Static callback: Generic client events (OnOff status responses)
// ===========================================================================
void BLEMeshProvisioner::genericClientCallback(
    esp_ble_mesh_generic_client_cb_event_t  event,
    esp_ble_mesh_generic_client_cb_param_t* param)
{
    ESP_LOGI(TAG, "Generic client event: 0x%02x, opcode: 0x%04" PRIx32,
             event, param->params->opcode);

    switch (event) {
    case ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT:
        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET) {
            ESP_LOGI(TAG, "OnOff status: 0x%02x",
                     param->status_cb.onoff_status.present_onoff);
        }
        break;

    case ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT:
        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
            ESP_LOGI(TAG, "OnOff set confirmed: 0x%02x",
                     param->status_cb.onoff_status.present_onoff);
        }
        break;

    default:
        break;
    }
}
