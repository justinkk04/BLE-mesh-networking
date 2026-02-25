#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "command_parser.h"
#include "mesh_gateway.h"
#include "gatt_service.h"
#include "node_tracker.h"
#include "monitor.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "CMD_PARSE"

// ============== Parse Pi 5 Command ==============
// Format: "NODE_ID:COMMAND" or "NODE_ID:COMMAND:VALUE"
// Examples: "1:RAMP", "2:STOP", "1:DUTY:50", "ALL:RAMP"
void process_gatt_command(const char *cmd, uint16_t len) {
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
