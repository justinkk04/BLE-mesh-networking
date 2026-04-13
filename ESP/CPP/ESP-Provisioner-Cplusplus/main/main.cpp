/* main.cpp
 *
 * ESP BLE Mesh Provisioner — C++ entry point.
 *
 * Auto-provisions devices whose UUID starts with 0xdddd,
 * distributes NetKey/AppKey, and binds vendor models to group 0xC000.
 */

#include "BLEMeshProvisioner.h"
#include "MeshConfig.h"

#include "ble_mesh_example_init.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char* TAG = "MAIN";

// Single provisioner instance with static storage duration
static BLEMeshProvisioner provisioner;

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Initializing BLE Mesh Provisioner...");

    // Initialise NVS (required by the BLE stack for key/state persistence)
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Initialise the Bluetooth controller and host
    err = bluetooth_init();
    if (err) {
        ESP_LOGE(TAG, "Bluetooth init failed: %d", err);
        return;
    }

    // Populate the provisioner's device UUID from the BT address
    ble_mesh_get_dev_uuid(MeshConfig::dev_uuid);

    // Start the mesh stack and enable the provisioner
    err = provisioner.init();
    if (err) {
        ESP_LOGE(TAG, "Mesh init failed: %d", err);
        return;
    }

    ESP_LOGI(TAG, "Provisioner running - waiting for mesh nodes...");
}
