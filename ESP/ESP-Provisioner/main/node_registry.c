/* Node registry: track provisioned nodes */

#include "esp_log.h"

#include "node_registry.h"

#define TAG "NODE_REG"

mesh_node_info_t nodes[MAX_NODES] = {0};
int node_count = 0;

// Store node info
esp_err_t store_node_info(const uint8_t uuid[16], uint16_t unicast,
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

mesh_node_info_t *get_node_info(uint16_t unicast) {
  for (int i = 0; i < node_count; i++) {
    if (nodes[i].unicast <= unicast &&
        nodes[i].unicast + nodes[i].elem_num > unicast) {
      return &nodes[i];
    }
  }
  return NULL;
}
