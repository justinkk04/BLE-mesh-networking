/* ModelBinder.cpp
 *
 * Implements the sequential model-binding and group-subscription state machine
 * used to fully configure a newly provisioned BLE Mesh node.
 */

#include "ModelBinder.h"
#include "MeshConfig.h"
#include "MeshConstants.h"

#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_log.h"

static const char* TAG = "MODEL_BIND";

ModelBinder::ModelBinder(NodeRegistry& registry)
    : registry_(registry) {}

esp_err_t ModelBinder::bindModel(MeshNode* node, uint16_t model_id) {
    esp_ble_mesh_client_common_param_t common = {};
    esp_ble_mesh_cfg_client_set_state_t set   = {};

    const char* label =
        (model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV) ? "Server" : "Client";
    ESP_LOGI(TAG, "Binding OnOff %s to node 0x%04x", label, node->unicast);

    common.opcode       = ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND;
    common.model        = &MeshConfig::root_models[1]; // CFG_CLI
    common.ctx.net_idx  = MeshConfig::prov_key.net_idx;
    common.ctx.app_idx  = MeshConfig::prov_key.app_idx;
    common.ctx.addr     = node->unicast;
    common.ctx.send_ttl = MSG_SEND_TTL;
    common.msg_timeout  = MSG_TIMEOUT;
    common.msg_role     = ROLE_PROVISIONER;

    set.model_app_bind.element_addr    = node->unicast;
    set.model_app_bind.model_app_idx   = MeshConfig::prov_key.app_idx;
    set.model_app_bind.model_id        = model_id;
    set.model_app_bind.company_id      = ESP_BLE_MESH_CID_NVAL;

    return esp_ble_mesh_config_client_set_state(&common, &set);
}

esp_err_t ModelBinder::bindVendorModel(MeshNode* node, uint16_t model_id) {
    esp_ble_mesh_client_common_param_t  common = {};
    esp_ble_mesh_cfg_client_set_state_t set    = {};

    const char* label =
        (model_id == VND_MODEL_ID_SERVER) ? "Vnd Server" : "Vnd Client";
    ESP_LOGI(TAG, "Binding %s to node 0x%04x", label, node->unicast);

    common.opcode       = ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND;
    common.model        = &MeshConfig::root_models[1]; // CFG_CLI
    common.ctx.net_idx  = MeshConfig::prov_key.net_idx;
    common.ctx.app_idx  = MeshConfig::prov_key.app_idx;
    common.ctx.addr     = node->unicast;
    common.ctx.send_ttl = MSG_SEND_TTL;
    common.msg_timeout  = MSG_TIMEOUT;
    common.msg_role     = ROLE_PROVISIONER;

    set.model_app_bind.element_addr    = node->unicast;
    set.model_app_bind.model_app_idx   = MeshConfig::prov_key.app_idx;
    set.model_app_bind.model_id        = model_id;
    set.model_app_bind.company_id      = CID_ESP; // Vendor models carry the CID

    return esp_ble_mesh_config_client_set_state(&common, &set);
}

esp_err_t ModelBinder::subscribeVendorModelToGroup(MeshNode* node) {
    esp_ble_mesh_client_common_param_t  common = {};
    esp_ble_mesh_cfg_client_set_state_t set    = {};

    ESP_LOGI(TAG, "Subscribing Vnd Server on 0x%04x to group 0x%04x",
             node->unicast, MESH_GROUP_ADDR);

    common.opcode       = ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD;
    common.model        = &MeshConfig::root_models[1]; // CFG_CLI
    common.ctx.net_idx  = MeshConfig::prov_key.net_idx;
    common.ctx.app_idx  = MeshConfig::prov_key.app_idx;
    common.ctx.addr     = node->unicast;
    common.ctx.send_ttl = MSG_SEND_TTL;
    common.msg_timeout  = MSG_TIMEOUT;
    common.msg_role     = ROLE_PROVISIONER;

    set.model_sub_add.element_addr = node->unicast;
    set.model_sub_add.sub_addr     = MESH_GROUP_ADDR;
    set.model_sub_add.model_id     = VND_MODEL_ID_SERVER;
    set.model_sub_add.company_id   = CID_ESP;

    return esp_ble_mesh_config_client_set_state(&common, &set);
}

void ModelBinder::bindNextModel(MeshNode* node) {
    esp_err_t err = ESP_OK;

    if (node->has_onoff_srv && !node->srv_bound) {
        err = bindModel(node, ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV);
        if (err) ESP_LOGE(TAG, "Bind OnOff Server failed: %d", err);

    } else if (node->has_onoff_cli && !node->cli_bound) {
        err = bindModel(node, ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI);
        if (err) ESP_LOGE(TAG, "Bind OnOff Client failed: %d", err);

    } else if (node->has_vnd_srv && !node->vnd_srv_bound) {
        err = bindVendorModel(node, VND_MODEL_ID_SERVER);
        if (err) ESP_LOGE(TAG, "Bind Vendor Server failed: %d", err);

    } else if (node->has_vnd_cli && !node->vnd_cli_bound) {
        err = bindVendorModel(node, VND_MODEL_ID_CLIENT);
        if (err) ESP_LOGE(TAG, "Bind Vendor Client failed: %d", err);

    } else if (node->has_vnd_srv && !node->vnd_srv_subscribed) {
        err = subscribeVendorModelToGroup(node);
        if (err) ESP_LOGE(TAG, "Subscribe Vnd Server to group failed: %d", err);

    } else {
        ESP_LOGI(TAG, "========== NODE 0x%04x FULLY CONFIGURED ==========",
                 node->unicast);
        ESP_LOGI(TAG, "Provisioned nodes: %d", registry_.count());
    }
}
