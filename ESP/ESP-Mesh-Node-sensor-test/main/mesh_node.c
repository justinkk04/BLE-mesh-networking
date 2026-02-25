#include "mesh_node.h"
#include "nvs_store.h"
#include "command.h"
#include "load_control.h"
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

#define CID_ESP 0x02E5

// Vendor model definitions (shared with gateway and provisioner)
#define VND_MODEL_ID_SERVER 0x0001
#define VND_OP_SEND ESP_BLE_MESH_MODEL_OP_3(0x00, CID_ESP)
#define VND_OP_STATUS ESP_BLE_MESH_MODEL_OP_3(0x01, CID_ESP)

// UUID with prefix 0xdd 0xdd for auto-provisioning
uint8_t dev_uuid[16] = {0xdd, 0xdd};

// ============== Mesh State Storage ==============
struct mesh_node_state node_state = {
    .net_idx = ESP_BLE_MESH_KEY_UNUSED,
    .app_idx = ESP_BLE_MESH_KEY_UNUSED,
    .addr = 0x0000,
    .onoff = 0,
    .tid = 0,
};

// Cached indices for sending
uint16_t cached_net_idx = 0xFFFF;
uint16_t cached_app_idx = 0xFFFF;

// ============== Mesh Models ==============
static esp_ble_mesh_client_t onoff_client;

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

// Vendor model: receives commands from gateway, sends sensor data back
static esp_ble_mesh_model_op_t vnd_op[] = {
    ESP_BLE_MESH_MODEL_OP(VND_OP_SEND, 1),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, VND_MODEL_ID_SERVER, vnd_op, NULL, NULL),
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

    case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
      ESP_LOGI(TAG,
               "Model bound: elem=0x%04x, app=0x%04x, model=0x%04x, cid=0x%04x",
               param->value.state_change.mod_app_bind.element_addr,
               param->value.state_change.mod_app_bind.app_idx,
               param->value.state_change.mod_app_bind.model_id,
               param->value.state_change.mod_app_bind.company_id);

      if (param->value.state_change.mod_app_bind.company_id == 0xFFFF &&
          (param->value.state_change.mod_app_bind.model_id ==
               ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI ||
           param->value.state_change.mod_app_bind.model_id ==
               ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV)) {
        node_state.app_idx = param->value.state_change.mod_app_bind.app_idx;
        cached_app_idx = node_state.app_idx;
        save_node_state();
        ESP_LOGI(TAG, "Node fully configured and ready!");
      }
      // Also handle vendor model bind
      if (param->value.state_change.mod_app_bind.company_id == CID_ESP &&
          param->value.state_change.mod_app_bind.model_id ==
              VND_MODEL_ID_SERVER) {
        ESP_LOGI(TAG, "Vendor Server model bound - full command support!");
      }
      break;

    default:
      break;
    }
  }
}

// Handle incoming OnOff commands - now executes directly instead of UART
// forward
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

      // Direct control: ON = set duty 100%, OFF = set duty 0%
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

// Client callbacks
static void generic_client_cb(esp_ble_mesh_generic_client_cb_event_t event,
                              esp_ble_mesh_generic_client_cb_param_t *param) {
  ESP_LOGI(TAG, "Client event: 0x%02x, opcode: 0x%04" PRIx32, event,
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

  case ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT:
    ESP_LOGW(TAG, "Client timeout");
    break;

  default:
    break;
  }
}

// ============== Vendor Model Callback ==============
// Receives commands from GATT Gateway, processes locally, responds
// synchronously
static void custom_model_cb(esp_ble_mesh_model_cb_event_t event,
                            esp_ble_mesh_model_cb_param_t *param) {
  switch (event) {
  case ESP_BLE_MESH_MODEL_OPERATION_EVT:
    if (param->model_operation.opcode == VND_OP_SEND) {
      // Extract text command
      char cmd[64];
      uint16_t len = param->model_operation.length;
      if (len >= sizeof(cmd))
        len = sizeof(cmd) - 1;
      memcpy(cmd, param->model_operation.msg, len);
      cmd[len] = '\0';

      ESP_LOGI(TAG, "Vendor SEND from 0x%04x: %s",
               param->model_operation.ctx->addr, cmd);

      // Process command and respond synchronously (no UART, no queue!)
      char response[128];
      int resp_len = process_command(cmd, response, sizeof(response));

      // Send response back through mesh immediately
      esp_ble_mesh_msg_ctx_t ctx = *param->model_operation.ctx;
      // When message arrived via group address (0xC000), recv_dst is the
      // group addr.  The server send uses recv_dst as the reply source,
      // but we can't send FROM a group address — override with our unicast.
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
    }
    break;

  case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
    if (param->model_send_comp.err_code) {
      ESP_LOGE(TAG, "Vendor send COMP err=%d", param->model_send_comp.err_code);
    } else {
      ESP_LOGI(TAG, "Vendor send COMP OK");
    }
    break;

  default:
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

  err = esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV |
                                      ESP_BLE_MESH_PROV_GATT);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Enable provisioning failed: %d", err);
    return err;
  }

  ESP_LOGI(TAG, "============================================");
  ESP_LOGI(TAG, "  BLE Mesh Node Ready");
  ESP_LOGI(TAG, "  UUID: %s", bt_hex(dev_uuid, 16));
  ESP_LOGI(TAG, "  Waiting for provisioner...");
  ESP_LOGI(TAG, "============================================");

  return ESP_OK;
}
