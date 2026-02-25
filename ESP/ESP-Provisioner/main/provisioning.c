/* Provisioning: callbacks and mesh initialization */

#include <inttypes.h>

#include "esp_log.h"

#include "provisioning.h"

#define TAG "PROV"

// Called when provisioning completes
esp_err_t prov_complete(int node_idx, const esp_ble_mesh_octet16_t uuid,
                        uint16_t unicast, uint8_t elem_num,
                        uint16_t net_idx) {
  esp_ble_mesh_client_common_param_t common = {0};
  esp_ble_mesh_cfg_client_get_state_t get_state = {0};
  mesh_node_info_t *node = NULL;
  char name[16] = {0};
  int err;

  ESP_LOGI(TAG, "========== PROVISIONING COMPLETE ==========");
  ESP_LOGI(TAG, "Node index: %d", node_idx);
  ESP_LOGI(TAG, "Unicast address: 0x%04x", unicast);
  ESP_LOGI(TAG, "Element count: %d", elem_num);
  ESP_LOGI(TAG, "NetKey index: 0x%04x", net_idx);
  ESP_LOGI(TAG, "Device UUID: %s", bt_hex(uuid, 16));
  ESP_LOGI(TAG, "============================================");

  // Set node name
  snprintf(name, sizeof(name), "NODE-%d", node_idx);
  err = esp_ble_mesh_provisioner_set_node_name(node_idx, name);
  if (err) {
    ESP_LOGE(TAG, "Set node name failed: %d", err);
  }

  // Store node info
  err = store_node_info(uuid, unicast, elem_num, node_idx);
  if (err) {
    ESP_LOGE(TAG, "Store node info failed");
    return ESP_FAIL;
  }

  node = get_node_info(unicast);
  if (!node) {
    ESP_LOGE(TAG, "Get node info failed");
    return ESP_FAIL;
  }

  // Request composition data - Config messages use NetKey (not AppKey)
  // Use &root_models[1] directly (CFG_CLI model) to ensure valid model pointer
  ESP_LOGI(TAG, "Sending COMP_DATA_GET to 0x%04x using net_idx 0x%04x", unicast,
           prov_key.net_idx);
  set_config_common(&common, unicast, &root_models[1],
                    ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET);
  get_state.comp_data_get.page = COMP_DATA_PAGE_0;

  err = esp_ble_mesh_config_client_get_state(&common, &get_state);
  if (err) {
    ESP_LOGE(TAG, "Get composition data failed: %d", err);
    return ESP_FAIL;
  }

  return ESP_OK;
}

static void prov_link_open(esp_ble_mesh_prov_bearer_t bearer) {
  ESP_LOGI(TAG, "Provisioning link opened (%s)",
           bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
}

static void prov_link_close(esp_ble_mesh_prov_bearer_t bearer, uint8_t reason) {
  ESP_LOGI(TAG, "Provisioning link closed (%s), reason: 0x%02x",
           bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT", reason);
}

// Handle unprovisioned device advertisements
static void recv_unprov_adv_pkt(uint8_t dev_uuid[16], uint8_t addr[BD_ADDR_LEN],
                                esp_ble_mesh_addr_type_t addr_type,
                                uint16_t oob_info, uint8_t adv_type,
                                esp_ble_mesh_prov_bearer_t bearer) {
  esp_ble_mesh_unprov_dev_add_t add_dev = {0};
  int err;

  // Only start provisioning if not already in progress
  if (provisioning_in_progress) {
    return; // Skip duplicate advertisements
  }

  // Don't start provisioning until local keys are ready
  if (!netkey_ready || !appkey_ready) {
    ESP_LOGW(TAG, "Keys not ready yet (net=%d app=%d), ignoring unprov device",
             netkey_ready, appkey_ready);
    return;
  }

  ESP_LOGI(TAG, "========== UNPROVISIONED DEVICE FOUND ==========");
  ESP_LOGI(TAG, "Address: %s (type: %d)", bt_hex(addr, BD_ADDR_LEN), addr_type);
  ESP_LOGI(TAG, "UUID: %s", bt_hex(dev_uuid, 16));
  ESP_LOGI(TAG, "Bearer: %s",
           (bearer & ESP_BLE_MESH_PROV_ADV) ? "PB-ADV" : "PB-GATT");
  ESP_LOGI(TAG, "================================================");

  // Add device and start provisioning immediately
  memcpy(add_dev.addr, addr, BD_ADDR_LEN);
  add_dev.addr_type = addr_type;
  memcpy(add_dev.uuid, dev_uuid, 16);
  add_dev.oob_info = oob_info;
  add_dev.bearer = bearer;

  provisioning_in_progress = true; // Set flag before starting
  err = esp_ble_mesh_provisioner_add_unprov_dev(
      &add_dev, ADD_DEV_RM_AFTER_PROV_FLAG | ADD_DEV_START_PROV_NOW_FLAG |
                    ADD_DEV_FLUSHABLE_DEV_FLAG);

  if (err) {
    ESP_LOGE(TAG, "Add unprovisioned device failed: %d", err);
    provisioning_in_progress = false; // Reset on failure
  } else {
    ESP_LOGI(TAG, "Started provisioning device...");
  }
}

// Provisioning callbacks
void provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                     esp_ble_mesh_prov_cb_param_t *param) {
  switch (event) {
  case ESP_BLE_MESH_PROVISIONER_PROV_ENABLE_COMP_EVT:
    ESP_LOGI(TAG, "Provisioner enabled, err_code: %d",
             param->provisioner_prov_enable_comp.err_code);
    if (param->provisioner_prov_enable_comp.err_code == ESP_OK) {
      // Primary NetKey (0x0000) is auto-created by esp_ble_mesh_init(),
      // so we only need to add our AppKey to it.
      netkey_ready = true;
      esp_err_t err = esp_ble_mesh_provisioner_add_local_app_key(
          prov_key.app_key, prov_key.net_idx, prov_key.app_idx);
      if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "AppKey already exists (restored from NVS)");
        appkey_ready = true;
      } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Add local AppKey failed: %d", err);
      } else {
        ESP_LOGI(TAG, "AppKey add requested, waiting for callback...");
      }
    }
    break;

  case ESP_BLE_MESH_PROVISIONER_PROV_DISABLE_COMP_EVT:
    ESP_LOGI(TAG, "Provisioner disabled, err_code: %d",
             param->provisioner_prov_disable_comp.err_code);
    break;

  case ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT:
    recv_unprov_adv_pkt(param->provisioner_recv_unprov_adv_pkt.dev_uuid,
                        param->provisioner_recv_unprov_adv_pkt.addr,
                        param->provisioner_recv_unprov_adv_pkt.addr_type,
                        param->provisioner_recv_unprov_adv_pkt.oob_info,
                        param->provisioner_recv_unprov_adv_pkt.adv_type,
                        param->provisioner_recv_unprov_adv_pkt.bearer);
    break;

  case ESP_BLE_MESH_PROVISIONER_PROV_LINK_OPEN_EVT:
    prov_link_open(param->provisioner_prov_link_open.bearer);
    break;

  case ESP_BLE_MESH_PROVISIONER_PROV_LINK_CLOSE_EVT:
    prov_link_close(param->provisioner_prov_link_close.bearer,
                    param->provisioner_prov_link_close.reason);
    provisioning_in_progress = false; // Reset flag when link closes
    break;

  case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT:
    prov_complete(param->provisioner_prov_complete.node_idx,
                  param->provisioner_prov_complete.device_uuid,
                  param->provisioner_prov_complete.unicast_addr,
                  param->provisioner_prov_complete.element_num,
                  param->provisioner_prov_complete.netkey_idx);
    break;

  case ESP_BLE_MESH_PROVISIONER_ADD_UNPROV_DEV_COMP_EVT:
    ESP_LOGI(TAG, "Add unprov device complete, err_code: %d",
             param->provisioner_add_unprov_dev_comp.err_code);
    break;

  case ESP_BLE_MESH_PROVISIONER_SET_DEV_UUID_MATCH_COMP_EVT:
    ESP_LOGI(TAG, "UUID match set, err_code: %d",
             param->provisioner_set_dev_uuid_match_comp.err_code);
    break;

  case ESP_BLE_MESH_PROVISIONER_SET_NODE_NAME_COMP_EVT:
    if (param->provisioner_set_node_name_comp.err_code == ESP_OK) {
      const char *name = esp_ble_mesh_provisioner_get_node_name(
          param->provisioner_set_node_name_comp.node_index);
      ESP_LOGI(TAG, "Node %d named: %s",
               param->provisioner_set_node_name_comp.node_index,
               name ? name : "NULL");
    }
    break;

  case ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT:
    ESP_LOGI(TAG, "Add local AppKey complete, err_code: %d",
             param->provisioner_add_app_key_comp.err_code);
    if (param->provisioner_add_app_key_comp.err_code == ESP_OK) {
      prov_key.app_idx = param->provisioner_add_app_key_comp.app_idx;
      appkey_ready = true; // AppKey is now ready for use
      ESP_LOGI(TAG, "AppKey ready (idx 0x%04x)", prov_key.app_idx);

      // Bind AppKey to our local OnOff Client model
      esp_err_t err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(
          PROV_OWN_ADDR, prov_key.app_idx, ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI,
          ESP_BLE_MESH_CID_NVAL);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bind local model AppKey failed: %d", err);
      }
    }
    break;

  case ESP_BLE_MESH_PROVISIONER_BIND_APP_KEY_TO_MODEL_COMP_EVT:
    ESP_LOGI(TAG, "Bind AppKey to local model complete, err_code: %d",
             param->provisioner_bind_app_key_to_model_comp.err_code);
    break;

  case ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_NET_KEY_COMP_EVT:
    ESP_LOGI(TAG, "Add local NetKey complete, err_code: %d",
             param->provisioner_add_net_key_comp.err_code);
    if (param->provisioner_add_net_key_comp.err_code == ESP_OK) {
      prov_key.net_idx = param->provisioner_add_net_key_comp.net_idx;
      netkey_ready = true; // NetKey is NOW ready for use
      ESP_LOGI(TAG, "NetKey ready (idx 0x%04x)", prov_key.net_idx);

      // Now add AppKey (Chained sequence)
      esp_err_t err = esp_ble_mesh_provisioner_add_local_app_key(
          prov_key.app_key, prov_key.net_idx, prov_key.app_idx);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Add local AppKey failed: %d", err);
      }
    } else {
      ESP_LOGE(TAG, "NetKey add failed!");
    }
    break;

  default:
    break;
  }
}

// Config client callbacks - handles AppKey distribution
void config_client_cb(esp_ble_mesh_cfg_client_cb_event_t event,
                      esp_ble_mesh_cfg_client_cb_param_t *param) {
  esp_ble_mesh_client_common_param_t common = {0};
  mesh_node_info_t *node = NULL;
  uint32_t opcode = param->params->opcode;
  uint16_t addr = param->params->ctx.addr;
  int err;

  ESP_LOGI(TAG,
           "Config client event: 0x%02x, opcode: 0x%04" PRIx32 ", addr: 0x%04x",
           event, opcode, addr);

  // Handle errors - but don't bail on Model App Bind status errors
  if (param->error_code) {
    ESP_LOGE(TAG, "Config message failed, opcode: 0x%04" PRIx32 ", error: %d",
             opcode, param->error_code);

    // For Model App Bind, a status error means model doesn't exist on the node.
    // Skip to the next model in the bind chain instead of giving up.
    if (opcode == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND ||
        opcode == ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD) {
      node = get_node_info(addr);
      if (node) {
        ESP_LOGW(TAG, "Config op failed, trying next step...");
        bind_next_model(node);
      }
    }
    return;
  }

  node = get_node_info(addr);
  if (!node) {
    ESP_LOGE(TAG, "Node 0x%04x not found", addr);
    return;
  }

  switch (event) {
  case ESP_BLE_MESH_CFG_CLIENT_GET_STATE_EVT:
    if (opcode == ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET) {
      ESP_LOGI(TAG, "Got composition data from 0x%04x", addr);

      // Parse composition data to detect which models node has
      parse_composition_data(
          node, param->status_cb.comp_data_status.composition_data);

      // Now add AppKey to the node
      esp_ble_mesh_cfg_client_set_state_t set = {0};
      set_config_common(&common, addr, &root_models[1],
                        ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD);
      set.app_key_add.net_idx = prov_key.net_idx;
      set.app_key_add.app_idx = prov_key.app_idx;
      memcpy(set.app_key_add.app_key, prov_key.app_key, 16);

      err = esp_ble_mesh_config_client_set_state(&common, &set);
      if (err) {
        ESP_LOGE(TAG, "AppKey Add failed: %d", err);
      }
    }
    break;

  case ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT:
    if (opcode == ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD) {
      ESP_LOGI(TAG, "AppKey added to node 0x%04x", addr);
      // Start binding models in priority order
      bind_next_model(node);
    } else if (opcode == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND) {
      // Determine which model was just bound from the status
      uint16_t model_id = param->status_cb.model_app_status.model_id;
      uint16_t company_id = param->status_cb.model_app_status.company_id;

      if (company_id == ESP_BLE_MESH_CID_NVAL) {
        // SIG model bound
        if (model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV) {
          node->srv_bound = true;
          ESP_LOGI(TAG, "OnOff Server bound on 0x%04x", addr);
        } else if (model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI) {
          node->cli_bound = true;
          ESP_LOGI(TAG, "OnOff Client bound on 0x%04x", addr);
        }
      } else if (company_id == CID_ESP) {
        // Vendor model bound
        if (model_id == VND_MODEL_ID_SERVER) {
          node->vnd_srv_bound = true;
          ESP_LOGI(TAG, "Vendor Server bound on 0x%04x", addr);
        } else if (model_id == VND_MODEL_ID_CLIENT) {
          node->vnd_cli_bound = true;
          ESP_LOGI(TAG, "Vendor Client bound on 0x%04x", addr);
        }
      }

      // Chain to next unbound model
      bind_next_model(node);
    } else if (opcode == ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD) {
      ESP_LOGI(TAG, "Group subscription added on 0x%04x", addr);
      if (node) {
        node->vnd_srv_subscribed = true;
        bind_next_model(node); // Chains to "FULLY CONFIGURED"
      }
    }
    break;

  case ESP_BLE_MESH_CFG_CLIENT_TIMEOUT_EVT:
    ESP_LOGW(TAG, "Config client timeout for opcode 0x%04" PRIx32, opcode);
    break;

  default:
    break;
  }
}

// Generic OnOff client callbacks (for testing)
void generic_client_cb(esp_ble_mesh_generic_client_cb_event_t event,
                       esp_ble_mesh_generic_client_cb_param_t *param) {
  ESP_LOGI(TAG, "Generic client event: 0x%02x, opcode: 0x%04" PRIx32, event,
           param->params->opcode);

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

esp_err_t ble_mesh_init(void) {
  uint8_t match[UUID_MATCH_LEN] = UUID_MATCH_PREFIX;
  esp_err_t err;

  // Initialize keys
  prov_key.net_idx = ESP_BLE_MESH_KEY_PRIMARY;
  prov_key.app_idx = APP_KEY_IDX;
  memset(prov_key.net_key, NET_KEY_OCTET, sizeof(prov_key.net_key));
  memset(prov_key.app_key, APP_KEY_OCTET, sizeof(prov_key.app_key));

  // Register callbacks
  esp_ble_mesh_register_prov_callback(provisioning_cb);
  esp_ble_mesh_register_config_client_callback(config_client_cb);
  esp_ble_mesh_register_generic_client_callback(generic_client_cb);

  // Initialize mesh stack
  err = esp_ble_mesh_init(&provision, &composition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Mesh init failed: %d", err);
    return err;
  }

  // IMPORTANT: Explicitly set model pointers for client structs
  // These may not be auto-filled by the macros
  config_client.model = &root_models[1]; // CFG_CLI model
  onoff_client.model = &root_models[2];  // GEN_ONOFF_CLI model

  // Set UUID match filter - only provision devices starting with 0xdd 0xdd
  err = esp_ble_mesh_provisioner_set_dev_uuid_match(match, sizeof(match), 0x0,
                                                    false);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Set UUID match failed: %d", err);
    return err;
  }

  // Enable provisioner (both PB-ADV and PB-GATT) - MUST BE ENABLED BEFORE
  // ADDING KEYS
  err = esp_ble_mesh_provisioner_prov_enable(ESP_BLE_MESH_PROV_ADV |
                                             ESP_BLE_MESH_PROV_GATT);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Enable provisioner failed: %d", err);
    return err;
  }

  // NetKey will be added in PROV_ENABLE_COMP_EVT callback
  // (must wait for provisioner to be fully enabled before adding keys)

  ESP_LOGI(TAG, "===========================================");
  ESP_LOGI(TAG, "  BLE Mesh Provisioner Ready");
  ESP_LOGI(TAG, "  Scanning for devices with UUID: 0xdd 0xdd...");
  ESP_LOGI(TAG, "===========================================");

  return ESP_OK;
}
