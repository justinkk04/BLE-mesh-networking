#ifndef NODE_TRACKER_H
#define NODE_TRACKER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_NODES 10
#define NODE_BASE_ADDR 0x0005

extern uint16_t known_nodes[MAX_NODES];
extern int known_node_count;
extern bool discovery_complete;

void register_known_node(uint16_t addr);

#ifdef __cplusplus
}
#endif

#endif /* NODE_TRACKER_H */
