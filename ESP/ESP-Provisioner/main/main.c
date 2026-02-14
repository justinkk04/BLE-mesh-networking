/* ESP BLE Mesh Provisioner
 *
 * Auto-provisions devices with UUID prefix 0xdd
 * Distributes NetKey/AppKey and binds models
 *
 * Based on ESP-IDF BLE Mesh provisioner example
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"

#include "ble_mesh_example_init.h"

#define TAG "PROVISIONER"

#define CID_ESP 0x02E5

// Vendor model IDs (shared with gateway and mesh node)
#define VND_MODEL_ID_CLIENT 0x0000
#define VND_MODEL_ID_SERVER 0x0001

#define PROV_OWN_ADDR 0x0001 // Provisioner's own address

#define MSG_SEND_TTL 3
#define MSG_TIMEOUT 0

#define COMP_DATA_PAGE_0 0x00

#define APP_KEY_IDX 0x0000
#define APP_KEY_OCTET 0x12

// UUID prefix to match - devices must start with this
#define UUID_MATCH_PREFIX                                                      \
  { 0xdd, 0xdd }
#define UUID_MATCH_LEN 2

static uint8_t dev_uuid[16];

// Track provisioned nodes
typedef struct {
  uint8_t uuid[16];
  uint16_t unicast;
  uint8_t elem_num;
  uint8_t onoff;
  char name[16];
  bool has_onoff_srv;
  bool has_onoff_cli;
  bool has_vnd_srv;
  bool has_vnd_cli;
  bool srv_bound;
  bool cli_bound;
  bool vnd_srv_bound;
  bool vnd_cli_bound;
} mesh_node_info_t;

#define MAX_NODES 10
static mesh_node_info_t nodes[MAX_NODES] = {0};
static int node_count = 0;

// Prevent duplicate provisioning attempts
static bool provisioning_in_progress = false;

// Track when local keys are ready for use
static bool netkey_ready = false;
static bool appkey_ready = false;

// Network/App keys
#define NET_KEY_OCTET 0x11

static struct esp_ble_mesh_key {
  uint16_t net_idx;
  uint16_t app_idx;
  uint8_t net_key[16];
  uint8_t app_key[16];
} prov_key;

// Client models
static esp_ble_mesh_client_t config_client;
static esp_ble_mesh_client_t onoff_client;

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
static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_CFG_CLI(&config_client),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(NULL, &onoff_client),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, ESP_BLE_MESH_MODEL_NONE),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

// Provisioner configuration
static esp_ble_mesh_prov_t provision = {
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

// Store node info
static esp_err_t store_node_info(const uint8_t uuid[16], uint16_t unicast,
                                 uint8_t elem_num, int node_idx) {
  if (node_count >= MAX_NODES) {
    ESP_LOGE(TAG, "Max nodes reached");
    return ESP_FAIL;
  }

  // Check if already exists
  for (int i = 0; i < node_count; i++) {
    if (!memcmp(nodes[i].uuid, uuid, 16)) {
      nodes[i].unicast = unicast;
      nodes[i].elem_num = elem_num;
      ESP_LOGW(TAG, "Node re-provisioned at 0x%04x", unicast);
      return ESP_OK;
    }
  }

  // Add new node
  memcpy(nodes[node_count].uuid, uuid, 16);
  nodes[node_count].unicast = unicast;
  nodes[node_count].elem_num = elem_num;
  snprintf(nodes[node_count].name, sizeof(nodes[node_count].name), "NODE-%d",
           node_idx);
  node_count++;

  ESP_LOGI(TAG, "Stored node %d: addr=0x%04x, elements=%d", node_count, unicast,
           elem_num);

  return ESP_OK;
}

static mesh_node_info_t *get_node_info(uint16_t unicast) {
  for (int i = 0; i < node_count; i++) {
    if (nodes[i].unicast <= unicast &&
        nodes[i].unicast + nodes[i].elem_num > unicast) {
      return &nodes[i];
    }
  }
  return NULL;
}

// Parse composition data to detect which models a node has
// Format: CID(2) + PID(2) + VID(2) + CRPL(2) + Features(2) + [elements...]
// Each element: Loc(2) + NumS(1) + NumV(1) + [SIG Models(2 each)] + [Vendor(4
// each)]
static void parse_composition_data(mesh_node_info_t *node,
                                   struct net_buf_simple *comp) {
  if (!node || !comp || comp->len < 10) {
    ESP_LOGW(TAG, "Invalid composition data");
    return;
  }

  // Reset model flags
  node->has_onoff_srv = false;
  node->has_onoff_cli = false;
  node->has_vnd_srv = false;
  node->has_vnd_cli = false;
  node->srv_bound = false;
  node->cli_bound = false;
  node->vnd_srv_bound = false;
  node->vnd_cli_bound = false;

  // Skip header: CID(2) + PID(2) + VID(2) + CRPL(2) + Features(2) = 10 bytes
  uint8_t *data = comp->data;
  size_t len = comp->len;
  size_t offset = 10;

  ESP_LOGI(TAG, "Parsing comp data (%d bytes)", (int)len);

  // Parse elements
  while (offset + 4 <= len) {
    // Element: Loc(2) + NumS(1) + NumV(1)
    offset += 2; // Skip Loc
    if (offset + 2 > len)
      break;
    uint8_t num_s = data[offset++]; // Number of SIG models
    uint8_t num_v = data[offset++]; // Number of Vendor models

    // Parse SIG models (2 bytes each)
    for (int i = 0; i < num_s && offset + 2 <= len; i++) {
      uint16_t model_id = data[offset] | (data[offset + 1] << 8);
      offset += 2;

      if (model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV) {
        node->has_onoff_srv = true;
        ESP_LOGI(TAG, "  Found OnOff Server (0x%04x)", model_id);
      } else if (model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI) {
        node->has_onoff_cli = true;
        ESP_LOGI(TAG, "  Found OnOff Client (0x%04x)", model_id);
      }
    }

    // Parse Vendor models (4 bytes each: CID(2) + Model ID(2))
    for (int i = 0; i < num_v && offset + 4 <= len; i++) {
      uint16_t cid = data[offset] | (data[offset + 1] << 8);
      uint16_t vid = data[offset + 2] | (data[offset + 3] << 8);
      offset += 4;

      if (cid == CID_ESP && vid == VND_MODEL_ID_SERVER) {
        node->has_vnd_srv = true;
        ESP_LOGI(TAG, "  Found Vendor Server (CID:0x%04x, ID:0x%04x)", cid, vid);
      } else if (cid == CID_ESP && vid == VND_MODEL_ID_CLIENT) {
        node->has_vnd_cli = true;
        ESP_LOGI(TAG, "  Found Vendor Client (CID:0x%04x, ID:0x%04x)", cid, vid);
      }
    }
  }

  ESP_LOGI(TAG, "Node 0x%04x models: srv=%d, cli=%d, vnd_srv=%d, vnd_cli=%d",
           node->unicast, node->has_onoff_srv, node->has_onoff_cli,
           node->has_vnd_srv, node->has_vnd_cli);
}

// Helper to bind a model to AppKey
static esp_err_t bind_model(mesh_node_info_t *node, uint16_t model_id) {
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
static esp_err_t bind_vendor_model(mesh_node_info_t *node, uint16_t model_id) {
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

// Bind the next unbound model in priority order, or log FULLY CONFIGURED
static void bind_next_model(mesh_node_info_t *node) {
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
  } else {
    ESP_LOGI(TAG, "========== NODE 0x%04x FULLY CONFIGURED ==========",
             node->unicast);
    ESP_LOGI(TAG, "Provisioned nodes: %d", node_count);
  }
}

// Set common parameters for CONFIG CLIENT messages (uses NetKey only, NOT
// AppKey) Config messages are encrypted with NetKey, so app_idx must be
// ESP_BLE_MESH_KEY_UNUSED
static esp_err_t set_config_common(esp_ble_mesh_client_common_param_t *common,
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
static esp_err_t set_msg_common(esp_ble_mesh_client_common_param_t *common,
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

// Called when provisioning completes
static esp_err_t prov_complete(int node_idx, const esp_ble_mesh_octet16_t uuid,
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
static void provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
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
static void config_client_cb(esp_ble_mesh_cfg_client_cb_event_t event,
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
    if (opcode == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND) {
      node = get_node_info(addr);
      if (node) {
        ESP_LOGW(TAG, "Bind failed, trying next model...");
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
static void generic_client_cb(esp_ble_mesh_generic_client_cb_event_t event,
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

static esp_err_t ble_mesh_init(void) {
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

void app_main(void) {
  esp_err_t err;

  ESP_LOGI(TAG, "Initializing BLE Mesh Provisioner...");

  // Initialize NVS
  err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  // Initialize Bluetooth
  err = bluetooth_init();
  if (err) {
    ESP_LOGE(TAG, "Bluetooth init failed: %d", err);
    return;
  }

  // Get device UUID
  ble_mesh_get_dev_uuid(dev_uuid);

  // Initialize mesh
  err = ble_mesh_init();
  if (err) {
    ESP_LOGE(TAG, "Mesh init failed: %d", err);
    return;
  }

  ESP_LOGI(TAG, "Provisioner running - waiting for mesh nodes...");
}
