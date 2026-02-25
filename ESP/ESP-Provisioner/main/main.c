/* ESP BLE Mesh Provisioner
 *
 * Auto-provisions devices with UUID prefix 0xdd
 * Distributes NetKey/AppKey and binds models
 *
 * Based on ESP-IDF BLE Mesh provisioner example
 */

#include "esp_log.h"
#include "nvs_flash.h"

#include "ble_mesh_example_init.h"

#include "mesh_config.h"
#include "provisioning.h"

#define TAG "MAIN"

void app_main(void) {
  esp_err_t err;

  ESP_LOGI(TAG, "Initializing BLE Mesh Provisioner...");

  // Initialize NVS
  err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  // Initialize Bluetooth
  err = bluetooth_init();
  if (err) {
    ESP_LOGE(TAG, "Bluetooth init failed: %d", err);
    return;
  }

  // Get device UUID
  ble_mesh_get_dev_uuid(dev_uuid);

  // Initialize mesh
  err = ble_mesh_init();
  if (err) {
    ESP_LOGE(TAG, "Mesh init failed: %d", err);
    return;
  }

  ESP_LOGI(TAG, "Provisioner running - waiting for mesh nodes...");
}
