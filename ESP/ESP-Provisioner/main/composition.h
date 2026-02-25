/* Composition data parsing */

#ifndef COMPOSITION_H
#define COMPOSITION_H

#include "node_registry.h"
#include "mesh_config.h"

void parse_composition_data(mesh_node_info_t *node,
                            struct net_buf_simple *comp);

#endif /* COMPOSITION_H */
