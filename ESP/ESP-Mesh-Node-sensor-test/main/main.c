/* ESP BLE Mesh Node - Direct I2C Sensor + PWM Load Control
 * v0.6.2: Modular split — see individual modules for implementation
 */

#include "esp_log.h"
#include "nvs_flash.h"
#include "ble_mesh_example_init.h"
#include "ble_mesh_example_nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sensor.h"
#include "load_control.h"
#include "mesh_node.h"
#include "nvs_store.h"
#include "command.h"

#define TAG "MAIN"

void app_main(void) {
  esp_err_t err;

  printf("\n");
  printf("========================================\n");
  printf("  ESP32-C6 BLE Mesh Node\n");
  printf("  Direct I2C Sensor + PWM Control\n");
  printf("========================================\n\n");

  // Initialize NVS
  err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  err = ble_mesh_nvs_open(&NVS_HANDLE);
  if (err) { ESP_LOGE(TAG, "NVS open failed"); return; }

  err = bluetooth_init();
  if (err) { ESP_LOGE(TAG, "Bluetooth init failed: %d", err); return; }

  ble_mesh_get_dev_uuid(dev_uuid);

  sensor_init();
  pwm_init();

  err = ble_mesh_init();
  if (err) { ESP_LOGE(TAG, "Mesh init failed"); return; }

  xTaskCreate(console_task, "console", 4096, NULL, 5, NULL);

  ESP_LOGI(TAG, "============================================");
  ESP_LOGI(TAG, "  Node running - direct I2C/PWM control");
  ESP_LOGI(TAG, "  INA260: %s", sensor_is_ready() ? "OK" : "NOT FOUND");
  ESP_LOGI(TAG, "  Console: type 'read', 'r', 's', 'duty:50'");
  ESP_LOGI(TAG, "============================================");
}
