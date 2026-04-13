/* NodeRegistry.cpp
 *
 * Implements storage and lookup of provisioned BLE Mesh nodes.
 */

#include "NodeRegistry.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"

static const char* TAG = "NODE_REG";

esp_err_t NodeRegistry::storeNode(const uint8_t uuid[16], uint16_t unicast,
                                   uint8_t elem_num, int node_idx) {
    if (node_count_ >= MAX_NODES) {
        ESP_LOGE(TAG, "Max nodes (%d) reached", MAX_NODES);
        return ESP_FAIL;
    }

    // Update existing node if the UUID already appears in the registry
    for (int i = 0; i < node_count_; i++) {
        if (memcmp(nodes_[i].uuid, uuid, 16) == 0) {
            nodes_[i].unicast  = unicast;
            nodes_[i].elem_num = elem_num;
            ESP_LOGW(TAG, "Node re-provisioned at 0x%04x", unicast);
            return ESP_OK;
        }
    }

    // Append new node
    memcpy(nodes_[node_count_].uuid, uuid, 16);
    nodes_[node_count_].unicast  = unicast;
    nodes_[node_count_].elem_num = elem_num;
    snprintf(nodes_[node_count_].name, sizeof(nodes_[node_count_].name),
             "NODE-%d", node_idx);
    node_count_++;

    ESP_LOGI(TAG, "Stored node %d: addr=0x%04x, elements=%d",
             node_count_, unicast, elem_num);
    return ESP_OK;
}

MeshNode* NodeRegistry::getNode(uint16_t unicast) {
    for (int i = 0; i < node_count_; i++) {
        // A node occupies addresses [unicast, unicast + elem_num)
        if (nodes_[i].unicast <= unicast &&
            nodes_[i].unicast + nodes_[i].elem_num > unicast) {
            return &nodes_[i];
        }
    }
    return nullptr;
}
