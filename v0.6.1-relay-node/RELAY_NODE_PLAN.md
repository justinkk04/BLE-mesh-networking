# Relay-Only Mesh Node Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Create a minimal relay-only BLE Mesh node firmware that extends network range and adds path redundancy — no sensors, no vendor model, just mesh relay + LED heartbeat.

**Architecture:** Fork `ESP-Mesh-Node-sensor-test` into `ESP-Mesh-Relay-Node`. Strip all I2C/PWM/vendor model code. Keep mesh provisioning, NVS persistence (auto-rejoin after power cycle), and relay config. Add an LED heartbeat blink to indicate the node is alive and relaying.

**Tech Stack:** ESP-IDF, ESP BLE Mesh (NimBLE), FreeRTOS, GPIO (LED)

---

## Project Context

### What This Project Is

A BLE Mesh network for DC power monitoring. The system has:

| Component | Directory | Role |
| --- | --- | --- |
| Provisioner | `ESP/ESP-Provisioner` | Auto-provisions all mesh nodes (UUID prefix `0xdd`) |
| GATT Gateway | `ESP/ESP_GATT_BLE_Gateway` | Pi 5 ↔ Mesh bridge |
| Mesh Sensing Node(s) | `ESP/ESP-Mesh-Node-sensor-test` | Reads INA260 via I2C, controls PWM load, responds to vendor commands |
| **Relay Node (NEW)** | **`ESP/ESP-Mesh-Relay-Node`** | **Silent relay — extends range, adds redundancy** |
| Pi 5 Gateway | `gateway-pi5/gateway.py` | Python TUI with PowerManager |

### Current State of ESP-Mesh-Relay-Node

The directory `ESP/ESP-Mesh-Relay-Node` already exists. It is a **direct copy** of `ESP-Mesh-Node-sensor-test` — the `main.c` is identical (781 lines of sensing code). The `sdkconfig.defaults`, `CMakeLists.txt`, and build directory are all from the sensing node.

### What We Want

A stripped-down firmware (~200 lines) that:

1. Gets provisioned by the existing Provisioner (UUID prefix `0xdd`)
2. Relays mesh packets (relay enabled, TTL=7)
3. Persists mesh credentials in NVS (auto-rejoins mesh after power cycle)
4. Blinks an LED to show it's alive
5. Does **NOT** have a vendor model, I2C, PWM, or command processing
6. Does **NOT** respond to READ/DUTY/RAMP commands (the Pi 5 gateway naturally ignores it during discovery)

### How the Pi 5 Gateway Handles Relay Nodes

The gateway's `PowerManager._bootstrap_discovery()` probes mesh addresses with READ commands. Relay nodes have no vendor model, so they won't respond. The gateway logs `"Node X no response"` and moves on. The only side effect: `sensing_node_count` (from BLE scan) will be inflated by 1, causing one extra failed probe. This is harmless and handled gracefully.

---

## Tasks

### Task 1: Update CMakeLists.txt Project Name

**Files:**

- Modify: `ESP/ESP-Mesh-Relay-Node/CMakeLists.txt`

**Step 1: Change project name**

```cmake
# Line 8 — change from sensing node name to relay node name
project(relay-node-ble-mesh)
```

**Step 2: Verify**

Open the file and confirm line 8 reads `project(relay-node-ble-mesh)`.

**Step 3: Commit**

```bash
git add ESP/ESP-Mesh-Relay-Node/CMakeLists.txt
git commit -m "feat(relay): rename project to relay-node-ble-mesh"
```

---

### Task 2: Strip main.c Down to Relay-Only Firmware

**Files:**

- Modify: `ESP/ESP-Mesh-Relay-Node/main/main.c`

This is the main task. Replace the entire 781-line sensing node `main.c` with a ~200-line relay-only version.

**Step 1: Write the new main.c**

The new file keeps ONLY:

- Mesh includes + BLE Mesh init
- Config Server with relay enabled (TTL=7)
- NVS persistence (save/restore mesh state)
- Provisioning callback (prov_complete, restore_node_state)
- Config server callback (AppKey add, model bind)
- LED heartbeat FreeRTOS task
- Generic OnOff server (for LED control — ON/OFF toggles LED)
- `app_main()` with NVS init, BT init, UUID, mesh init, LED task

**Remove entirely:**

- All I2C defines/functions (`I2C_PORT`, `INA260_*`, `i2c_scan()`, `sensor_init()`, `ina260_read_voltage()`, `ina260_read_current()`)
- All PWM defines/functions (`PWM_GPIO`, `pwm_init()`, `set_duty()`)
- Sensor response formatting (`format_sensor_response()`)
- Command processing (`process_command()`)
- Vendor model definition (`VND_MODEL_ID_SERVER`, `VND_OP_SEND`, `VND_OP_STATUS`, `vnd_op[]`, `vnd_models[]`)
- Vendor model callback (`custom_model_cb()`)
- Console task (`console_task()`)
- `#include "driver/i2c.h"` and `#include "driver/ledc.h"` and `#include <math.h>`

Here is the complete replacement `main.c`:

```c
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
```

**Step 2: Verify the file compiles mentally**

Check that:

- No I2C/PWM/vendor includes remain
- No references to `ina260_*`, `set_duty`, `process_command`, `custom_model_cb`
- `elements[]` uses `ESP_BLE_MESH_MODEL_NONE` for vendor models (no vendor model array)
- `ble_mesh_init()` only registers provisioning and config server callbacks (no generic server/client/custom model)
- LED GPIO is set to `GPIO_NUM_8` (user can change this)

**Step 3: Commit**

```bash
git add ESP/ESP-Mesh-Relay-Node/main/main.c
git commit -m "feat(relay): strip to relay-only firmware with LED heartbeat"
```

---

### Task 3: Clean Up sdkconfig.defaults

**Files:**

- Modify: `ESP/ESP-Mesh-Relay-Node/sdkconfig.defaults`

**Step 1: Remove OnOff Client config (relay doesn't need it)**

The relay node doesn't use Generic OnOff Client, so remove `CONFIG_BLE_MESH_GENERIC_ONOFF_CLI=y`. The rest of the config is correct — relay enabled, GATT proxy, mesh settings for NVS persistence.

Final `sdkconfig.defaults`:

```ini
# Override some defaults so BT stack is enabled
CONFIG_BT_ENABLED=y
CONFIG_BT_BTU_TASK_STACK_SIZE=4512
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y

# ESP BLE Mesh - Relay node
CONFIG_BLE_MESH=y
CONFIG_BLE_MESH_NODE=y
CONFIG_BLE_MESH_RELAY=y
CONFIG_BLE_MESH_PB_GATT=y
CONFIG_BLE_MESH_GATT_PROXY_SERVER=y
CONFIG_BLE_MESH_TX_SEG_MSG_COUNT=10
CONFIG_BLE_MESH_RX_SEG_MSG_COUNT=10

# Persistent storage for mesh credentials across power cycles
CONFIG_BLE_MESH_SETTINGS=y
```

**Step 2: Delete stale sdkconfig and build files**

The existing `sdkconfig` and `build/` were generated from the sensing node's settings. They must be regenerated.

```bash
cd ESP/ESP-Mesh-Relay-Node
del sdkconfig
del sdkconfig.old
rmdir /s /q build
```

**Step 3: Commit**

```bash
git add ESP/ESP-Mesh-Relay-Node/sdkconfig.defaults
git commit -m "feat(relay): clean up sdkconfig.defaults for relay-only node"
```

---

### Task 4: Build and Flash

**Step 1: Set target and build**

```bash
cd ESP/ESP-Mesh-Relay-Node
idf.py set-target esp32c6
idf.py build
```

Expected: Build succeeds with no errors.

**Step 2: Flash**

```bash
idf.py -p COMxx erase-flash
idf.py -p COMxx flash monitor
```

Replace `COMxx` with the relay node's serial port.

Expected output:

```
========================================
  ESP32-C6 BLE Mesh Relay Node
  Silent Relay + LED Heartbeat
========================================

MESH_RELAY: LED initialized on GPIO8
MESH_RELAY: BLE Mesh Relay Node Ready
MESH_RELAY: UUID: dd:dd:xx:xx:...
MESH_RELAY: Waiting for provisioner...
```

LED should blink fast (200ms) indicating it's unprovisioned.

**Step 3: Commit build success**

```bash
git commit --allow-empty -m "chore(relay): verified build and flash"
```

---

### Task 5: Provision and Verify Relay

**Step 1: Power on the Provisioner**

The existing Provisioner auto-discovers devices with UUID prefix `0xdd`. The relay node will be discovered and provisioned. The Provisioner will:

1. Provision the relay node (assign unicast address)
2. Add AppKey
3. Try to bind models — only Config Server exists, so binding completes quickly
4. Log `"FULLY CONFIGURED"`

Expected on relay node serial:

```
MESH_RELAY: Provisioning link opened (PB-ADV)
MESH_RELAY: ========== RELAY PROVISIONED ==========
MESH_RELAY: Unicast addr: 0x00XX
MESH_RELAY: ========================================
MESH_RELAY: AppKey added
MESH_RELAY: Relay node fully configured!
```

LED should switch to slow blink (1s) indicating it's provisioned and active.

**Step 2: Verify relay behavior**

With the relay provisioned, place it between the GATT Gateway and a sensing node. Send a command from the Pi 5:

```
read
```

- The READ command should reach the sensing node through the relay.
- The relay node's serial monitor should show NO output for the vendor command (it doesn't have the vendor model).
- The sensing node should respond normally.

**Step 3: Verify power cycle recovery**

1. Power off the relay node
2. Wait 5 seconds
3. Power it back on

Expected: The relay auto-rejoins the mesh (NVS-stored credentials). LED goes directly to slow blink. No reprovisioning needed.

---

### Task 6: Verify Pi 5 Gateway Handles Relay Gracefully

**Step 1: Start the Pi 5 gateway**

```bash
python gateway.py
```

**Step 2: Enable Power Manager**

```
threshold 10000
```

The PowerManager will run `_bootstrap_discovery()`. It will:

1. Count mesh devices from BLE scan (includes relay node)
2. Probe each address with READ
3. Relay node won't respond → logged as "Node X no response"
4. Sensing nodes respond → added to PM

Expected log output (example with 2 sensing nodes + 1 relay):

```
[POWER] Probing 3 sensing node(s)...
[POWER] Found node 1
[POWER] Found node 2
[POWER] Node 3 no response
[POWER] Discovery complete: 2 node(s)
```

This is correct behavior — the relay is silently forwarding packets but is never treated as a sensing node.

---

## LED Behavior Summary

| State | LED Pattern |
| --- | --- |
| Unprovisioned | Fast blink (200ms on/off) |
| Provisioned & Active | Slow blink (1s on/off) |
| Power cycled | Slow blink resumes (NVS credentials restored) |

## Important Notes

- **LED GPIO:** Default is `GPIO_NUM_8`. Change `#define LED_GPIO` in `main.c` to match your wiring.
- **No erase needed on sensing nodes:** The relay node is a separate device. Existing sensing nodes and gateway are unaffected.
- **Provisioner handles it automatically:** The Provisioner's `bind_next_model()` chain will just skip vendor model binding since the relay has no vendor model. It should still reach `"FULLY CONFIGURED"`.
