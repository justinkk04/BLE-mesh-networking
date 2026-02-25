/* Model binding: bind models to AppKey and subscribe to groups */

#include "esp_log.h"

#include "model_binding.h"

#define TAG "MODEL_BIND"

// Helper to bind a model to AppKey
esp_err_t bind_model(mesh_node_info_t *node, uint16_t model_id) {
  esp_ble_mesh_client_common_param_t common = {0};
  esp_ble_mesh_cfg_client_set_state_t set = {0};

  const char *name =
      (model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV) ? "Server" : "Client";
  ESP_LOGI(TAG, "Binding OnOff %s to node 0x%04x", name, node->unicast);

  common.opcode = ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND;
  common.model = &root_models[1]; // CFG_CLI
  common.ctx.net_idx = prov_key.net_idx;
  common.ctx.app_idx = prov_key.app_idx;
  common.ctx.addr = node->unicast;
  common.ctx.send_ttl = MSG_SEND_TTL;
  common.msg_timeout = MSG_TIMEOUT;
  common.msg_role = ROLE_PROVISIONER;

  set.model_app_bind.element_addr = node->unicast;
  set.model_app_bind.model_app_idx = prov_key.app_idx;
  set.model_app_bind.model_id = model_id;
  set.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;

  return esp_ble_mesh_config_client_set_state(&common, &set);
}

// Helper to bind a vendor model to AppKey (company_id = CID_ESP)
esp_err_t bind_vendor_model(mesh_node_info_t *node, uint16_t model_id) {
  esp_ble_mesh_client_common_param_t common = {0};
  esp_ble_mesh_cfg_client_set_state_t set = {0};

  const char *name =
      (model_id == VND_MODEL_ID_SERVER) ? "Vnd Server" : "Vnd Client";
  ESP_LOGI(TAG, "Binding %s to node 0x%04x", name, node->unicast);

  common.opcode = ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND;
  common.model = &root_models[1]; // CFG_CLI
  common.ctx.net_idx = prov_key.net_idx;
  common.ctx.app_idx = prov_key.app_idx;
  common.ctx.addr = node->unicast;
  common.ctx.send_ttl = MSG_SEND_TTL;
  common.msg_timeout = MSG_TIMEOUT;
  common.msg_role = ROLE_PROVISIONER;

  set.model_app_bind.element_addr = node->unicast;
  set.model_app_bind.model_app_idx = prov_key.app_idx;
  set.model_app_bind.model_id = model_id;
  set.model_app_bind.company_id = CID_ESP; // Vendor model uses company ID

  return esp_ble_mesh_config_client_set_state(&common, &set);
}

// Subscribe a node's vendor server model to the group address
esp_err_t subscribe_vendor_model_to_group(mesh_node_info_t *node) {
  esp_ble_mesh_client_common_param_t common = {0};
  esp_ble_mesh_cfg_client_set_state_t set = {0};

  ESP_LOGI(TAG, "Subscribing Vnd Server on 0x%04x to group 0x%04x",
           node->unicast, MESH_GROUP_ADDR);

  common.opcode = ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD;
  common.model = &root_models[1]; // CFG_CLI
  common.ctx.net_idx = prov_key.net_idx;
  common.ctx.app_idx = prov_key.app_idx;
  common.ctx.addr = node->unicast;
  common.ctx.send_ttl = MSG_SEND_TTL;
  common.msg_timeout = MSG_TIMEOUT;
  common.msg_role = ROLE_PROVISIONER;

  set.model_sub_add.element_addr = node->unicast;
  set.model_sub_add.sub_addr = MESH_GROUP_ADDR;
  set.model_sub_add.model_id = VND_MODEL_ID_SERVER;
  set.model_sub_add.company_id = CID_ESP;

  return esp_ble_mesh_config_client_set_state(&common, &set);
}

// Bind the next unbound model in priority order, or log FULLY CONFIGURED
void bind_next_model(mesh_node_info_t *node) {
  esp_err_t err;

  if (node->has_onoff_srv && !node->srv_bound) {
    err = bind_model(node, ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV);
    if (err)
      ESP_LOGE(TAG, "Bind OnOff Server failed: %d", err);
  } else if (node->has_onoff_cli && !node->cli_bound) {
    err = bind_model(node, ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI);
    if (err)
      ESP_LOGE(TAG, "Bind OnOff Client failed: %d", err);
  } else if (node->has_vnd_srv && !node->vnd_srv_bound) {
    err = bind_vendor_model(node, VND_MODEL_ID_SERVER);
    if (err)
      ESP_LOGE(TAG, "Bind Vendor Server failed: %d", err);
  } else if (node->has_vnd_cli && !node->vnd_cli_bound) {
    err = bind_vendor_model(node, VND_MODEL_ID_CLIENT);
    if (err)
      ESP_LOGE(TAG, "Bind Vendor Client failed: %d", err);
  } else if (node->has_vnd_srv && !node->vnd_srv_subscribed) {
    err = subscribe_vendor_model_to_group(node);
    if (err)
      ESP_LOGE(TAG, "Subscribe Vnd Server to group failed: %d", err);
  } else {
    ESP_LOGI(TAG, "========== NODE 0x%04x FULLY CONFIGURED ==========",
             node->unicast);
    ESP_LOGI(TAG, "Provisioned nodes: %d", node_count);
  }
}
