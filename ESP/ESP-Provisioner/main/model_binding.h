/* Model binding: bind models to AppKey and subscribe to groups */

#ifndef MODEL_BINDING_H
#define MODEL_BINDING_H

#include "mesh_config.h"
#include "node_registry.h"

esp_err_t bind_model(mesh_node_info_t *node, uint16_t model_id);

esp_err_t bind_vendor_model(mesh_node_info_t *node, uint16_t model_id);

esp_err_t subscribe_vendor_model_to_group(mesh_node_info_t *node);

void bind_next_model(mesh_node_info_t *node);

#endif /* MODEL_BINDING_H */
