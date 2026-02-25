#include "nvs_store.h"
#include "mesh_node.h"
#include "esp_log.h"

nvs_handle_t NVS_HANDLE;
static const char *NVS_KEY = "mesh_node";

void save_node_state(void) {
  ble_mesh_nvs_store(NVS_HANDLE, NVS_KEY, &node_state, sizeof(node_state));
}

void restore_node_state(void) {
  esp_err_t err;
  bool exist = false;

  err = ble_mesh_nvs_restore(NVS_HANDLE, NVS_KEY, &node_state,
                             sizeof(node_state), &exist);
  if (err == ESP_OK && exist) {
    ESP_LOGI("NVS", "Restored: net_idx=0x%04x, app_idx=0x%04x, addr=0x%04x",
             node_state.net_idx, node_state.app_idx, node_state.addr);
    cached_net_idx = node_state.net_idx;
    cached_app_idx = node_state.app_idx;
  }
}
