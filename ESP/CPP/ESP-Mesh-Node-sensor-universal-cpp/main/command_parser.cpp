#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "command_parser.h"
#include "mesh_node.h"
#include "gatt_service.h"
#include "node_tracker.h"
#include "monitor.h"
#include "command.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
    constexpr char TAG[] = "CMD_PARSE";
}

// Helper: process command locally and notify Pi 5 via GATT
static void process_local_and_notify(const char *pico_cmd) {
  char response[128];
  int resp_len = process_command(pico_cmd, response, sizeof(response));

  char buf[SENSOR_DATA_MAX_LEN];
  int node_num = (node_state.addr >= NODE_BASE_ADDR)
                     ? (node_state.addr - NODE_BASE_ADDR)
                     : 0;
  int hdr_len = snprintf(buf, sizeof(buf), "NODE%d:DATA:", node_num);
  if (hdr_len + resp_len < static_cast<int>(sizeof(buf))) {
    memcpy(buf + hdr_len, response, resp_len);
    buf[hdr_len + resp_len] = '\0';
    gatt_notify_sensor_data(buf, static_cast<uint16_t>(hdr_len + resp_len));
  }
}

// ============== Parse Pi 5 Command ==============
// Format: "NODE_ID:COMMAND" or "NODE_ID:COMMAND:VALUE"
// Examples: "1:RAMP", "2:STOP", "1:DUTY:50", "ALL:RAMP"
void process_gatt_command(const char *cmd, uint16_t len) {
  char buf[COMMAND_MAX_LEN + 1];
  char *saveptr = nullptr; // strtok_r state — stack-local, re-entrant safe
  int node_id = -1;
  uint16_t target_addr = 0;
  bool is_all = false;

  if (len > COMMAND_MAX_LEN)
    len = COMMAND_MAX_LEN;
  memcpy(buf, cmd, len);
  buf[len] = '\0';

  ESP_LOGI(TAG, "Pi5 command: %s", buf);

  // Parse node ID
  char *token = strtok_r(buf, ":", &saveptr);
  if (!token) {
    gatt_notify_sensor_data("ERROR:NO_NODE_ID", 15);
    return;
  }

  if (strcasecmp(token, "ALL") == 0) {
    is_all = true;
  } else {
    node_id = static_cast<int>(strtol(token, nullptr, 10));
    if (node_id < 0 || node_id >= MAX_NODES) {
      gatt_notify_sensor_data("ERROR:INVALID_NODE", 18);
      return;
    }
    target_addr = static_cast<uint16_t>(NODE_BASE_ADDR + node_id);
  }

  // Parse command
  token = strtok_r(nullptr, ":", &saveptr);
  if (!token) {
    gatt_notify_sensor_data("ERROR:NO_COMMAND", 16);
    return;
  }

  // Build the command string to send through vendor model
  char pico_cmd[COMMAND_MAX_LEN];
  char *value_token = strtok_r(nullptr, ":", &saveptr); // Get optional value

  // Stop any active monitor when sending a new command (except MONITOR itself)
  bool is_monitor = (strcasecmp(token, "MONITOR") == 0);
  if (!is_monitor && monitor_target_addr != 0) {
    monitor_stop();
    int wait_ms = 0;
    while (vnd_send_busy && wait_ms < 3000) {
      vTaskDelay(pdMS_TO_TICKS(100));
      wait_ms += 100;
    }
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
    int duty = value_token ? static_cast<int>(strtol(value_token, nullptr, 10)) : 50;
    snprintf(pico_cmd, sizeof(pico_cmd), "duty:%d", duty);
  } else if (strcasecmp(token, "STATUS") == 0 ||
             strcasecmp(token, "READ") == 0) {
    snprintf(pico_cmd, sizeof(pico_cmd), "read");
  } else if (is_monitor) {
    if (vnd_bound) {
      if (is_all) {
        uint16_t addr = known_node_count > 0 ? known_nodes[0] : static_cast<uint16_t>(NODE_BASE_ADDR + 1);
        monitor_start(addr);
      } else {
        monitor_start(target_addr);
      }
      gatt_notify_sensor_data("SENT:MONITOR", 12);
    } else {
      gatt_notify_sensor_data("ERROR:NOT_READY", 15);
    }
    return;
  } else {
    char resp[48];
    snprintf(resp, sizeof(resp), "ERROR:UNKNOWN_CMD:%s", token);
    gatt_notify_sensor_data(resp, static_cast<uint16_t>(strlen(resp)));
    return;
  }

  // ---- Self-addressing: if targeting this node, process locally ----
  if (!is_all && target_addr == node_state.addr) {
    process_local_and_notify(pico_cmd);
    return;
  }

  // Route through vendor model if bound, else fall back to OnOff
  if (vnd_bound) {
    if (is_all) {
      // Process locally first (group send doesn't reach local server)
      process_local_and_notify(pico_cmd);
      // Then send to mesh group for other nodes
      send_vendor_command(MESH_GROUP_ADDR, pico_cmd, static_cast<uint16_t>(strlen(pico_cmd)));
    } else {
      send_vendor_command(target_addr, pico_cmd, static_cast<uint16_t>(strlen(pico_cmd)));
    }
    char resp[48];
    snprintf(resp, sizeof(resp), "SENT:%s", token);
    gatt_notify_sensor_data(resp, static_cast<uint16_t>(strlen(resp)));
  } else {
    // Fallback: OnOff only (lossy - no duty, no sensor data)
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
        send_mesh_onoff(static_cast<uint16_t>(NODE_BASE_ADDR + 1), onoff);
      }
    } else {
      send_mesh_onoff(target_addr, onoff);
    }
    gatt_notify_sensor_data(onoff ? "SENT:ON(fallback)" : "SENT:OFF(fallback)",
                            onoff ? 17 : 18);
  }
}
