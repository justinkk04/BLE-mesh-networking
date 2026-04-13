# ESP32-C6 Sensing Node

**Firmware:** ESP-IDF 5.x (C) · **Target:** ESP32-C6

Reads INA260 voltage/current/power via I2C, controls a load via LEDC PWM, and communicates over BLE Mesh (vendor model + Generic OnOff).

## Architecture

```
Pi 5 ──GATT──> GATT Gateway ──Mesh──> [THIS NODE]
                                        ├── I2C → INA260 (sensor)
                                        └── PWM → 2N2222 → MOSFET (load)
```

## Module Map

| File | Responsibility | Key Functions |
|---|---|---|
| `main.c` | **Thin orchestrator** — init calls only | `app_main()` |
| `sensor.c/h` | I2C bus init, INA260 probe, voltage/current/power reads | `sensor_init()`, `sensor_is_ready()`, `ina260_read_voltage()`, `ina260_read_current()` |
| `load_control.c/h` | LEDC PWM output, duty cycle, ramp test | `pwm_init()`, `set_duty()`, `start_ramp()`, `stop_load()`, `ramp_task()` |
| `command.c/h` | Serial console parser + command dispatch | `console_task()`, `process_command()` |
| `mesh_node.c/h` | BLE Mesh provisioning, vendor SERVER model, Generic OnOff | `ble_mesh_init()`, `provisioning_cb()`, `custom_model_cb()` |
| `nvs_store.c/h` | NVS handle + device UUID globals | `NVS_HANDLE`, `dev_uuid` |

## Init Order (in `app_main`)

```
nvs_flash_init() → ble_mesh_nvs_open() → bluetooth_init() →
ble_mesh_get_dev_uuid() → sensor_init() → pwm_init() →
ble_mesh_init() → xTaskCreate(console_task)
```

## Mesh Identity

- **UUID prefix:** `0xdd` (auto-provisioned by the provisioner)
- **Vendor SERVER model** (`0x0001`, CID `0x02E5`): receives `VND_OP_SEND`, responds with `VND_OP_STATUS`
- **Generic OnOff Server/Client**: backup command path
- **Group address:** Subscribed to `0xC000` for broadcast commands

## Sensor Data Format

Responses sent back through mesh as:

```
D:<duty>%,V:<voltage>V,I:<current>MA,P:<power>MW
```

Example: `D:50%,V:12.34V,I:1234.5MA,P:15234.5MW`

## Serial Console Commands

Type directly in the ESP monitor for local testing:

- `read` — single sensor reading
- `duty:50` — set 50% duty cycle
- `r` — start ramp test
- `s` — stop load

## Build

```bash
idf.py set-target esp32c6
idf.py build flash monitor
```

## Adding New Modules

1. Create `new_module.c` and `new_module.h`
2. Add `new_module.c` to `CMakeLists.txt` SRCS
3. Add init call to `main.c` (keep main.c as thin orchestrator)
