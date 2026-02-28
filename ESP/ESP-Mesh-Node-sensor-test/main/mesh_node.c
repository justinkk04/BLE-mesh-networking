#include "mesh_node.h"
#include "nvs_store.h"
#include "command.h"
#include "load_control.h"
#include "gatt_service.h"
#include "node_tracker.h"
#include "esp_log.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "ble_mesh_example_init.h"
#include <inttypes.h>
#include <string.h>

static const char *TAG = "MESH_NODE";

// UUID with prefix 0xdd 0xdd for auto-provisioning
uint8_t dev_uuid[16] = {0xdd, 0xdd};

// ============== Mesh State Storage ==============
struct mesh_node_state node_state = {
    .net_idx = ESP_BLE_MESH_KEY_UNUSED,
    .app_idx = ESP_BLE_MESH_KEY_UNUSED,
    .addr = 0x0000,
    .onoff = 0,
    .tid = 0,
    .vnd_bound_flag = 0,
};

// Cached indices for sending
uint16_t cached_net_idx = 0xFFFF;
uint16_t cached_app_idx = 0xFFFF;

// Vendor client state
bool vnd_bound = false;
bool vnd_send_busy = false;
uint16_t vnd_send_target_addr = 0x0000;
static TickType_t vnd_send_start_tick = 0;

// Monitor mode state (shared with monitor.c and command_parser.c)
uint16_t monitor_target_addr = 0;
bool monitor_waiting_response = false;

// ============== Mesh Models ==============
static esp_ble_mesh_client_t onoff_client;

// --- Vendor SERVER model: receives commands from mesh ---
static esp_ble_mesh_model_op_t vnd_srv_op[] = {
    ESP_BLE_MESH_MODEL_OP(VND_OP_SEND, 1),
    ESP_BLE_MESH_MODEL_OP_END,
};

// --- Vendor CLIENT model: sends commands TO other mesh nodes ---
static const esp_ble_mesh_client_op_pair_t vnd_op_pair[] = {
    {VND_OP_SEND, VND_OP_STATUS},
};

static esp_ble_mesh_client_t vendor_client = {
    .op_pair_size = ARRAY_SIZE(vnd_op_pair),
    .op_pair = vnd_op_pair,
};

static esp_ble_mesh_model_op_t vnd_cli_op[] = {
    ESP_BLE_MESH_MODEL_OP(VND_OP_STATUS, 1),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_cfg_srv_t config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(3, 20),
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(4, 20),
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

ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_cli_pub, 2 + 1, ROLE_NODE);
ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_srv_pub, 2 + 3, ROLE_NODE);

static esp_ble_mesh_gen_onoff_srv_t onoff_server = {
    .rsp_ctrl =
        {
            .get_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
            .set_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
        },
};

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(&onoff_cli_pub, &onoff_client),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(&onoff_srv_pub, &onoff_server),
};

// Both vendor models on the same element
static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, VND_MODEL_ID_SERVER, vnd_srv_op, NULL, NULL),
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, VND_MODEL_ID_CLIENT, vnd_cli_op, NULL, &vendor_client),
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

// ============== Send Mesh OnOff Command (fallback) ==============
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
  common.msg_timeout = 2000;

  set.onoff_set.op_en = false;
  set.onoff_set.onoff = onoff;
  set.onoff_set.tid = node_state.tid++;

  return esp_ble_mesh_generic_client_set_state(&common, &set);
}

// ============== Send Vendor Command ==============
esp_err_t send_vendor_command(uint16_t target_addr, const char *cmd,
                              uint16_t len) {
  bool is_group = (target_addr == MESH_GROUP_ADDR);

  // Skip busy-wait for group sends - multiple nodes respond asynchronously
  if (!is_group) {
    int wait_loops = 0;
    while (vnd_send_busy && wait_loops < 50) {
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
  ctx.send_ttl = 7;

  ESP_LOGI(TAG, "Vendor SEND to 0x%04x: %.*s", target_addr, len, cmd);

  if (!is_group) {
    vnd_send_busy = true;
    vnd_send_target_addr = target_addr;
    vnd_send_start_tick = xTaskGetTickCount();
  }

  esp_err_t err = esp_ble_mesh_client_model_send_msg(
      vendor_client.model, &ctx, VND_OP_SEND, len, (uint8_t *)cmd,
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

// ============== Mesh Callbacks ==============
static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags,
                          uint32_t iv_index) {
  ESP_LOGI(TAG, "========== PROVISIONED ==========");
  ESP_LOGI(TAG, "NetKey index: 0x%04x", net_idx);
  ESP_LOGI(TAG, "Unicast addr: 0x%04x", addr);
  ESP_LOGI(TAG, "Flags: 0x%02x, IV: 0x%08" PRIx32, flags, iv_index);
  ESP_LOGI(TAG, "==================================");

  node_state.net_idx = net_idx;
  node_state.addr = addr;
  cached_net_idx = net_idx;
}

static void provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                            esp_ble_mesh_prov_cb_param_t *param) {
  switch (event) {
  case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
    ESP_LOGI(TAG, "Mesh stack registered");
    onoff_client.model = &elements[0].sig_models[1];
    vendor_client.model = &vnd_models[1]; // CLIENT is index 1
    restore_node_state();
    break;

  case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
    ESP_LOGI(TAG, "Provisioning enabled");
    break;

  case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
    ESP_LOGI(TAG, "Provisioning link opened (%s)",
             param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV
                 ? "PB-ADV"
                 : "PB-GATT");
    break;

  case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
    ESP_LOGI(TAG, "Provisioning link closed");
    break;

  case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
    prov_complete(
        param->node_prov_complete.net_idx, param->node_prov_complete.addr,
        param->node_prov_complete.flags, param->node_prov_complete.iv_index);
    break;

  case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
    ESP_LOGW(TAG, "Node reset - reprovisioning needed");
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
      ESP_LOGI(TAG, "AppKey added: net=0x%04x, app=0x%04x",
               param->value.state_change.appkey_add.net_idx,
               param->value.state_change.appkey_add.app_idx);
      cached_net_idx = param->value.state_change.appkey_add.net_idx;
      cached_app_idx = param->value.state_change.appkey_add.app_idx;
      node_state.net_idx = cached_net_idx;
      node_state.app_idx = cached_app_idx;
      save_node_state();
      break;

    case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND: {
      uint16_t model_id = param->value.state_change.mod_app_bind.model_id;
      uint16_t company_id = param->value.state_change.mod_app_bind.company_id;
      ESP_LOGI(TAG,
               "Model bound: elem=0x%04x, app=0x%04x, model=0x%04x, cid=0x%04x",
               param->value.state_change.mod_app_bind.element_addr,
               param->value.state_change.mod_app_bind.app_idx,
               model_id, company_id);

      node_state.app_idx = param->value.state_change.mod_app_bind.app_idx;
      cached_app_idx = node_state.app_idx;

      if (company_id == 0xFFFF &&
          (model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI ||
           model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV)) {
        ESP_LOGI(TAG, "OnOff model bound");
      }

      if (company_id == CID_ESP && model_id == VND_MODEL_ID_SERVER) {
        ESP_LOGI(TAG, "Vendor Server model bound - mesh command support!");
      }

      if (company_id == CID_ESP && model_id == VND_MODEL_ID_CLIENT) {
        vnd_bound = true;
        node_state.vnd_bound_flag = 1;
        ESP_LOGI(TAG, "Vendor Client bound - GATT gateway capability active!");
        gatt_notify_sensor_data("MESH_READY:VENDOR", 17);
      }

      save_node_state();
      break;
    }

    default:
      break;
    }
  }
}

// Handle incoming OnOff commands - executes directly
static void generic_server_cb(esp_ble_mesh_generic_server_cb_event_t event,
                              esp_ble_mesh_generic_server_cb_param_t *param) {
  ESP_LOGI(TAG, "Server event: 0x%02x, opcode: 0x%04" PRIx32 ", src: 0x%04x",
           event, param->ctx.recv_op, param->ctx.addr);

  switch (event) {
  case ESP_BLE_MESH_GENERIC_SERVER_RECV_GET_MSG_EVT:
    if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET) {
      ESP_LOGI(TAG, "OnOff GET - reading sensor");
      char resp[128];
      format_sensor_response(resp, sizeof(resp));
      ESP_LOGI(TAG, "Sensor: %s", resp);
    }
    break;

  case ESP_BLE_MESH_GENERIC_SERVER_RECV_SET_MSG_EVT:
    if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET ||
        param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK) {

      uint8_t onoff = param->value.set.onoff.onoff;
      ESP_LOGI(TAG, "OnOff SET: %d from 0x%04x", onoff, param->ctx.addr);

      if (onoff) {
        set_duty(100);
      } else {
        set_duty(0);
      }

      node_state.onoff = onoff;

      if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
        esp_ble_mesh_gen_onoff_srv_t *srv = param->model->user_data;
        if (srv)
          srv->state.onoff = onoff;
        esp_ble_mesh_msg_ctx_t ctx = param->ctx;
        uint8_t status_msg[1] = {onoff};
        esp_ble_mesh_server_model_send_msg(
            param->model, &ctx, ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_STATUS,
            sizeof(status_msg), status_msg);
      }

      if (cached_net_idx == 0xFFFF) {
        cached_net_idx = param->ctx.net_idx;
        cached_app_idx = param->ctx.app_idx;
      }
    }
    break;

  default:
    break;
  }
}

// Client callbacks - forward OnOff responses to Pi 5 via GATT
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

// ============== Vendor Model Callback (Dual Role) ==============
// SERVER role: receives commands from mesh, processes locally, responds
// CLIENT role: receives responses from other nodes, forwards to Pi 5 via GATT
static void custom_model_cb(esp_ble_mesh_model_cb_event_t event,
                            esp_ble_mesh_model_cb_param_t *param) {
  switch (event) {
  case ESP_BLE_MESH_MODEL_OPERATION_EVT:
    if (param->model_operation.opcode == VND_OP_SEND) {
      // ---- SERVER role: received a command, process locally ----
      uint16_t src_addr = param->model_operation.ctx->addr;

      // Skip self-echoed group messages: when WE send ALL: via group,
      // mesh delivers it back to us. command_parser.c already processed
      // the local part, so don't duplicate.
      if (src_addr == node_state.addr) {
        ESP_LOGD(TAG, "Skipping self-echo from 0x%04x", src_addr);
        break;
      }

      char cmd[64];
      uint16_t len = param->model_operation.length;
      if (len >= sizeof(cmd))
        len = sizeof(cmd) - 1;
      memcpy(cmd, param->model_operation.msg, len);
      cmd[len] = '\0';

      ESP_LOGI(TAG, "Vendor SEND from 0x%04x: %s",
               src_addr, cmd);

      char response[128];
      int resp_len = process_command(cmd, response, sizeof(response));

      esp_ble_mesh_msg_ctx_t ctx = *param->model_operation.ctx;
      // When message arrived via group address, override recv_dst with unicast
      if (ctx.recv_dst != node_state.addr) {
        ctx.recv_dst = node_state.addr;
      }
      esp_err_t err = esp_ble_mesh_server_model_send_msg(
          &vnd_models[0], &ctx, VND_OP_STATUS, resp_len, (uint8_t *)response);

      if (err) {
        ESP_LOGE(TAG, "Vendor STATUS send failed: %d", err);
      } else {
        ESP_LOGI(TAG, "Response -> 0x%04x: %s", ctx.addr, response);
      }
    } else if (param->model_operation.opcode == VND_OP_STATUS) {
      // ---- CLIENT role: received response from another node ----
      // Forward to Pi 5 via GATT notify
      char buf[SENSOR_DATA_MAX_LEN];
      uint16_t len = param->model_operation.length;
      uint16_t src = param->model_operation.ctx->addr;

      if (len >= sizeof(buf))
        len = sizeof(buf) - 1;
      int node_id = (src >= NODE_BASE_ADDR) ? (src - NODE_BASE_ADDR) : 0;

      if (src == vnd_send_target_addr || vnd_send_target_addr == 0x0000) {
        vnd_send_busy = false;
        vnd_send_target_addr = 0x0000;
      }

      register_known_node(src);

      int hdr_len = snprintf(buf, sizeof(buf), "NODE%d:DATA:", node_id);
      if (hdr_len + len < (int)sizeof(buf)) {
        memcpy(buf + hdr_len, param->model_operation.msg, len);
        buf[hdr_len + len] = '\0';
        gatt_notify_sensor_data(buf, hdr_len + len);
      }

      ESP_LOGI(TAG, "Vendor STATUS from 0x%04x (%d bytes)", src, len);

      if (monitor_target_addr != 0) {
        monitor_waiting_response = false;
      }
    }
    break;

  case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
    if (param->model_send_comp.err_code) {
      ESP_LOGE(TAG, "Vendor send COMP err=%d", param->model_send_comp.err_code);
      vnd_send_busy = false;
      vnd_send_target_addr = 0x0000;
      gatt_notify_sensor_data("ERROR:MESH_SEND_FAIL", 20);
    } else {
      ESP_LOGI(TAG, "Vendor send COMP OK");
    }
    break;

  case ESP_BLE_MESH_CLIENT_MODEL_SEND_TIMEOUT_EVT: {
    uint16_t timeout_target = vnd_send_target_addr;
    if (timeout_target == 0x0000 && !vnd_send_busy) {
      ESP_LOGI(TAG, "Group send timeout (expected, responses already received)");
      break;
    }
    ESP_LOGW(TAG, "Vendor message timeout (target was 0x%04x)", timeout_target);
    if (timeout_target > NODE_BASE_ADDR + known_node_count) {
      discovery_complete = true;
      ESP_LOGI(TAG, "Discovery complete (no node at 0x%04x)", timeout_target);
    }
    vnd_send_busy = false;
    vnd_send_target_addr = 0x0000;
    monitor_waiting_response = false;
    if (monitor_target_addr == 0) {
      gatt_notify_sensor_data("ERROR:MESH_TIMEOUT", 18);
    }
    break;
  }

  case ESP_BLE_MESH_CLIENT_MODEL_RECV_PUBLISH_MSG_EVT:
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
      if (hdr_len + len < (int)sizeof(buf)) {
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

// ============== Mesh Initialization ==============
esp_err_t ble_mesh_init(void) {
  esp_err_t err;

  esp_ble_mesh_register_prov_callback(provisioning_cb);
  esp_ble_mesh_register_config_server_callback(config_server_cb);
  esp_ble_mesh_register_generic_server_callback(generic_server_cb);
  esp_ble_mesh_register_generic_client_callback(generic_client_cb);
  esp_ble_mesh_register_custom_model_callback(custom_model_cb);

  err = esp_ble_mesh_init(&provision, &composition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Mesh init failed: %d", err);
    return err;
  }

  // Initialize vendor client model (required for op_pair timeout tracking)
  err = esp_ble_mesh_client_model_init(&vnd_models[1]);
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

  ESP_LOGI(TAG, "============================================");
  ESP_LOGI(TAG, "  BLE Mesh Universal Node Ready");
  ESP_LOGI(TAG, "  UUID: %s", bt_hex(dev_uuid, 16));
  ESP_LOGI(TAG, "  Models: Vendor Server + Vendor Client");
  ESP_LOGI(TAG, "  Waiting for provisioner...");
  ESP_LOGI(TAG, "============================================");

  return ESP_OK;
}
