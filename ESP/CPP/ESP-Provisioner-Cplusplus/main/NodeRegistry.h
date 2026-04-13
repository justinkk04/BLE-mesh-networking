/* NodeRegistry.h
 *
 * Manages the in-memory list of provisioned BLE Mesh nodes.
 * Provides storage and lookup by unicast address.
 */

#pragma once

#include "MeshNode.h"
#include "esp_err.h"

class NodeRegistry {
public:
    static constexpr int MAX_NODES = 10;

    // Store a newly provisioned node (or update an existing one by UUID).
    // node_idx is the provisioner-assigned index used to build the name.
    esp_err_t storeNode(const uint8_t uuid[16], uint16_t unicast,
                        uint8_t elem_num, int node_idx);

    // Return a pointer to the node whose address range covers 'unicast',
    // or nullptr if not found.
    MeshNode* getNode(uint16_t unicast);

    // Current number of stored nodes.
    int count() const { return node_count_; }

private:
    MeshNode nodes_[MAX_NODES] = {};
    int      node_count_        = 0;
};
