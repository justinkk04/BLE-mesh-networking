/* CompositionParser.h
 *
 * Parses the raw composition data buffer returned by a provisioned node
 * and populates the model-presence flags on a MeshNode.
 *
 * Composition data binary layout (BLE Mesh spec):
 *   Header (10 bytes): CID(2) + PID(2) + VID(2) + CRPL(2) + Features(2)
 *   Per element:       Loc(2) + NumSigModels(1) + NumVendorModels(1)
 *     SIG model:       ModelID(2)
 *     Vendor model:    CID(2) + ModelID(2)
 */

#pragma once

#include "MeshNode.h"

// Forward-declare the ESP-IDF net_buf_simple to avoid pulling in all BLE headers
struct net_buf_simple;

class CompositionParser {
public:
    // Parse 'comp' and set the has_* flags on 'node'.
    // Also resets all bound/subscribed flags so re-provisioning starts clean.
    static void parse(MeshNode* node, struct net_buf_simple* comp);
};
