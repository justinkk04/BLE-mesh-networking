# C Basics For This Repo

This is only the C syntax you need for this project.

## 1) `#define` constants

Used for opcodes, addresses, and config values.

Example:

```c
#define VND_OP_SEND   ESP_BLE_MESH_MODEL_OP_3(0x00, CID_ESP)
#define MESH_GROUP_ADDR 0xC000
```

Where:

- `ESP/ESP_GATT_BLE_Gateway/main/main.c:50`
- `ESP/ESP_GATT_BLE_Gateway/main/main.c:70`

## 2) `struct` state containers

Used to keep mesh/node state in RAM and NVS.

```c
typedef struct {
  uint16_t net_idx;
  uint16_t app_idx;
  uint16_t addr;
} mesh_node_state_t;
```

You will see this pattern in all ESP firmware files.

## 3) `static` functions and globals

`static` here means file-local visibility.

```c
static void provisioning_cb(...);
static esp_err_t send_vendor_command(...);
```

Why it matters:

- keeps symbols private to one `.c` file
- avoids name conflicts

## 4) Callback pattern (most important concept)

ESP-IDF is callback-driven.

You register functions, then ESP/BLE stack calls them on events.

```c
esp_ble_mesh_register_prov_callback(provisioning_cb);
esp_ble_mesh_register_custom_model_callback(custom_model_cb);
```

Where:

- `ESP/ESP_GATT_BLE_Gateway/main/main.c:915`
- `ESP/ESP-Mesh-Node-sensor-test/main/main.c:699`

## 5) `switch (event)` dispatch

Most callbacks look like this:

```c
switch (event) {
  case ESP_BLE_MESH_MODEL_OPERATION_EVT:
    // handle message
    break;
  case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
    // handle send complete
    break;
}
```

Read order in this repo:

1. find callback function
2. find `switch (event)`
3. read only the case you care about

## 6) Buffer and payload handling

You will see this repeatedly:

```c
char buf[64];
uint16_t len = param->model_operation.length;
if (len >= sizeof(buf)) len = sizeof(buf) - 1;
memcpy(buf, param->model_operation.msg, len);
buf[len] = '\0';
```

This is safe string extraction from message payload bytes.

## 7) Return codes (`esp_err_t`)

Functions return `ESP_OK` on success.

Always check errors:

```c
esp_err_t err = esp_ble_mesh_init(&provision, &composition);
if (err != ESP_OK) {
  ESP_LOGE(TAG, "Mesh init failed: %d", err);
  return err;
}
```

## 8) What to ignore for now

Skip these until later:

- macro internals like `ESP_BLE_MESH_MODEL_OP_3(...)`
- all `sdkconfig` options
- advanced FreeRTOS task scheduling details

## 9) Minimal reading plan for C files

For each file:

1. Find role comment at top
2. Read constants (`#define`)
3. Read callback registration in init
4. Read one callback `switch` path end-to-end

Use with:

- `[[ESP-IDF/ESP-IDF-Index]]`
- `[[Labs/Hands-On-Lab-1-Trace-READ]]`
