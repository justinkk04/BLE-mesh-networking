#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "mesh_gateway.h"
#include "gatt_service.h"
#include "nvs_store.h"
#include "node_tracker.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Mesh includes
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"

#define TAG "MESH_GW"

// UUID with prefix 0xdd 0xdd for auto-provisioning
uint8_t dev_uuid[16] = {0xdd, 0xdd};

uint16_t cached_net_idx = 0xFFFF;
uint16_t cached_app_idx = 0xFFFF;
static uint8_t msg_tid = 0;
bool vnd_bound = false; // Vendor model bound to AppKey

// Monitor mode state (shared with monitor.c and command_parser.c)
uint16_t monitor_target_addr = 0;
bool monitor_waiting_response = false; // Don't send next READ until response

// Vendor send serialization: prevent overlapping mesh sends
bool vnd_send_busy = false;
uint16_t vnd_send_target_addr = 0x0000;  // Address we're waiting for
static TickType_t vnd_send_start_tick = 0;

// ============== Mesh Models ==============
static esp_ble_mesh_client_t onoff_client;

// Vendor client: sends commands to mesh nodes, receives sensor data
static const esp_ble_mesh_client_op_pair_t vnd_op_pair[] = {
    {VND_OP_SEND, VND_OP_STATUS},
};

static esp_ble_mesh_client_t vendor_client = {
    .op_pair_size = ARRAY_SIZE(vnd_op_pair),
    .op_pair = vnd_op_pair,
};

static esp_ble_mesh_model_op_t vnd_op[] = {
    ESP_BLE_MESH_MODEL_OP(VND_OP_STATUS, 1), // min 1 byte response
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, VND_MODEL_ID_CLIENT, vnd_op, NULL,
                              &vendor_client),
};

static esp_ble_mesh_cfg_srv_t config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(3, 20),  // 4 transmissions for reliable relay delivery
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(4, 20),  // 5 retransmissions for reliable relay to Node 2
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
    .gatt_proxy =
        ESP_BLE_MESH_GATT_PROXY_ENABLED, // Allow Pi5 to connect via proxy
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
    .default_ttl = 7,
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_cli_pub, 2 + 1, ROLE_NODE);

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(&onoff_cli_pub, &onoff_client),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vnd_models),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
    .output_size = 0,
    .output_actions = 0,
};

// ============== Mesh -> GATT: Forward mesh responses to Pi 5 ==============
static void generic_client_cb(esp_ble_mesh_generic_client_cb_event_t event,
                              esp_ble_mesh_generic_client_cb_param_t *param) {
  char buf[64];
  uint16_t src_addr = param->params->ctx.addr;

  ESP_LOGI(TAG, "Client event: 0x%02x from 0x%04x", event, src_addr);

  switch (event) {
  case ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT:
    if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET) {
      int node_id =
          (src_addr >= NODE_BASE_ADDR) ? (src_addr - NODE_BASE_ADDR) : 0;
      snprintf(buf, sizeof(buf), "NODE%d:ONOFF:%d", node_id,
               param->status_cb.onoff_status.present_onoff);
      gatt_notify_sensor_data(buf, strlen(buf));
    }
    break;

  case ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT:
    if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
      int node_id =
          (src_addr >= NODE_BASE_ADDR) ? (src_addr - NODE_BASE_ADDR) : 0;
      snprintf(buf, sizeof(buf), "NODE%d:ACK:%d", node_id,
               param->status_cb.onoff_status.present_onoff);
      gatt_notify_sensor_data(buf, strlen(buf));
    }
    break;

  case ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT:
    snprintf(buf, sizeof(buf), "TIMEOUT:0x%04x", src_addr);
    gatt_notify_sensor_data(buf, strlen(buf));
    ESP_LOGW(TAG, "Mesh timeout for 0x%04x", src_addr);
    break;

  default:
    break;
  }
}

// ============== Send Mesh Command ==============
esp_err_t send_mesh_onoff(uint16_t target_addr, uint8_t onoff) {
  esp_ble_mesh_generic_client_set_state_t set = {0};
  esp_ble_mesh_client_common_param_t common = {0};

  if (cached_app_idx == 0xFFFF) {
    ESP_LOGE(TAG, "Not configured yet");
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Sending OnOff=%d to 0x%04x", onoff, target_addr);

  common.opcode = ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET;
  common.model = onoff_client.model;
  common.ctx.net_idx = cached_net_idx;
  common.ctx.app_idx = cached_app_idx;
  common.ctx.addr = target_addr;
  common.ctx.send_ttl = 3;
  common.msg_timeout = 2000; // 2 second timeout

  set.onoff_set.op_en = false;
  set.onoff_set.onoff = onoff;
  set.onoff_set.tid = msg_tid++;

  return esp_ble_mesh_generic_client_set_state(&common, &set);
}

// ============== Send Vendor Command ==============
esp_err_t send_vendor_command(uint16_t target_addr, const char *cmd,
                                     uint16_t len) {
  bool is_group = (target_addr == MESH_GROUP_ADDR);

  // Skip busy-wait for group sends — multiple nodes respond asynchronously
  if (!is_group) {
    // Wait for previous send to complete (serializes mesh sends)
    int wait_loops = 0;
    while (vnd_send_busy && wait_loops < 50) {  // 50 * 100ms = 5s max wait
      TickType_t elapsed = xTaskGetTickCount() - vnd_send_start_tick;
      if (elapsed > pdMS_TO_TICKS(VND_SEND_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "Send busy timeout (%lu ms), clearing flag",
                 (unsigned long)(elapsed * portTICK_PERIOD_MS));
        vnd_send_busy = false;
        vnd_send_target_addr = 0x0000;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      wait_loops++;
    }
  }

  esp_ble_mesh_msg_ctx_t ctx = {0};
  ctx.net_idx = cached_net_idx;
  ctx.app_idx = cached_app_idx;
  ctx.addr = target_addr;
  ctx.send_ttl = 7;  // Match default_ttl — TTL=3 was too low for relay paths

  ESP_LOGI(TAG, "Vendor SEND to 0x%04x: %.*s", target_addr, len, cmd);

  if (!is_group) {
    vnd_send_busy = true;
    vnd_send_target_addr = target_addr;
    vnd_send_start_tick = xTaskGetTickCount();
  }

  esp_err_t err = esp_ble_mesh_client_model_send_msg(vendor_client.model, &ctx,
                                            VND_OP_SEND, len, (uint8_t *)cmd,
                                            5000, true, ROLE_NODE);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Vendor send_msg failed: %d", err);
    if (!is_group) {
      vnd_send_busy = false;
      vnd_send_target_addr = 0x0000;
    }
  }
  return err;
}

// ============== Vendor Model Callback ==============
static void custom_model_cb(esp_ble_mesh_model_cb_event_t event,
                             esp_ble_mesh_model_cb_param_t *param) {
  switch (event) {
  case ESP_BLE_MESH_MODEL_OPERATION_EVT:
    if (param->model_operation.opcode == VND_OP_STATUS) {
      // Received sensor data from mesh node — forward to Pi 5 via GATT
      char buf[SENSOR_DATA_MAX_LEN];
      uint16_t len = param->model_operation.length;
      uint16_t src = param->model_operation.ctx->addr;

      if (len >= sizeof(buf))
        len = sizeof(buf) - 1;
      int node_id = (src >= NODE_BASE_ADDR) ? (src - NODE_BASE_ADDR) : 0;

      // Only clear busy if this response is from the node we're waiting for.
      // A different node responding (e.g. Node 1 while waiting for Node 2)
      // must not unblock the send pipeline prematurely.
      if (src == vnd_send_target_addr || vnd_send_target_addr == 0x0000) {
        vnd_send_busy = false;
        vnd_send_target_addr = 0x0000;
      }

      // Track this node as a known responder
      register_known_node(src);

      // Format: NODE<id>:DATA:<payload>
      int hdr_len =
          snprintf(buf, sizeof(buf), "NODE%d:DATA:", node_id);
      if (hdr_len + len < sizeof(buf)) {
        memcpy(buf + hdr_len, param->model_operation.msg, len);
        buf[hdr_len + len] = '\0';
        gatt_notify_sensor_data(buf, hdr_len + len);
      }

      ESP_LOGI(TAG, "Vendor STATUS from 0x%04x (%d bytes)", src, len);

      // Clear monitor wait flag so next poll can proceed
      if (monitor_target_addr != 0) {
        monitor_waiting_response = false;
      }
    }
    break;

  case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
    // Do NOT clear vnd_send_busy on success. SEND_COMP means "message left
    // the local radio", NOT "target node received it." For relay nodes the
    // message is still in transit. Clearing here lets the next command
    // collide with the first on the relay path.
    // Busy is cleared by: STATUS response, TIMEOUT, or wait-loop fallback.
    if (param->model_send_comp.err_code) {
      ESP_LOGE(TAG, "Vendor send failed: %d", param->model_send_comp.err_code);
      vnd_send_busy = false;          // Clear only on failure (msg never left)
      vnd_send_target_addr = 0x0000;
      gatt_notify_sensor_data("ERROR:MESH_SEND_FAIL", 20);
    } else {
      ESP_LOGI(TAG, "Vendor SEND_COMP OK (%lu ms)",
               (unsigned long)((xTaskGetTickCount() - vnd_send_start_tick) * portTICK_PERIOD_MS));
    }
    break;

  case ESP_BLE_MESH_CLIENT_MODEL_SEND_TIMEOUT_EVT: {
    // For group sends, vnd_send_target_addr is 0x0000 (not tracked).
    // The SDK fires a timeout because it expects a single response, but
    // group responses arrive individually via STATUS callback — so this
    // timeout is expected and harmless.  Silently ignore it.
    uint16_t timeout_target = vnd_send_target_addr;
    if (timeout_target == 0x0000 && !vnd_send_busy) {
      ESP_LOGI(TAG, "Group send timeout (expected, responses already received)");
      break;
    }
    ESP_LOGW(TAG, "Vendor message timeout (target was 0x%04x)", timeout_target);
    // If this was a probe to an undiscovered address, mark discovery complete
    // so we don't waste 5s probing on every future ALL command.
    if (timeout_target > NODE_BASE_ADDR + known_node_count) {
      discovery_complete = true;
      ESP_LOGI(TAG, "Discovery complete (no node at 0x%04x)", timeout_target);
    }
    vnd_send_busy = false;
    vnd_send_target_addr = 0x0000;
    monitor_waiting_response = false; // Allow next poll attempt
    // Only notify Pi 5 of timeout if NOT in monitor mode (avoid spamming)
    if (monitor_target_addr == 0) {
      gatt_notify_sensor_data("ERROR:MESH_TIMEOUT", 18);
    }
    break;
  }

  case ESP_BLE_MESH_CLIENT_MODEL_RECV_PUBLISH_MSG_EVT:
    // Client model matched a response to a TX context (e.g. group send reply)
    if (param->client_recv_publish_msg.opcode == VND_OP_STATUS) {
      char buf[SENSOR_DATA_MAX_LEN];
      uint16_t len = param->client_recv_publish_msg.length;
      uint16_t src = param->client_recv_publish_msg.ctx->addr;

      if (len >= sizeof(buf))
        len = sizeof(buf) - 1;
      int node_id = (src >= NODE_BASE_ADDR) ? (src - NODE_BASE_ADDR) : 0;

      if (src == vnd_send_target_addr || vnd_send_target_addr == 0x0000) {
        vnd_send_busy = false;
        vnd_send_target_addr = 0x0000;
      }

      register_known_node(src);

      int hdr_len = snprintf(buf, sizeof(buf), "NODE%d:DATA:", node_id);
      if (hdr_len + len < sizeof(buf)) {
        memcpy(buf + hdr_len, param->client_recv_publish_msg.msg, len);
        buf[hdr_len + len] = '\0';
        gatt_notify_sensor_data(buf, hdr_len + len);
      }

      ESP_LOGI(TAG, "Vendor STATUS (publish) from 0x%04x (%d bytes)", src, len);

      if (monitor_target_addr != 0) {
        monitor_waiting_response = false;
      }
    }
    break;

  default:
    ESP_LOGD(TAG, "Unhandled model event: 0x%02x", event);
    break;
  }
}

// ============== Mesh Callbacks ==============
static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags,
                          uint32_t iv_index) {
  ESP_LOGI(TAG, "========== GATEWAY PROVISIONED ==========");
  ESP_LOGI(TAG, "NetKey index: 0x%04x", net_idx);
  ESP_LOGI(TAG, "Unicast addr: 0x%04x", addr);
  ESP_LOGI(TAG, "==========================================");

  gw_state.net_idx = net_idx;
  gw_state.addr = addr;
  cached_net_idx = net_idx;
}

static void provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                            esp_ble_mesh_prov_cb_param_t *param) {
  switch (event) {
  case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
    ESP_LOGI(TAG, "Mesh stack registered");
    onoff_client.model = &elements[0].sig_models[1];
    vendor_client.model = &vnd_models[0];
    restore_gw_state();
    break;

  case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
    ESP_LOGI(TAG, "Mesh provisioning enabled");
    break;

  case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
    prov_complete(
        param->node_prov_complete.net_idx, param->node_prov_complete.addr,
        param->node_prov_complete.flags, param->node_prov_complete.iv_index);
    break;

  default:
    break;
  }
}

static void config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                             esp_ble_mesh_cfg_server_cb_param_t *param) {
  if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
    switch (param->ctx.recv_op) {
    case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
      ESP_LOGI(TAG, "AppKey added");
      cached_net_idx = param->value.state_change.appkey_add.net_idx;
      cached_app_idx = param->value.state_change.appkey_add.app_idx;
      gw_state.net_idx = cached_net_idx;
      gw_state.app_idx = cached_app_idx;
      save_gw_state();
      break;

    case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND: {
      uint16_t model_id = param->value.state_change.mod_app_bind.model_id;
      uint16_t company_id = param->value.state_change.mod_app_bind.company_id;
      ESP_LOGI(TAG, "Model bound: model=0x%04x, cid=0x%04x", model_id,
               company_id);
      gw_state.app_idx = param->value.state_change.mod_app_bind.app_idx;
      cached_app_idx = gw_state.app_idx;

      if (company_id == CID_ESP && model_id == VND_MODEL_ID_CLIENT) {
        vnd_bound = true;
        gw_state.vnd_bound_flag = 1;
        ESP_LOGI(TAG, "Vendor Client bound - full command support active!");
        gatt_notify_sensor_data("MESH_READY:VENDOR", 17);
      } else {
        gatt_notify_sensor_data("MESH_READY", 10);
      }
      save_gw_state();
      break;
    }
    }
  }
}

// ============== Initialization ==============
esp_err_t mesh_init(void) {
  esp_err_t err;

  esp_ble_mesh_register_prov_callback(provisioning_cb);
  esp_ble_mesh_register_config_server_callback(config_server_cb);
  esp_ble_mesh_register_generic_client_callback(generic_client_cb);
  esp_ble_mesh_register_custom_model_callback(custom_model_cb);

  err = esp_ble_mesh_init(&provision, &composition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Mesh init failed: %d", err);
    return err;
  }

  // Initialize vendor client model (required for op_pair timeout tracking)
  err = esp_ble_mesh_client_model_init(&vnd_models[0]);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Vendor client init failed: %d", err);
    return err;
  }

  err = esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV |
                                      ESP_BLE_MESH_PROV_GATT);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Enable provisioning failed: %d", err);
    return err;
  }

  ESP_LOGI(TAG, "Mesh initialized, waiting for provisioner...");
  return ESP_OK;
}
