#include "nvs_store.h"
#include "mesh_gateway.h"

#include "esp_log.h"

#define TAG "NVS"

struct gateway_state gw_state = {
    .net_idx = ESP_BLE_MESH_KEY_UNUSED,
    .app_idx = ESP_BLE_MESH_KEY_UNUSED,
    .addr = 0x0000,
    .vnd_bound_flag = 0,
};

nvs_handle_t NVS_HANDLE;
static const char *NVS_KEY = "mesh_gw";

void save_gw_state(void) {
  ble_mesh_nvs_store(NVS_HANDLE, NVS_KEY, &gw_state, sizeof(gw_state));
  ESP_LOGI(TAG, "Saved GW state: net=0x%04x, app=0x%04x, addr=0x%04x",
           gw_state.net_idx, gw_state.app_idx, gw_state.addr);
}

void restore_gw_state(void) {
  bool exist = false;
  esp_err_t err = ble_mesh_nvs_restore(NVS_HANDLE, NVS_KEY, &gw_state,
                                        sizeof(gw_state), &exist);
  if (err == ESP_OK && exist) {
    cached_net_idx = gw_state.net_idx;
    cached_app_idx = gw_state.app_idx;
    // Restore vendor bound flag; also infer from valid app_idx
    // (provisioner always binds vendor model after adding AppKey)
    if (gw_state.vnd_bound_flag || cached_app_idx != 0xFFFF) {
      vnd_bound = true;
    }
    ESP_LOGI(TAG, "Restored GW state: net=0x%04x, app=0x%04x, addr=0x%04x, vnd=%d",
             gw_state.net_idx, gw_state.app_idx, gw_state.addr, vnd_bound);
  } else {
    ESP_LOGW(TAG, "No saved GW state found - waiting for provisioning");
  }
}
