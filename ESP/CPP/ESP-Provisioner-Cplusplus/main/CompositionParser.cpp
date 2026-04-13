/* CompositionParser.cpp
 *
 * Parses raw BLE Mesh composition data and populates MeshNode model flags.
 */

#include "CompositionParser.h"
#include "MeshConstants.h"

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_log.h"

static const char* TAG = "COMP";

void CompositionParser::parse(MeshNode* node, struct net_buf_simple* comp) {
    if (!node || !comp || comp->len < 10) {
        ESP_LOGW(TAG, "Invalid composition data");
        return;
    }

    // Reset all model presence and binding flags before re-parsing
    node->has_onoff_srv  = false;
    node->has_onoff_cli  = false;
    node->has_vnd_srv    = false;
    node->has_vnd_cli    = false;
    node->srv_bound      = false;
    node->cli_bound      = false;
    node->vnd_srv_bound  = false;
    node->vnd_cli_bound  = false;

    const uint8_t* data   = comp->data;
    const size_t   len    = comp->len;
    size_t         offset = 10; // skip CID+PID+VID+CRPL+Features

    ESP_LOGI(TAG, "Parsing composition data (%d bytes)", static_cast<int>(len));

    // Iterate over elements
    while (offset + 4 <= len) {
        offset += 2; // skip Loc field
        if (offset + 2 > len) break;

        const uint8_t num_s = data[offset++]; // SIG model count
        const uint8_t num_v = data[offset++]; // Vendor model count

        // Parse SIG models (2 bytes each)
        for (int i = 0; i < num_s && offset + 2 <= len; i++) {
            const uint16_t model_id =
                static_cast<uint16_t>(data[offset]) |
                static_cast<uint16_t>(data[offset + 1] << 8);
            offset += 2;

            if (model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV) {
                node->has_onoff_srv = true;
                ESP_LOGI(TAG, "  Found OnOff Server (0x%04x)", model_id);
            } else if (model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI) {
                node->has_onoff_cli = true;
                ESP_LOGI(TAG, "  Found OnOff Client (0x%04x)", model_id);
            }
        }

        // Parse Vendor models (4 bytes each: CID(2) + ModelID(2))
        for (int i = 0; i < num_v && offset + 4 <= len; i++) {
            const uint16_t cid =
                static_cast<uint16_t>(data[offset]) |
                static_cast<uint16_t>(data[offset + 1] << 8);
            const uint16_t vid =
                static_cast<uint16_t>(data[offset + 2]) |
                static_cast<uint16_t>(data[offset + 3] << 8);
            offset += 4;

            if (cid == CID_ESP && vid == VND_MODEL_ID_SERVER) {
                node->has_vnd_srv = true;
                ESP_LOGI(TAG, "  Found Vendor Server (CID:0x%04x, ID:0x%04x)",
                         cid, vid);
            } else if (cid == CID_ESP && vid == VND_MODEL_ID_CLIENT) {
                node->has_vnd_cli = true;
                ESP_LOGI(TAG, "  Found Vendor Client (CID:0x%04x, ID:0x%04x)",
                         cid, vid);
            }
        }
    }

    ESP_LOGI(TAG,
             "Node 0x%04x models: srv=%d, cli=%d, vnd_srv=%d, vnd_cli=%d",
             node->unicast,
             node->has_onoff_srv, node->has_onoff_cli,
             node->has_vnd_srv,   node->has_vnd_cli);
}
