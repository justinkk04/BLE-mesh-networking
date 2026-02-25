/* ESP BLE Mesh GATT Gateway
 *
 * Bridges Pi 5 (GATT client) to the BLE Mesh network.
 *
 * This node:
 * - Is provisioned by the mesh provisioner (UUID prefix 0xdd)
 * - Provides GATT service 0xDC01 for Pi 5 to connect
 * - Receives commands from Pi 5 via GATT writes
 * - Translates commands to mesh Generic OnOff messages
 * - Forwards mesh responses back to Pi 5 via GATT notifications
 *
 * Command format from Pi 5: "NODE_ID:COMMAND"
 * Examples: "1:RAMP", "2:STOP", "1:DUTY:50", "ALL:RAMP"
 */

#include <stdio.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "ble_mesh_example_init.h"
#include "ble_mesh_example_nvs.h"

#include "nvs_store.h"
#include "gatt_service.h"
#include "mesh_gateway.h"

#define TAG "MAIN"

void app_main(void) {
  esp_err_t err;

  printf("\n");
  printf("==========================================\n");
  printf("  ESP32-C6 BLE Mesh GATT Gateway\n");
  printf("  Bridges Pi 5 <-> Mesh Network\n");
  printf("==========================================\n\n");

  // Initialize NVS
  err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  // Open NVS
  err = ble_mesh_nvs_open(&NVS_HANDLE);
  if (err) {
    ESP_LOGE(TAG, "NVS open failed");
    return;
  }

  // Initialize Bluetooth (NimBLE stack)
  err = bluetooth_init();
  if (err) {
    ESP_LOGE(TAG, "Bluetooth init failed: %d", err);
    return;
  }

  // Get device UUID
  ble_mesh_get_dev_uuid(dev_uuid);

  // Register custom GATT services BEFORE mesh init (mesh locks GATT table)
  err = gatt_register_services();
  if (err) {
    ESP_LOGE(TAG, "GATT register failed");
    return;
  }

  // Initialize mesh (this locks the GATT table)
  err = mesh_init();
  if (err) {
    ESP_LOGE(TAG, "Mesh init failed");
    return;
  }

  // Start GATT advertising for Pi 5 connection
  gatt_start_advertising();

  ESP_LOGI(TAG, "============================================");
  ESP_LOGI(TAG, "  Gateway running");
  ESP_LOGI(TAG, "  - Mesh: waiting for provisioner");
  ESP_LOGI(TAG, "  - GATT: advertising 'Mesh-Gateway'");
  ESP_LOGI(TAG, "  Command format: NODE_ID:COMMAND");
  ESP_LOGI(TAG, "  Examples: 0:RAMP, 1:STOP, ALL:ON");
  ESP_LOGI(TAG, "============================================");
}
