/* MeshNode.h
 *
 * Data structure representing a provisioned BLE Mesh node.
 * Tracks the node's identity, element count, discovered models,
 * and the configuration state of each model binding.
 */

#pragma once

#include <cstdint>

struct MeshNode {
    uint8_t  uuid[16];           // Device's 16-byte UUID
    uint16_t unicast;            // Assigned unicast address
    uint8_t  elem_num;           // Number of elements on this node
    uint8_t  onoff;              // Current on/off state (for tracking)
    char     name[16];           // Human-readable label, e.g. "NODE-1"

    // Model presence flags (set by CompositionParser)
    bool     has_onoff_srv;      // Node exposes a Generic OnOff Server
    bool     has_onoff_cli;      // Node exposes a Generic OnOff Client
    bool     has_vnd_srv;        // Node exposes a Vendor Server
    bool     has_vnd_cli;        // Node exposes a Vendor Client

    // AppKey binding state (set by ModelBinder as each bind completes)
    bool     srv_bound;          // OnOff Server bound to AppKey
    bool     cli_bound;          // OnOff Client bound to AppKey
    bool     vnd_srv_bound;      // Vendor Server bound to AppKey
    bool     vnd_cli_bound;      // Vendor Client bound to AppKey

    // Group subscription state
    bool     vnd_srv_subscribed; // Vendor Server subscribed to MESH_GROUP_ADDR
};
