#include "node_tracker.h"
#include "mesh_node.h"

#include "esp_log.h"

#define TAG "NODE_TRACK"

uint16_t known_nodes[MAX_NODES] = {0}; // Unicast addrs of discovered nodes
int known_node_count = 0;
bool discovery_complete = false; // Set true when probe times out (no more nodes)

void register_known_node(uint16_t addr) {
  // Don't register our own address
  if (addr == node_state.addr)
    return;
  for (int i = 0; i < known_node_count; i++) {
    if (known_nodes[i] == addr)
      return; // Already known
  }
  if (known_node_count < MAX_NODES) {
    known_nodes[known_node_count++] = addr;
    discovery_complete = false; // New node found - re-enable probing
    ESP_LOGI(TAG, "Registered node 0x%04x (total: %d)", addr, known_node_count);
  }
}
