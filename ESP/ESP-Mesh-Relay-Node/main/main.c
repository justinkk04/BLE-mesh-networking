/* ESP BLE Mesh Relay Node — Silent Relay + LED Heartbeat
 *
 * This node:
 * - Is provisioned by the mesh provisioner (UUID prefix 0xdd)
 * - Relays mesh packets between nodes (relay enabled, TTL=7)
 * - Persists mesh credentials in NVS (auto-rejoins after power cycle)
 * - Blinks LED to indicate it is alive and part of the mesh
 * - Does NOT have sensor/PWM/vendor model — it is relay-only
 *
 * Wiring: LED+ -> ESP GPIO8 (via 330Ω resistor) -> LED- -> GND
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"

#include "ble_mesh_example_init.h"
#include "ble_mesh_example_nvs.h"

#define TAG "MESH_RELAY"

#define CID_ESP 0x02E5

// LED Configuration
#define LED_GPIO GPIO_NUM_8  // Change to match your wiring

// UUID with prefix 0xdd 0xdd for auto-provisioning
static uint8_t dev_uuid[16] = {0xdd, 0xdd};

// ============== Mesh State Storage ==============
static struct mesh_node_state {
  uint16_t net_idx;
  uint16_t app_idx;
  uint16_t addr;
  uint8_t onoff;
  uint8_t tid;
} __attribute__((packed)) node_state = {
    .net_idx = ESP_BLE_MESH_KEY_UNUSED,
    .app_idx = ESP_BLE_MESH_KEY_UNUSED,
    .addr = 0x0000,
    .onoff = 0,
    .tid = 0,
};

static nvs_handle_t NVS_HANDLE;
static const char *NVS_KEY = "mesh_relay";  // Different key from sensing node

static bool provisioned = false;  // Track provisioning state for LED pattern

// ============== NVS Storage ==============
static void save_node_state(void) {
  ble_mesh_nvs_store(NVS_HANDLE, NVS_KEY, &node_state, sizeof(node_state));
}

static void restore_node_state(void) {
  esp_err_t err;
  bool exist = false;

  err = ble_mesh_nvs_restore(NVS_HANDLE, NVS_KEY, &node_state,
                             sizeof(node_state), &exist);
  if (err == ESP_OK && exist) {
    ESP_LOGI(TAG, "Restored: net_idx=0x%04x, app_idx=0x%04x, addr=0x%04x",
             node_state.net_idx, node_state.app_idx, node_state.addr);
    provisioned = true;
  }
}

// ============== Mesh Models ==============
static esp_ble_mesh_cfg_srv_t config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(3, 20),
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(4, 20),
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
#else
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
    .default_ttl = 7,
};

// Only Config Server — no OnOff, no Vendor model
static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, ESP_BLE_MESH_MODEL_NONE),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
    .output_size = 0,
    .output_actions = 0,
};

// ============== LED Heartbeat ==============
static void led_init(void) {
  gpio_reset_pin(LED_GPIO);
  gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_level(LED_GPIO, 0);
  ESP_LOGI(TAG, "LED initialized on GPIO%d", LED_GPIO);
}

static void led_heartbeat_task(void *pvParameters) {
  while (1) {
    if (provisioned) {
      // Provisioned: slow steady blink (1s on, 1s off)
      gpio_set_level(LED_GPIO, 1);
      vTaskDelay(pdMS_TO_TICKS(1000));
      gpio_set_level(LED_GPIO, 0);
      vTaskDelay(pdMS_TO_TICKS(1000));
    } else {
      // Not provisioned: fast blink (200ms on, 200ms off)
      gpio_set_level(LED_GPIO, 1);
      vTaskDelay(pdMS_TO_TICKS(200));
      gpio_set_level(LED_GPIO, 0);
      vTaskDelay(pdMS_TO_TICKS(200));
    }
  }
}

// ============== Mesh Callbacks ==============
static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags,
                          uint32_t iv_index) {
  ESP_LOGI(TAG, "========== RELAY PROVISIONED ==========");
  ESP_LOGI(TAG, "NetKey index: 0x%04x", net_idx);
  ESP_LOGI(TAG, "Unicast addr: 0x%04x", addr);
  ESP_LOGI(TAG, "Flags: 0x%02x, IV: 0x%08" PRIx32, flags, iv_index);
  ESP_LOGI(TAG, "=======================================");

  node_state.net_idx = net_idx;
  node_state.addr = addr;
  provisioned = true;
  save_node_state();
}

static void provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                            esp_ble_mesh_prov_cb_param_t *param) {
  switch (event) {
  case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
    ESP_LOGI(TAG, "Mesh stack registered");
    restore_node_state();
    break;

  case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
    ESP_LOGI(TAG, "Provisioning enabled — waiting for provisioner");
    break;

  case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
    ESP_LOGI(TAG, "Provisioning link opened (%s)",
             param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV
                 ? "PB-ADV"
                 : "PB-GATT");
    break;

  case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
    ESP_LOGI(TAG, "Provisioning link closed");
    break;

  case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
    prov_complete(
        param->node_prov_complete.net_idx, param->node_prov_complete.addr,
        param->node_prov_complete.flags, param->node_prov_complete.iv_index);
    break;

  case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
    ESP_LOGW(TAG, "Node reset — reprovisioning needed");
    provisioned = false;
    break;

  default:
    break;
  }
}

static void config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                             esp_ble_mesh_cfg_server_cb_param_t *param) {
  if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
    switch (param->ctx.recv_op) {
    case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
      ESP_LOGI(TAG, "AppKey added: net=0x%04x, app=0x%04x",
               param->value.state_change.appkey_add.net_idx,
               param->value.state_change.appkey_add.app_idx);
      node_state.net_idx = param->value.state_change.appkey_add.net_idx;
      node_state.app_idx = param->value.state_change.appkey_add.app_idx;
      save_node_state();
      break;

    case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
      ESP_LOGI(TAG, "Model bound: elem=0x%04x, app=0x%04x, model=0x%04x",
               param->value.state_change.mod_app_bind.element_addr,
               param->value.state_change.mod_app_bind.app_idx,
               param->value.state_change.mod_app_bind.model_id);
      save_node_state();
      ESP_LOGI(TAG, "Relay node fully configured!");
      break;

    default:
      break;
    }
  }
}

// ============== Mesh Initialization ==============
static esp_err_t ble_mesh_init(void) {
  esp_err_t err;

  esp_ble_mesh_register_prov_callback(provisioning_cb);
  esp_ble_mesh_register_config_server_callback(config_server_cb);

  err = esp_ble_mesh_init(&provision, &composition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Mesh init failed: %d", err);
    return err;
  }

  err = esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV |
                                      ESP_BLE_MESH_PROV_GATT);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Enable provisioning failed: %d", err);
    return err;
  }

  ESP_LOGI(TAG, "============================================");
  ESP_LOGI(TAG, "  BLE Mesh Relay Node Ready");
  ESP_LOGI(TAG, "  UUID: %s", bt_hex(dev_uuid, 16));
  ESP_LOGI(TAG, "  Waiting for provisioner...");
  ESP_LOGI(TAG, "============================================");

  return ESP_OK;
}

// ============== Main ==============
void app_main(void) {
  esp_err_t err;

  printf("\n");
  printf("========================================\n");
  printf("  ESP32-C6 BLE Mesh Relay Node\n");
  printf("  Silent Relay + LED Heartbeat\n");
  printf("========================================\n\n");

  // Initialize NVS
  err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  // Open NVS namespace
  err = ble_mesh_nvs_open(&NVS_HANDLE);
  if (err) {
    ESP_LOGE(TAG, "NVS open failed");
    return;
  }

  // Initialize Bluetooth
  err = bluetooth_init();
  if (err) {
    ESP_LOGE(TAG, "Bluetooth init failed: %d", err);
    return;
  }

  // Get device UUID (keeps 0xdd prefix)
  ble_mesh_get_dev_uuid(dev_uuid);

  // Initialize LED
  led_init();

  // Initialize mesh
  err = ble_mesh_init();
  if (err) {
    ESP_LOGE(TAG, "Mesh init failed");
    return;
  }

  // Start LED heartbeat task
  xTaskCreate(led_heartbeat_task, "led_hb", 2048, NULL, 5, NULL);

  ESP_LOGI(TAG, "============================================");
  ESP_LOGI(TAG, "  Relay node running");
  ESP_LOGI(TAG, "  LED: GPIO%d (fast=unprovisioned, slow=active)", LED_GPIO);
  ESP_LOGI(TAG, "  Relay: enabled, TTL=7");
  ESP_LOGI(TAG, "============================================");
}
