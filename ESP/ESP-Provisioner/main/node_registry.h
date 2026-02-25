/* Node registry: track provisioned nodes */

#ifndef NODE_REGISTRY_H
#define NODE_REGISTRY_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"

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
  bool vnd_srv_subscribed; // Vendor Server subscribed to group 0xC000
} mesh_node_info_t;

#define MAX_NODES 10
extern mesh_node_info_t nodes[MAX_NODES];
extern int node_count;

esp_err_t store_node_info(const uint8_t uuid[16], uint16_t unicast,
                          uint8_t elem_num, int node_idx);

mesh_node_info_t *get_node_info(uint16_t unicast);

#endif /* NODE_REGISTRY_H */
