/* ESP BLE Mesh GATT Gateway
 *
 * Bridges Pi 5 (GATT client) to the BLE Mesh network.
 *
 * This node:
 * - Is provisioned by the mesh provisioner (UUID prefix 0xdd)
 * - Provides GATT service 0xDC01 for Pi 5 to connect
 * - Receives commands from Pi 5 via GATT writes
 * - Translates commands to mesh Generic OnOff messages
 * - Forwards mesh responses back to Pi 5 via GATT notifications
 *
 * Command format from Pi 5: "NODE_ID:COMMAND"
 * Examples: "1:RAMP", "2:STOP", "1:DUTY:50", "ALL:RAMP"
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

// Mesh includes
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"

// NimBLE GATT includes
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "ble_mesh_example_init.h"
#include "ble_mesh_example_nvs.h"

#define TAG "MESH_GW"

#define CID_ESP 0x02E5

// Vendor model definitions (shared with mesh node and provisioner)
#define VND_MODEL_ID_CLIENT 0x0000
#define VND_OP_SEND   ESP_BLE_MESH_MODEL_OP_3(0x00, CID_ESP)
#define VND_OP_STATUS ESP_BLE_MESH_MODEL_OP_3(0x01, CID_ESP)

// UUID with prefix 0xdd 0xdd for auto-provisioning
static uint8_t dev_uuid[16] = {0xdd, 0xdd};

// ============== GATT Service UUIDs (compatible with Pi 5 gateway.py)
// ==============
#define DC_MONITOR_SERVICE_UUID 0xDC01
#define SENSOR_DATA_CHAR_UUID 0xDC02 // Read/Notify - data from mesh
#define COMMAND_CHAR_UUID 0xDC03     // Write - commands from Pi 5

#define SENSOR_DATA_MAX_LEN 128
#define COMMAND_MAX_LEN 64

// ============== Node Address Mapping ==============
// Node IDs in commands map to mesh unicast addresses
// Provisioner assigns: NODE-0 = 0x0005, NODE-1 = 0x0006, etc.
#define NODE_BASE_ADDR 0x0005
#define MAX_NODES 10
#define MESH_GROUP_ADDR 0xC000 // Group address for ALL commands

static uint16_t known_nodes[MAX_NODES] = {0}; // Unicast addrs of discovered nodes
static int known_node_count = 0;
static bool discovery_complete = false; // Set true when probe times out (no more nodes)

// Mesh state
static struct gateway_state {
  uint16_t net_idx;
  uint16_t app_idx;
  uint16_t addr;
  uint8_t vnd_bound_flag; // Persisted: vendor model bound to AppKey
} gw_state = {
    .net_idx = ESP_BLE_MESH_KEY_UNUSED,
    .app_idx = ESP_BLE_MESH_KEY_UNUSED,
    .addr = 0x0000,
    .vnd_bound_flag = 0,
};

// Monitor mode: gateway polls node with periodic READ commands
static TimerHandle_t monitor_timer = NULL;
static uint16_t monitor_target_addr = 0;
static bool monitor_waiting_response = false; // Don't send next READ until response
#define MONITOR_INTERVAL_MS 1000

static uint16_t cached_net_idx = 0xFFFF;
static uint16_t cached_app_idx = 0xFFFF;
static uint8_t msg_tid = 0;
static bool vnd_bound = false; // Vendor model bound to AppKey

// Vendor send serialization: prevent overlapping mesh sends
static bool vnd_send_busy = false;
static uint16_t vnd_send_target_addr = 0x0000;  // Address we're waiting for
static TickType_t vnd_send_start_tick = 0;
#define VND_SEND_TIMEOUT_MS 5000  // Auto-clear after 5s (match mesh SDK timeout)

// GATT state
static char sensor_data[SENSOR_DATA_MAX_LEN] = "GATEWAY_READY";
static uint16_t sensor_data_len = 13;
static uint16_t gatt_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t sensor_char_val_handle;

static nvs_handle_t NVS_HANDLE;
static const char *NVS_KEY = "mesh_gw";

// ============== NVS Save/Restore for Gateway State ==============
static void save_gw_state(void) {
  ble_mesh_nvs_store(NVS_HANDLE, NVS_KEY, &gw_state, sizeof(gw_state));
  ESP_LOGI(TAG, "Saved GW state: net=0x%04x, app=0x%04x, addr=0x%04x",
           gw_state.net_idx, gw_state.app_idx, gw_state.addr);
}

static void restore_gw_state(void) {
  bool exist = false;
  esp_err_t err = ble_mesh_nvs_restore(NVS_HANDLE, NVS_KEY, &gw_state,
                                        sizeof(gw_state), &exist);
  if (err == ESP_OK && exist) {
    cached_net_idx = gw_state.net_idx;
    cached_app_idx = gw_state.app_idx;
    // Restore vendor bound flag; also infer from valid app_idx
    // (provisioner always binds vendor model after adding AppKey)
    if (gw_state.vnd_bound_flag || cached_app_idx != 0xFFFF) {
      vnd_bound = true;
    }
    ESP_LOGI(TAG, "Restored GW state: net=0x%04x, app=0x%04x, addr=0x%04x, vnd=%d",
             gw_state.net_idx, gw_state.app_idx, gw_state.addr, vnd_bound);
  } else {
    ESP_LOGW(TAG, "No saved GW state found - waiting for provisioning");
  }
}

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

// ============== Known Node Tracking ==============
static void register_known_node(uint16_t addr) {
  // Don't register our own address
  if (addr == gw_state.addr)
    return;
  for (int i = 0; i < known_node_count; i++) {
    if (known_nodes[i] == addr)
      return; // Already known
  }
  if (known_node_count < MAX_NODES) {
    known_nodes[known_node_count++] = addr;
    discovery_complete = false; // New node found — re-enable probing
    ESP_LOGI(TAG, "Registered node 0x%04x (total: %d)", addr, known_node_count);
  }
}

// ============== GATT Notify Function ==============
// MTU through BLE Mesh GATT Proxy is hard-limited to 23 (20 bytes payload).
// Messages > 20 bytes are split into chunks:
//   - Continuation chunks: '+' prefix + 19 bytes data
//   - Final (or only) chunk: no prefix, up to 20 bytes
// Pi 5 reassembles by accumulating '+'-prefixed chunks.
#define GATT_MAX_PAYLOAD 20

static void gatt_notify_sensor_data(const char *data, uint16_t len) {
  if (gatt_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    ESP_LOGW(TAG, "GATT notify skipped (no connection): %.*s", len, data);
    return;
  }

  if (len >= SENSOR_DATA_MAX_LEN)
    len = SENSOR_DATA_MAX_LEN - 1;
  memcpy(sensor_data, data, len);
  sensor_data[len] = '\0';
  sensor_data_len = len;

  if (len <= GATT_MAX_PAYLOAD) {
    // Fits in a single notification
    struct os_mbuf *om = ble_hs_mbuf_from_flat(sensor_data, len);
    if (om) {
      int rc = ble_gatts_notify_custom(gatt_conn_handle, sensor_char_val_handle, om);
      if (rc == 0) {
        ESP_LOGI(TAG, "GATT notify (%d bytes): %s", len, sensor_data);
      } else {
        ESP_LOGW(TAG, "GATT notify failed (rc=%d), clearing conn_handle", rc);
        gatt_conn_handle = BLE_HS_CONN_HANDLE_NONE;
      }
    }
  } else {
    // Split into chunked notifications
    uint16_t offset = 0;
    int chunk_num = 0;
    while (offset < len) {
      uint8_t chunk[GATT_MAX_PAYLOAD];
      uint16_t chunk_len;
      uint16_t remaining = len - offset;

      if (remaining > GATT_MAX_PAYLOAD) {
        // Continuation chunk: '+' prefix + 19 bytes of data
        chunk[0] = '+';
        uint16_t data_in_chunk = GATT_MAX_PAYLOAD - 1;
        memcpy(chunk + 1, sensor_data + offset, data_in_chunk);
        chunk_len = GATT_MAX_PAYLOAD;
        offset += data_in_chunk;
      } else {
        // Final chunk: no prefix, just data
        memcpy(chunk, sensor_data + offset, remaining);
        chunk_len = remaining;
        offset += remaining;
      }

      struct os_mbuf *om = ble_hs_mbuf_from_flat(chunk, chunk_len);
      if (om) {
        int rc = ble_gatts_notify_custom(gatt_conn_handle, sensor_char_val_handle, om);
        if (rc != 0) {
          ESP_LOGW(TAG, "GATT chunk %d notify failed (rc=%d)", chunk_num, rc);
          gatt_conn_handle = BLE_HS_CONN_HANDLE_NONE;
          return;
        }
      }
      chunk_num++;
    }
    ESP_LOGI(TAG, "GATT notify chunked (%d chunks, %d bytes): %s",
             chunk_num, len, sensor_data);
  }
}

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
static esp_err_t send_mesh_onoff(uint16_t target_addr, uint8_t onoff) {
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
static esp_err_t send_vendor_command(uint16_t target_addr, const char *cmd,
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

// ============== Monitor Mode (gateway-side polling) ==============
static void monitor_timer_cb(TimerHandle_t xTimer) {
  // Guard: skip this tick if a command send is already in-flight.
  // The timer runs on the FreeRTOS timer daemon task, while GATT commands
  // run on the NimBLE host task — both can call send_vendor_command().
  if (monitor_target_addr != 0 && vnd_bound && !monitor_waiting_response && !vnd_send_busy) {
    monitor_waiting_response = true;
    send_vendor_command(monitor_target_addr, "read", 4);
  }
}

static void monitor_start(uint16_t target_addr) {
  monitor_target_addr = target_addr;
  if (monitor_timer == NULL) {
    monitor_timer = xTimerCreate("monitor", pdMS_TO_TICKS(MONITOR_INTERVAL_MS),
                                  pdTRUE, NULL, monitor_timer_cb);
  }
  xTimerStart(monitor_timer, 0);
  ESP_LOGI(TAG, "Monitor started: polling 0x%04x every %d ms",
           target_addr, MONITOR_INTERVAL_MS);
}

static void monitor_stop(void) {
  if (monitor_timer != NULL) {
    xTimerStop(monitor_timer, 0);
  }
  monitor_target_addr = 0;
  monitor_waiting_response = false;
  ESP_LOGI(TAG, "Monitor stopped");
}

// ============== Parse Pi 5 Command ==============
// Format: "NODE_ID:COMMAND" or "NODE_ID:COMMAND:VALUE"
// Examples: "1:RAMP", "2:STOP", "1:DUTY:50", "ALL:RAMP"
static void process_gatt_command(const char *cmd, uint16_t len) {
  char buf[COMMAND_MAX_LEN + 1];
  char *token;
  int node_id = -1;
  uint16_t target_addr = 0; // Initialize to avoid warning
  bool is_all = false;

  if (len > COMMAND_MAX_LEN)
    len = COMMAND_MAX_LEN;
  memcpy(buf, cmd, len);
  buf[len] = '\0';

  ESP_LOGI(TAG, "Pi5 command: %s", buf);

  // Parse node ID
  token = strtok(buf, ":");
  if (!token) {
    gatt_notify_sensor_data("ERROR:NO_NODE_ID", 15);
    return;
  }

  if (strcasecmp(token, "ALL") == 0) {
    is_all = true;
  } else {
    node_id = atoi(token);
    if (node_id < 0 || node_id >= MAX_NODES) {
      gatt_notify_sensor_data("ERROR:INVALID_NODE", 18);
      return;
    }
    target_addr = NODE_BASE_ADDR + node_id;
  }

  // Parse command
  token = strtok(NULL, ":");
  if (!token) {
    gatt_notify_sensor_data("ERROR:NO_COMMAND", 16);
    return;
  }

  // Build the Pico-native command string to send through vendor model
  // Map Pi 5 commands to what the Pico expects
  char pico_cmd[COMMAND_MAX_LEN];
  char *value_token = strtok(NULL, ":"); // Get optional value

  // Stop any active monitor when sending a new command (except MONITOR itself)
  bool is_monitor = (strcasecmp(token, "MONITOR") == 0);
  if (!is_monitor && monitor_target_addr != 0) {
    monitor_stop();
    // Wait for any in-flight monitor READ to complete (STATUS or timeout).
    // The monitor timer may have fired just before we stopped it, leaving
    // a pending transaction in the mesh SDK's vendor client model.
    int wait_ms = 0;
    while (vnd_send_busy && wait_ms < 3000) {
      vTaskDelay(pdMS_TO_TICKS(100));
      wait_ms += 100;
    }
    // Force-clear so the next command can proceed cleanly
    vnd_send_busy = false;
    vnd_send_target_addr = 0x0000;
  }

  if (strcasecmp(token, "RAMP") == 0) {
    snprintf(pico_cmd, sizeof(pico_cmd), "r");
  } else if (strcasecmp(token, "STOP") == 0 || strcasecmp(token, "OFF") == 0) {
    snprintf(pico_cmd, sizeof(pico_cmd), "s");
  } else if (strcasecmp(token, "ON") == 0) {
    snprintf(pico_cmd, sizeof(pico_cmd), "r");
  } else if (strcasecmp(token, "DUTY") == 0) {
    int duty = value_token ? atoi(value_token) : 50;
    snprintf(pico_cmd, sizeof(pico_cmd), "duty:%d", duty);
  } else if (strcasecmp(token, "STATUS") == 0 ||
             strcasecmp(token, "READ") == 0) {
    snprintf(pico_cmd, sizeof(pico_cmd), "read");
  } else if (is_monitor) {
    // Monitor mode: gateway polls node periodically with READ commands
    // Don't send "m" to Pico — the gateway drives the polling loop
    if (vnd_bound) {
      if (is_all) {
        // Monitor first known node (or node 1 as fallback)
        uint16_t addr = known_node_count > 0 ? known_nodes[0] : NODE_BASE_ADDR + 1;
        monitor_start(addr);
      } else {
        monitor_start(target_addr);
      }
      gatt_notify_sensor_data("SENT:MONITOR", 12);
    } else {
      gatt_notify_sensor_data("ERROR:NOT_READY", 15);
    }
    return; // Don't fall through to the normal send path
  } else {
    char resp[48];
    snprintf(resp, sizeof(resp), "ERROR:UNKNOWN_CMD:%s", token);
    gatt_notify_sensor_data(resp, strlen(resp));
    return;
  }

  // Route through vendor model if bound, else fall back to OnOff
  if (vnd_bound) {
    if (is_all) {
      // Single group send — all subscribed nodes receive simultaneously
      send_vendor_command(MESH_GROUP_ADDR, pico_cmd, strlen(pico_cmd));
    } else {
      send_vendor_command(target_addr, pico_cmd, strlen(pico_cmd));
    }
    char resp[48];
    snprintf(resp, sizeof(resp), "SENT:%s", token);
    gatt_notify_sensor_data(resp, strlen(resp));
  } else {
    // Fallback: OnOff only (lossy — no duty, no sensor data)
    uint8_t onoff = (strcasecmp(token, "STOP") == 0 ||
                     strcasecmp(token, "OFF") == 0)
                        ? 0
                        : 1;
    if (is_all) {
      if (known_node_count > 0) {
        for (int i = 0; i < known_node_count; i++) {
          send_mesh_onoff(known_nodes[i], onoff);
          if (i < known_node_count - 1) {
            vTaskDelay(pdMS_TO_TICKS(200));
          }
        }
      } else {
        send_mesh_onoff(NODE_BASE_ADDR + 1, onoff);
      }
    } else {
      send_mesh_onoff(target_addr, onoff);
    }
    gatt_notify_sensor_data(onoff ? "SENT:ON(fallback)" : "SENT:OFF(fallback)",
                            onoff ? 17 : 18);
  }
}

// ============== GATT Callbacks ==============
static int sensor_data_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
  if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    // Capture connection handle from proxy connection (GAP event may not fire)
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE &&
        gatt_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
      gatt_conn_handle = conn_handle;
      ESP_LOGI(TAG, "GATT conn_handle captured from read: %d", conn_handle);
    }
    int rc = os_mbuf_append(ctxt->om, sensor_data, sensor_data_len);
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }
  return 0;
}

static int command_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg) {
  if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    // Capture connection handle from proxy connection (GAP event may not fire)
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
      if (gatt_conn_handle != conn_handle) {
        gatt_conn_handle = conn_handle;
        ESP_LOGI(TAG, "GATT conn_handle captured from write: %d", conn_handle);
      }
    }

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    char buf[COMMAND_MAX_LEN + 1];

    if (len > COMMAND_MAX_LEN)
      len = COMMAND_MAX_LEN;

    int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);
    if (rc != 0)
      return BLE_ATT_ERR_UNLIKELY;

    buf[len] = '\0';

    // Process command from Pi 5
    process_gatt_command(buf, len);
  }
  return 0;
}

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(DC_MONITOR_SERVICE_UUID),
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = BLE_UUID16_DECLARE(SENSOR_DATA_CHAR_UUID),
                    .access_cb = sensor_data_access_cb,
                    .val_handle = &sensor_char_val_handle,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                },
                {
                    .uuid = BLE_UUID16_DECLARE(COMMAND_CHAR_UUID),
                    .access_cb = command_access_cb,
                    .flags = BLE_GATT_CHR_F_WRITE,
                },
                {0},
            },
    },
    {0},
};

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    if (event->connect.status == 0) {
      gatt_conn_handle = event->connect.conn_handle;
      ESP_LOGI(TAG, "Pi 5 connected!");
      gatt_notify_sensor_data("GATEWAY_CONNECTED", 17);
    } else {
      gatt_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }
    break;

  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGI(TAG, "Pi 5 disconnected");
    gatt_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    // Restart advertising
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, NULL,
                      ble_gap_event, NULL);
    break;

  case BLE_GAP_EVENT_SUBSCRIBE:
    ESP_LOGI(TAG, "Pi 5 subscribed to notifications (handle=%d)",
             event->subscribe.conn_handle);
    // Also capture handle from subscribe event (proxy connection fallback)
    if (event->subscribe.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
      gatt_conn_handle = event->subscribe.conn_handle;
    }
    break;

  case BLE_GAP_EVENT_MTU:
    ESP_LOGI(TAG, "MTU updated: conn_handle=%d, mtu=%d",
             event->mtu.conn_handle, event->mtu.value);
    break;
  }
  return 0;
}

static void gatt_advertise(void) {
  struct ble_gap_adv_params adv_params = {0};
  struct ble_hs_adv_fields fields = {0};

  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

  const char *name = "Mesh-Gateway";
  fields.name = (uint8_t *)name;
  fields.name_len = strlen(name);
  fields.name_is_complete = 1;

  fields.uuids16 = (ble_uuid16_t[]){BLE_UUID16_INIT(DC_MONITOR_SERVICE_UUID)};
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;

  ble_gap_adv_set_fields(&fields);

  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params,
                    ble_gap_event, NULL);

  ESP_LOGI(TAG, "GATT advertising as '%s'", name);
}

static void gatt_on_sync(void) {
  ESP_LOGI(TAG, "GATT stack synchronized");
  gatt_advertise();
}

static void gatt_host_task(void *param) {
  ESP_LOGI(TAG, "GATT host task started");
  nimble_port_run();
  nimble_port_freertos_deinit();
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
static esp_err_t mesh_init(void) {
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

static esp_err_t gatt_register_services(void) {
  // Register custom GATT services BEFORE mesh init locks the GATT table
  // bluetooth_init() has already initialized NimBLE and the GAP/GATT stack

  // Set preferred ATT MTU so sensor data isn't truncated (default 23 = 20 payload)
  int rc_mtu = ble_att_set_preferred_mtu(185);
  ESP_LOGI(TAG, "Set preferred MTU=185, rc=%d", rc_mtu);

  // Set device name
  ble_svc_gap_device_name_set("Mesh-Gateway");

  int rc = ble_gatts_count_cfg(gatt_svr_svcs);
  if (rc != 0) {
    ESP_LOGE(TAG, "GATT count failed: %d", rc);
    return ESP_FAIL;
  }

  rc = ble_gatts_add_svcs(gatt_svr_svcs);
  if (rc != 0) {
    ESP_LOGE(TAG, "GATT add failed: %d", rc);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "GATT services registered");
  return ESP_OK;
}

static void gatt_start_advertising(void) {
  // Start advertising AFTER mesh init (called from app_main or on event)
  gatt_advertise();
  ESP_LOGI(TAG, "GATT advertising started");
}

void app_main(void) {
  esp_err_t err;

  printf("\n");
  printf("==========================================\n");
  printf("  ESP32-C6 BLE Mesh GATT Gateway\n");
  printf("  Bridges Pi 5 <-> Mesh Network\n");
  printf("==========================================\n\n");

  // Initialize NVS
  err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  // Open NVS
  err = ble_mesh_nvs_open(&NVS_HANDLE);
  if (err) {
    ESP_LOGE(TAG, "NVS open failed");
    return;
  }

  // Initialize Bluetooth (NimBLE stack)
  err = bluetooth_init();
  if (err) {
    ESP_LOGE(TAG, "Bluetooth init failed: %d", err);
    return;
  }

  // Get device UUID
  ble_mesh_get_dev_uuid(dev_uuid);

  // Register custom GATT services BEFORE mesh init (mesh locks GATT table)
  err = gatt_register_services();
  if (err) {
    ESP_LOGE(TAG, "GATT register failed");
    return;
  }

  // Initialize mesh (this locks the GATT table)
  err = mesh_init();
  if (err) {
    ESP_LOGE(TAG, "Mesh init failed");
    return;
  }

  // Start GATT advertising for Pi 5 connection
  gatt_start_advertising();

  ESP_LOGI(TAG, "============================================");
  ESP_LOGI(TAG, "  Gateway running");
  ESP_LOGI(TAG, "  - Mesh: waiting for provisioner");
  ESP_LOGI(TAG, "  - GATT: advertising 'Mesh-Gateway'");
  ESP_LOGI(TAG, "  Command format: NODE_ID:COMMAND");
  ESP_LOGI(TAG, "  Examples: 0:RAMP, 1:STOP, ALL:ON");
  ESP_LOGI(TAG, "============================================");
}
