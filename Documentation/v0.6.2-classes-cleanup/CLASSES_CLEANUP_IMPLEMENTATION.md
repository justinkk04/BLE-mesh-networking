# v0.6.2 — Modular Code Cleanup Implementation Guide

**Date:** February 25, 2026
**Author:** Justin Kwarteng
**Purpose:** Feed this document to an agent to split ESP firmware into clean modules.

---

## 1. System Summary

### Current State

All four ESP32-C6 firmware projects use a single monolithic `main.c`. Each file mixes unrelated responsibilities: I2C drivers, PWM control, mesh callbacks, command parsing, NVS storage, serial console — all in one file.

### Goal

Split each `main.c` into focused `.c`/`.h` module pairs. After the split:

- Each file has one clear responsibility
- `main.c` becomes a tiny orchestrator (~30 lines)
- Headers declare public interfaces with include guards
- `static` functions that become cross-module are made non-static with header declarations
- CMakeLists.txt lists all new source files

### Rules

1. **Zero behavior changes** — every function does exactly what it did before
2. **Cut and paste only** — move code between files, don't rewrite it
3. **Remove `static` keyword** from functions that need to be called cross-module
4. **Add `extern` declarations** in headers for globals shared between modules
5. **Every header uses include guards**: `#ifndef MODULE_H` / `#define MODULE_H` / `#endif`
6. **Build after each batch** — `idf.py build` must pass before the next batch

---

## 2. Batch 1 — Sensor Node (`ESP/ESP-Mesh-Node-sensor-test/`)

### Current File: `main/main.c` (781 lines)

**Function inventory with line ranges:**

| Function | Lines | Module Target |
|---|---|---|
| `i2c_scan()` | 182-202 | `sensor.c` |
| `sensor_init()` | 204-254 | `sensor.c` |
| `ina260_read_voltage()` | 256-281 | `sensor.c` |
| `ina260_read_current()` | 283-308 | `sensor.c` |
| `pwm_init()` | 310-334 | `load_control.c` |
| `set_duty()` | 336-349 | `load_control.c` |
| `format_sensor_response()` | 351-359 | `command.c` |
| `process_command()` | 361-406 | `command.c` |
| `console_task()` | 649-689 | `command.c` |
| `prov_complete()` | 408-420 | `mesh_node.c` |
| `provisioning_cb()` | 422-459 | `mesh_node.c` |
| `config_server_cb()` | 461-506 | `mesh_node.c` |
| `generic_server_cb()` | 508-562 | `mesh_node.c` |
| `generic_client_cb()` | 564-592 | `mesh_node.c` |
| `custom_model_cb()` | 594-647 | `mesh_node.c` |
| `ble_mesh_init()` | 691-721 | `mesh_node.c` |
| `save_node_state()` | 162-165 | `nvs_store.c` |
| `restore_node_state()` | 167-179 | `nvs_store.c` |
| `app_main()` | 723-780 | `main.c` (stays) |

**Global variables to route:**

| Variable | Lines | Accessed By | Routing |
|---|---|---|---|
| `ina260_ok` | 74 | `sensor.c` only | Keep static in `sensor.c` |
| `current_duty` | 73 | `load_control.c`, `command.c` | `extern` in `load_control.h` |
| `node_state` struct | 77-89 | `nvs_store.c`, `mesh_node.c`, `command.c` (via `current_duty`) | `extern` in `mesh_node.h` |
| `cached_net_idx/app_idx` | 95-96 | `mesh_node.c` | Keep in `mesh_node.c` |
| `NVS_HANDLE` | 91 | `nvs_store.c`, `mesh_node.c` (for init) | `extern` in `nvs_store.h` |
| `NVS_KEY` | 92 | `nvs_store.c` only | Keep static in `nvs_store.c` |
| `dev_uuid` | 52 | `mesh_node.c`, `main.c` | `extern` in `mesh_node.h` |
| Mesh model arrays | 98-160 | `mesh_node.c` only | Keep static in `mesh_node.c` |

---

### Task 1: Create `sensor.h` and `sensor.c`

**Files:**

- Create: `ESP/ESP-Mesh-Node-sensor-test/main/sensor.h`
- Create: `ESP/ESP-Mesh-Node-sensor-test/main/sensor.c`

**Step 1: Create `sensor.h`**

```c
#ifndef SENSOR_H
#define SENSOR_H

#include "esp_err.h"

// Initialize I2C bus and configure INA260 sensor.
// Returns ESP_OK on success. Sets internal ina260_ok flag.
esp_err_t sensor_init(void);

// Read INA260 bus voltage in volts. Returns 0.0 if sensor not found.
float ina260_read_voltage(void);

// Read INA260 current in milliamps. Returns 0.0 if sensor not found.
float ina260_read_current(void);

// Scan I2C bus and log all found devices. Diagnostic only.
void i2c_scan(void);

// Returns true if INA260 was detected during sensor_init().
bool sensor_is_ready(void);

#endif // SENSOR_H
```

**Step 2: Create `sensor.c`**

Move from `main.c` lines 54-308 into `sensor.c`:

- All `#define` constants for I2C/INA260 (lines 54-65)
- `static bool ina260_ok` (line 74)
- `i2c_scan()` (lines 182-202) — remove `static`
- `sensor_init()` (lines 204-254) — remove `static`
- `ina260_read_voltage()` (lines 256-281) — remove `static`
- `ina260_read_current()` (lines 283-308) — remove `static`

Add at top of `sensor.c`:

```c
#include "sensor.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "SENSOR";
```

Add the accessor function:

```c
bool sensor_is_ready(void) {
    return ina260_ok;
}
```

---

### Task 2: Create `load_control.h` and `load_control.c`

**Files:**

- Create: `ESP/ESP-Mesh-Node-sensor-test/main/load_control.h`
- Create: `ESP/ESP-Mesh-Node-sensor-test/main/load_control.c`

**Step 1: Create `load_control.h`**

```c
#ifndef LOAD_CONTROL_H
#define LOAD_CONTROL_H

// Initialize LEDC PWM for load control. Starts with load OFF (0%).
void pwm_init(void);

// Set load duty cycle (0-100%). Clamped to valid range.
void set_duty(int percent);

// Get current duty cycle percentage (0-100%).
int get_current_duty(void);

#endif // LOAD_CONTROL_H
```

**Step 2: Create `load_control.c`**

Move from `main.c` lines 67-349 (PWM section):

- All `#define` constants for PWM (lines 67-71)
- `static int current_duty` (line 73)
- `pwm_init()` (lines 310-334) — remove `static`
- `set_duty()` (lines 336-349) — remove `static`

Add at top:

```c
#include "load_control.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "LOAD_CTRL";
```

Add accessor:

```c
int get_current_duty(void) {
    return current_duty;
}
```

---

### Task 3: Create `nvs_store.h` and `nvs_store.c`

**Files:**

- Create: `ESP/ESP-Mesh-Node-sensor-test/main/nvs_store.h`
- Create: `ESP/ESP-Mesh-Node-sensor-test/main/nvs_store.c`

**Step 1: Create `nvs_store.h`**

```c
#ifndef NVS_STORE_H
#define NVS_STORE_H

#include "nvs_flash.h"
#include "ble_mesh_example_nvs.h"

// NVS handle — opened in app_main(), used by save/restore functions
extern nvs_handle_t NVS_HANDLE;

// Save current mesh node state to NVS
void save_node_state(void);

// Restore mesh node state from NVS. Updates cached_net_idx/app_idx.
void restore_node_state(void);

#endif // NVS_STORE_H
```

**Step 2: Create `nvs_store.c`**

Move from `main.c`:

- `NVS_HANDLE` (line 91) — remove `static`
- `NVS_KEY` (line 92) — keep `static`
- `save_node_state()` (lines 162-165) — remove `static`
- `restore_node_state()` (lines 167-179) — remove `static`

Note: `restore_node_state()` references `node_state`, `cached_net_idx`, `cached_app_idx` which live in `mesh_node.c`. Use `#include "mesh_node.h"` and expose those via extern.

```c
#include "nvs_store.h"
#include "mesh_node.h"
#include "esp_log.h"

nvs_handle_t NVS_HANDLE;
static const char *NVS_KEY = "mesh_node";

void save_node_state(void) {
    ble_mesh_nvs_store(NVS_HANDLE, NVS_KEY, &node_state, sizeof(node_state));
}

void restore_node_state(void) {
    bool exist = false;
    esp_err_t err = ble_mesh_nvs_restore(NVS_HANDLE, NVS_KEY, &node_state,
                                         sizeof(node_state), &exist);
    if (err == ESP_OK && exist) {
        ESP_LOGI("NVS", "Restored: net_idx=0x%04x, app_idx=0x%04x, addr=0x%04x",
                 node_state.net_idx, node_state.app_idx, node_state.addr);
        cached_net_idx = node_state.net_idx;
        cached_app_idx = node_state.app_idx;
    }
}
```

---

### Task 4: Create `command.h` and `command.c`

**Files:**

- Create: `ESP/ESP-Mesh-Node-sensor-test/main/command.h`
- Create: `ESP/ESP-Mesh-Node-sensor-test/main/command.c`

**Step 1: Create `command.h`**

```c
#ifndef COMMAND_H
#define COMMAND_H

#include <stddef.h>

// Format sensor data as "D:50%,V:12.003V,I:250.00mA,P:3000.8mW"
int format_sensor_response(char *buf, size_t buf_size);

// Process a text command (read, duty:50, r, s, etc.)
// Writes response to buf, returns response length.
int process_command(const char *cmd, char *response, size_t resp_size);

// FreeRTOS task: serial console for local testing via idf.py monitor
void console_task(void *pvParameters);

#endif // COMMAND_H
```

**Step 2: Create `command.c`**

Move from `main.c`:

- `format_sensor_response()` (lines 351-359) — remove `static`
- `process_command()` (lines 361-406) — remove `static`
- `console_task()` (lines 649-689) — remove `static`

Add at top:

```c
#include "command.h"
#include "sensor.h"
#include "load_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *TAG = "CMD";
```

Note: `format_sensor_response()` calls `ina260_read_voltage()`, `ina260_read_current()`, and reads `current_duty`. After the split:

- `ina260_read_voltage/current` → available via `#include "sensor.h"`
- `current_duty` → use `get_current_duty()` from `#include "load_control.h"`

Update `format_sensor_response()`:

```c
int format_sensor_response(char *buf, size_t buf_size) {
    float voltage = ina260_read_voltage();
    float current = ina260_read_current();
    float power = fabsf(voltage * current);
    return snprintf(buf, buf_size, "D:%d%%,V:%.3fV,I:%.2fmA,P:%.1fmW",
                    get_current_duty(), voltage, current, power);
}
```

And `console_task()` uses `i2c_scan()` from `sensor.h`.

---

### Task 5: Create `mesh_node.h` and `mesh_node.c`

**Files:**

- Create: `ESP/ESP-Mesh-Node-sensor-test/main/mesh_node.h`
- Create: `ESP/ESP-Mesh-Node-sensor-test/main/mesh_node.c`

**Step 1: Create `mesh_node.h`**

```c
#ifndef MESH_NODE_H
#define MESH_NODE_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_ble_mesh_defs.h"

// Mesh node state — persisted to NVS
struct mesh_node_state {
    uint16_t net_idx;
    uint16_t app_idx;
    uint16_t addr;
    uint8_t onoff;
    uint8_t tid;
} __attribute__((packed));

// Global mesh state (extern for nvs_store.c and command.c access)
extern struct mesh_node_state node_state;
extern uint16_t cached_net_idx;
extern uint16_t cached_app_idx;
extern uint8_t dev_uuid[16];

// Initialize BLE Mesh stack, register callbacks, enable provisioning
esp_err_t ble_mesh_init(void);

#endif // MESH_NODE_H
```

**Step 2: Create `mesh_node.c`**

Move from `main.c`:

- `dev_uuid[]` (line 52) — remove `static`
- `node_state` struct definition and instance (lines 77-89) — remove `static`
- `cached_net_idx/app_idx` (lines 95-96) — remove `static`
- All mesh model definitions (lines 98-160) — keep `static`
- `prov_complete()` (lines 408-420)
- `provisioning_cb()` (lines 422-459)
- `config_server_cb()` (lines 461-506)
- `generic_server_cb()` (lines 508-562)
- `generic_client_cb()` (lines 564-592)
- `custom_model_cb()` (lines 594-647)
- `ble_mesh_init()` (lines 691-721)

Add at top:

```c
#include "mesh_node.h"
#include "nvs_store.h"
#include "command.h"
#include "load_control.h"
#include "esp_log.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "ble_mesh_example_init.h"

static const char *TAG = "MESH_NODE";
```

Note: `custom_model_cb()` calls `process_command()` from `command.h`. `generic_server_cb()` calls `set_duty()` from `load_control.h` and `format_sensor_response()` from `command.h`. `provisioning_cb()` calls `restore_node_state()` from `nvs_store.h`. `config_server_cb()` calls `save_node_state()` from `nvs_store.h`.

---

### Task 6: Rewrite `main.c` as Thin Orchestrator

**Files:**

- Modify: `ESP/ESP-Mesh-Node-sensor-test/main/main.c`

Replace the entire 781-line `main.c` with:

```c
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
```

---

### Task 7: Update CMakeLists.txt and Delete Legacy Files

**Files:**

- Modify: `ESP/ESP-Mesh-Node-sensor-test/main/CMakeLists.txt`
- Delete: `ESP/ESP-Mesh-Node-sensor-test/main/ble_service.c`
- Delete: `ESP/ESP-Mesh-Node-sensor-test/main/ble_service.h`

**Step 1: Update CMakeLists.txt**

```cmake
set(srcs
    "main.c"
    "sensor.c"
    "load_control.c"
    "nvs_store.c"
    "command.c"
    "mesh_node.c"
)

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS ".")
```

**Step 2: Delete legacy files**

```bash
cd ESP/ESP-Mesh-Node-sensor-test/main
rm ble_service.c ble_service.h
```

These were from v0.3.0 (Pico-bridge architecture) and are not referenced anywhere in the build.

**Step 3: Build**

```bash
cd ESP/ESP-Mesh-Node-sensor-test
idf.py build
```

Expected: `Project build complete. To flash, run: idf.py flash`

**Step 4: Commit**

```bash
git add -A
git commit -m "refactor(sensor-node): split main.c into sensor, load_control, command, mesh_node, nvs_store modules"
```

---

## 3. Batch 2 — GATT Gateway (`ESP/ESP_GATT_BLE_Gateway/`)

### Current File: `main/main.c` (1036 lines)

**Function inventory:**

| Function | Lines | Module Target |
|---|---|---|
| `save_gw_state()` | 115-120 | `nvs_store.c` |
| `restore_gw_state()` | 122-139 | `nvs_store.c` |
| `register_known_node()` | 198-212 | `node_tracker.c` |
| `gatt_notify_sensor_data()` | 222-283 | `gatt_service.c` |
| `generic_client_cb()` | 285-323 | `mesh_gateway.c` |
| `send_mesh_onoff()` | 325-350 | `mesh_gateway.c` |
| `send_vendor_command()` | 352-400 | `mesh_gateway.c` |
| `custom_model_cb()` | 402-527 | `mesh_gateway.c` |
| `monitor_timer_cb()` | 529-538 | `monitor.c` |
| `monitor_start()` | 540-549 | `monitor.c` |
| `monitor_stop()` | 551-558 | `monitor.c` |
| `process_gatt_command()` | 560-693 | `command_parser.c` |
| `sensor_data_access_cb()` | 695-709 | `gatt_service.c` |
| `command_access_cb()` | 711-738 | `gatt_service.c` |
| GATT service def | 740-761 | `gatt_service.c` |
| `ble_gap_event()` | 763-798 | `gatt_service.c` |
| `gatt_advertise()` | 800-824 | `gatt_service.c` |
| `gatt_on_sync()` | 826-829 | `gatt_service.c` |
| `gatt_host_task()` | 831-835 | `gatt_service.c` |
| `prov_complete()` | 838-848 | `mesh_gateway.c` |
| `provisioning_cb()` | 850-873 | `mesh_gateway.c` |
| `config_server_cb()` | 875-909 | `mesh_gateway.c` |
| `mesh_init()` | 912-942 | `mesh_gateway.c` |
| `gatt_register_services()` | 944-969 | `gatt_service.c` |
| `gatt_start_advertising()` | 971-975 | `gatt_service.c` |
| `app_main()` | 977-1036 | `main.c` |

Follow the same pattern as Batch 1:

**New files to create:**

- `gatt_service.h` / `gatt_service.c` — GATT service def, callbacks, advertising, chunked notify
- `mesh_gateway.h` / `mesh_gateway.c` — vendor client model, mesh callbacks, send functions
- `command_parser.h` / `command_parser.c` — `process_gatt_command()`
- `monitor.h` / `monitor.c` — monitor mode timer
- `node_tracker.h` / `node_tracker.c` — `known_nodes[]`, `register_known_node()`
- `nvs_store.h` / `nvs_store.c` — save/restore gateway state

**Key shared globals:**

- `gatt_conn_handle` — `gatt_service.c` (extern in header for `mesh_gateway.c` to check)
- `sensor_char_val_handle` — `gatt_service.c` (static, used only by GATT callbacks)
- `vnd_bound` — `mesh_gateway.c` (extern in header for `command_parser.c`)
- `vnd_send_busy/target_addr` — `mesh_gateway.c` (extern in header for `monitor.c`)
- `known_nodes[]` / `known_node_count` — `node_tracker.c` (extern in header)
- `cached_net_idx/app_idx` — `mesh_gateway.c` (extern in header)
- `gw_state` — `nvs_store.c` (extern in header)

**CMakeLists.txt update:**

```cmake
set(srcs
    "main.c"
    "gatt_service.c"
    "mesh_gateway.c"
    "command_parser.c"
    "monitor.c"
    "node_tracker.c"
    "nvs_store.c"
)

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS ".")
```

**Build and commit:**

```bash
cd ESP/ESP_GATT_BLE_Gateway
idf.py build
git add -A
git commit -m "refactor(gatt-gateway): split main.c into gatt_service, mesh_gateway, command_parser, monitor, node_tracker, nvs_store"
```

---

## 4. Batch 3 — Provisioner (`ESP/ESP-Provisioner/`)

### Current File: `main/main.c` (868 lines)

**New files to create:**

- `node_registry.h` / `node_registry.c` — `mesh_node_info_t`, `store_node_info()`, `get_node_info()`, `node_count`, `nodes[]`
- `composition.h` / `composition.c` — `parse_composition_data()`
- `model_binding.h` / `model_binding.c` — `bind_model()`, `bind_vendor_model()`, `subscribe_vendor_model_to_group()`, `bind_next_model()`
- `provisioning.h` / `provisioning.c` — all callback functions, `prov_complete()`, `recv_unprov_adv_pkt()`
- `mesh_config.h` / `mesh_config.c` — `set_config_common()`, `set_msg_common()`, composition/provision structs, `prov_key`

**CMakeLists.txt update:**

```cmake
idf_component_register(SRCS "main.c"
                            "node_registry.c"
                            "composition.c"
                            "model_binding.c"
                            "provisioning.c"
                            "mesh_config.c"
                    INCLUDE_DIRS ".")
```

**Build and commit:**

```bash
cd ESP/ESP-Provisioner
idf.py build
git add -A
git commit -m "refactor(provisioner): split main.c into node_registry, composition, model_binding, provisioning, mesh_config"
```

---

## 5. Batch 4 — Relay Node (`ESP/ESP-Mesh-Relay-Node/`)

### Current File: `main/main.c` (311 lines)

Already small. Light split:

- `mesh_relay.h` / `mesh_relay.c` — mesh models, callbacks, `ble_mesh_init()`
- `led.h` / `led.c` — `led_init()`, `led_heartbeat_task()`
- `nvs_store.h` / `nvs_store.c` — save/restore

**CMakeLists.txt update:**

```cmake
set(srcs
    "main.c"
    "mesh_relay.c"
    "led.c"
    "nvs_store.c"
)

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS ".")
```

**Build and commit:**

```bash
cd ESP/ESP-Mesh-Relay-Node
idf.py build
git add -A
git commit -m "refactor(relay-node): split main.c into mesh_relay, led, nvs_store"
```

---

## 6. Common Pitfalls and Fixes

### 6.1 `static` Function Errors

**Symptom:** `undefined reference to 'function_name'`

**Fix:** The function was `static` in the original `main.c`. Remove `static` in the new `.c` file and add a declaration in the `.h` file.

### 6.2 Duplicate Symbol Errors

**Symptom:** `multiple definition of 'variable_name'`

**Fix:** The variable was defined (not just declared) in a header. Move the definition to one `.c` file and use `extern` in the `.h`:

```c
// header.h — declaration only
extern int my_variable;

// source.c — definition
int my_variable = 0;
```

### 6.3 Missing Includes

**Symptom:** `implicit declaration of function`, `unknown type name`

**Fix:** Add the appropriate `#include` at the top of the `.c` file. Common ones:

- `#include <stdint.h>` for `uint16_t`
- `#include <stdbool.h>` for `bool`
- `#include "esp_err.h"` for `esp_err_t`
- `#include "esp_ble_mesh_defs.h"` for mesh types

### 6.4 Circular Dependencies

**Symptom:** Two headers include each other

**Fix:** Use forward declarations instead of includes where possible. For example, if `command.h` doesn't need the full `mesh_node_state` struct, forward-declare it.

### 6.5 Tag Collisions

**Symptom:** Multiple modules define `static const char *TAG = "MESH_NODE"`

**Fix:** Each module uses its OWN unique tag: `"SENSOR"`, `"LOAD_CTRL"`, `"CMD"`, `"MESH_NODE"`, `"NVS"`.

---

## 7. Verification Checklist

| Step | Command | Expected |
|---|---|---|
| Build Sensor Node | `cd ESP/ESP-Mesh-Node-sensor-test && idf.py build` | `Project build complete` |
| Build GATT Gateway | `cd ESP/ESP_GATT_BLE_Gateway && idf.py build` | `Project build complete` |
| Build Provisioner | `cd ESP/ESP-Provisioner && idf.py build` | `Project build complete` |
| Build Relay Node | `cd ESP/ESP-Mesh-Relay-Node && idf.py build` | `Project build complete` |
| Flash + test sensor console | Flash sensor node, type `read` in monitor | Prints `D:0%,V:...` |
| Flash + test full mesh | Flash all devices, run `gateway.py` | Connects, commands work |

---

## 8. Summary

| Project | Before | After |
|---|---|---|
| Sensor Node | 1 × 781-line `main.c` | 5 modules + thin `main.c` (~30 lines) |
| GATT Gateway | 1 × 1036-line `main.c` | 6 modules + thin `main.c` (~30 lines) |
| Provisioner | 1 × 868-line `main.c` | 5 modules + thin `main.c` (~30 lines) |
| Relay Node | 1 × 311-line `main.c` | 3 modules + thin `main.c` (~30 lines) |

**Total: 4 files → 38 files. Zero behavior changes. Zero reprovisioning.**
