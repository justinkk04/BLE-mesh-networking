/* Composition data parsing */

#include "esp_log.h"

#include "composition.h"

#define TAG "COMP"

// Parse composition data to detect which models a node has
// Format: CID(2) + PID(2) + VID(2) + CRPL(2) + Features(2) + [elements...]
// Each element: Loc(2) + NumS(1) + NumV(1) + [SIG Models(2 each)] + [Vendor(4
// each)]
void parse_composition_data(mesh_node_info_t *node,
                            struct net_buf_simple *comp) {
  if (!node || !comp || comp->len < 10) {
    ESP_LOGW(TAG, "Invalid composition data");
    return;
  }

  // Reset model flags
  node->has_onoff_srv = false;
  node->has_onoff_cli = false;
  node->has_vnd_srv = false;
  node->has_vnd_cli = false;
  node->srv_bound = false;
  node->cli_bound = false;
  node->vnd_srv_bound = false;
  node->vnd_cli_bound = false;

  // Skip header: CID(2) + PID(2) + VID(2) + CRPL(2) + Features(2) = 10 bytes
  uint8_t *data = comp->data;
  size_t len = comp->len;
  size_t offset = 10;

  ESP_LOGI(TAG, "Parsing comp data (%d bytes)", (int)len);

  // Parse elements
  while (offset + 4 <= len) {
    // Element: Loc(2) + NumS(1) + NumV(1)
    offset += 2; // Skip Loc
    if (offset + 2 > len)
      break;
    uint8_t num_s = data[offset++]; // Number of SIG models
    uint8_t num_v = data[offset++]; // Number of Vendor models

    // Parse SIG models (2 bytes each)
    for (int i = 0; i < num_s && offset + 2 <= len; i++) {
      uint16_t model_id = data[offset] | (data[offset + 1] << 8);
      offset += 2;

      if (model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV) {
        node->has_onoff_srv = true;
        ESP_LOGI(TAG, "  Found OnOff Server (0x%04x)", model_id);
      } else if (model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI) {
        node->has_onoff_cli = true;
        ESP_LOGI(TAG, "  Found OnOff Client (0x%04x)", model_id);
      }
    }

    // Parse Vendor models (4 bytes each: CID(2) + Model ID(2))
    for (int i = 0; i < num_v && offset + 4 <= len; i++) {
      uint16_t cid = data[offset] | (data[offset + 1] << 8);
      uint16_t vid = data[offset + 2] | (data[offset + 3] << 8);
      offset += 4;

      if (cid == CID_ESP && vid == VND_MODEL_ID_SERVER) {
        node->has_vnd_srv = true;
        ESP_LOGI(TAG, "  Found Vendor Server (CID:0x%04x, ID:0x%04x)", cid, vid);
      } else if (cid == CID_ESP && vid == VND_MODEL_ID_CLIENT) {
        node->has_vnd_cli = true;
        ESP_LOGI(TAG, "  Found Vendor Client (CID:0x%04x, ID:0x%04x)", cid, vid);
      }
    }
  }

  ESP_LOGI(TAG, "Node 0x%04x models: srv=%d, cli=%d, vnd_srv=%d, vnd_cli=%d",
           node->unicast, node->has_onoff_srv, node->has_onoff_cli,
           node->has_vnd_srv, node->has_vnd_cli);
}
