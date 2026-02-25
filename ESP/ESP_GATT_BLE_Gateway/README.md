# ESP32-C6 GATT Gateway

**Firmware:** ESP-IDF 5.x (C) · **Target:** ESP32-C6

Bridges the Pi 5 (BLE GATT client) to the BLE Mesh network. Receives commands from the Pi via GATT writes, translates them to mesh vendor model messages, and forwards mesh responses back as GATT notifications.

## Architecture

```
Pi 5 ──GATT──> [THIS NODE] ──Mesh──> Sensor Node(s)
               Service 0xDC01         Relay Node(s)
```

## Module Map

| File | Responsibility | Key Functions |
|---|---|---|
| `main.c` | **Thin orchestrator** — init calls only | `app_main()` |
| `gatt_service.c/h` | Custom GATT service (0xDC01), advertising, chunked notifications | `gatt_register_services()`, `gatt_start_advertising()`, `gatt_notify_sensor_data()` |
| `mesh_gateway.c/h` | Mesh composition, vendor CLIENT model, command forwarding | `mesh_init()`, `send_vendor_command()`, `send_mesh_onoff()`, `custom_model_cb()` |
| `command_parser.c/h` | Parse "NODE:CMD:VAL" format from Pi 5 GATT writes | `parse_command()`, `execute_command()` |
| `monitor.c/h` | Continuous sensor polling via FreeRTOS timer | `monitor_task()`, `start_monitoring()`, `stop_monitoring()` |
| `node_tracker.c/h` | Track provisioned node addresses for mesh routing | `node_tracker_add()`, `node_tracker_get_addr()` |
| `nvs_store.c/h` | NVS handle + device UUID globals | `NVS_HANDLE`, `dev_uuid` |

## Init Order (in `app_main`)

> ⚠️ **CRITICAL:** GATT services MUST be registered BEFORE `mesh_init()` — mesh init locks the GATT table.

```
nvs_flash_init() → ble_mesh_nvs_open() → bluetooth_init() →
ble_mesh_get_dev_uuid() → gatt_register_services() →
mesh_init() → gatt_start_advertising()
```

## GATT Service

| Characteristic | UUID | Properties | Purpose |
|---|---|---|---|
| Sensor Data | `0xDC02` | Read + Notify | Sensor readings from mesh nodes |
| Command | `0xDC03` | Write | Commands from Pi 5 |

## Command Format

Pi 5 writes commands as UTF-8 strings:

```
<node_id>:<command>[:<value>]
```

Examples: `0:RAMP`, `1:DUTY:50`, `ALL:READ`, `2:STOP`

## Chunked Notifications

Messages > 20 bytes are split into chunks:

- Continuation chunks prefixed with `+`
- Final chunk has no prefix
- Pi 5 reassembles by buffering `+` chunks

## Mesh Identity

- **Vendor CLIENT model** (`0x0000`, CID `0x02E5`): sends `VND_OP_SEND` to nodes, receives `VND_OP_STATUS`
- Advertises as `"Mesh-Gateway"` for Pi 5 discovery

## Build

```bash
idf.py set-target esp32c6
idf.py build flash monitor
```
