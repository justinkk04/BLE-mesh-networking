# v0.6.2 Modular Code Cleanup Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Refactor all four ESP32-C6 firmware projects from monolithic `main.c` files into clean, single-responsibility modules. Zero behavior changes — code organization only.

**Architecture:** Each project's single `main.c` (781-1036 lines) is split into focused `.c`/`.h` pairs grouped by responsibility. `app_main()` shrinks to ~30 lines calling init functions. All globals become either module-local statics or exposed via header accessors.

**Tech Stack:** ESP-IDF 5.x (C), CMake build system

---

## System Context

Read `MESH_IMPLEMENTATION.md` in the project root for full architecture. Key points:

### File Map (Current — Before Cleanup)

| Component | Path | Lines | Responsibilities Crammed In |
|---|---|---|---|
| Sensor Node | `ESP/ESP-Mesh-Node-sensor-test/main/main.c` | 781 | I2C sensor, PWM control, mesh callbacks, vendor model, command parsing, serial console, NVS, mesh init |
| GATT Gateway | `ESP/ESP_GATT_BLE_Gateway/main/main.c` | 1036 | GATT service, chunked notify, vendor client, command parser, mesh callbacks, monitor mode, NVS, mesh init, known node tracking |
| Provisioner | `ESP/ESP-Provisioner/main/main.c` | 868 | Node registry, composition parser, model binding, group subscription, provisioning callbacks, config client |
| Relay Node | `ESP/ESP-Mesh-Relay-Node/main/main.c` | 311 | LED heartbeat, mesh callbacks, NVS (already small enough — minimal changes) |

### Build System

All projects use `main/CMakeLists.txt` with explicit `SRCS` lists:

```cmake
set(srcs "main.c")
idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS ".")
```

New `.c` files MUST be added to this `srcs` list.

### Existing Splits

The sensor node already has a legacy `ble_service.c`/`ble_service.h` from the old Pico-bridge architecture (v0.3.0). These files exist but are **NOT in the CMakeLists.txt** — they are dead code. They should be **deleted** during cleanup.

### What Does NOT Change

- Zero behavior changes — every function does exactly what it did before
- No new features, no removed features
- Mesh protocol unchanged — same opcodes, same message formats
- No reprovisioning needed — NVS keys and stored state formats are unchanged
- Python `gateway.py` — completely untouched
- Relay Node — minimal changes (already small at 311 lines)

---

## Batch 1: Sensor Node (Tasks 1-7)

### Target File Structure

```
ESP/ESP-Mesh-Node-sensor-test/main/
  main.c          (~30 lines)   — app_main() only
  sensor.c        (~100 lines)  — I2C init, INA260 read voltage/current, I2C scan
  sensor.h        (~20 lines)   — sensor_init(), ina260_read_voltage(), ina260_read_current()
  load_control.c  (~50 lines)   — PWM init, set_duty()
  load_control.h  (~15 lines)   — pwm_init(), set_duty(), get_current_duty()
  command.c       (~80 lines)   — process_command(), format_sensor_response(), console_task()
  command.h       (~15 lines)   — process_command(), format_sensor_response(), console_task()
  mesh_node.c     (~300 lines)  — mesh models, callbacks, ble_mesh_init()
  mesh_node.h     (~25 lines)   — ble_mesh_init(), node_state accessors
  nvs_store.c     (~30 lines)   — save_node_state(), restore_node_state()
  nvs_store.h     (~15 lines)   — save/restore declarations, NVS_HANDLE extern
  CMakeLists.txt  (updated)     — all new .c files listed
```

**Legacy files to DELETE:**

- `ble_service.c` — dead code from v0.3.0 Pico-bridge architecture
- `ble_service.h` — dead code from v0.3.0 Pico-bridge architecture

---

## Batch 2: GATT Gateway (Tasks 8-13)

### Target File Structure

```
ESP/ESP_GATT_BLE_Gateway/main/
  main.c              (~30 lines)   — app_main() only
  gatt_service.c      (~180 lines)  — GATT service def, callbacks, advertise, chunked notify
  gatt_service.h      (~20 lines)   — gatt_register_services(), gatt_start_advertising(), gatt_notify_sensor_data()
  mesh_gateway.c      (~200 lines)  — vendor client model, custom_model_cb(), send_vendor_command()
  mesh_gateway.h      (~20 lines)   — mesh_init(), send_vendor_command(), send_mesh_onoff()
  command_parser.c    (~150 lines)  — process_gatt_command()
  command_parser.h    (~10 lines)   — process_gatt_command()
  monitor.c           (~40 lines)   — monitor mode timer callbacks
  monitor.h           (~10 lines)   — monitor_start(), monitor_stop()
  node_tracker.c      (~25 lines)   — known_nodes[], register_known_node()
  node_tracker.h      (~15 lines)   — register_known_node(), get_known_nodes()
  nvs_store.c         (~30 lines)   — save_gw_state(), restore_gw_state()
  nvs_store.h         (~15 lines)   — save/restore, NVS_HANDLE extern
  CMakeLists.txt      (updated)
```

---

## Batch 3: Provisioner (Tasks 14-18)

### Target File Structure

```
ESP/ESP-Provisioner/main/
  main.c              (~30 lines)   — app_main() only
  node_registry.c     (~60 lines)   — store_node_info(), get_node_info(), mesh_node_info_t
  node_registry.h     (~30 lines)   — struct + accessor declarations
  composition.c       (~80 lines)   — parse_composition_data()
  composition.h       (~10 lines)   — parse_composition_data()
  model_binding.c     (~120 lines)  — bind_model(), bind_vendor_model(), subscribe_vendor_model_to_group(), bind_next_model()
  model_binding.h     (~15 lines)   — bind_next_model() + helpers
  provisioning.c      (~250 lines)  — provisioning_cb(), config_client_cb(), recv_unprov_adv_pkt(), prov_complete()
  provisioning.h      (~15 lines)   — callback registrations
  mesh_config.c       (~60 lines)   — set_config_common(), set_msg_common(), mesh composition/provision structs
  mesh_config.h       (~20 lines)   — helpers + extern declarations
  CMakeLists.txt      (updated)
```

---

## Batch 4: Relay Node (Task 19)

The relay node is only 311 lines — already reasonable. Minimal split:

```
ESP/ESP-Mesh-Relay-Node/main/
  main.c          (~30 lines)   — app_main() only
  mesh_relay.c    (~200 lines)  — mesh models, callbacks, ble_mesh_init()
  mesh_relay.h    (~15 lines)   — ble_mesh_init()
  led.c           (~30 lines)   — led_init(), led_heartbeat_task()
  led.h           (~10 lines)   — led_init(), led_heartbeat_task()
  nvs_store.c     (~30 lines)   — save/restore
  nvs_store.h     (~10 lines)   — declarations
  CMakeLists.txt  (updated)
```

---

## Verification Plan

### Build Verification (Automated — Per Batch)

After each batch, verify the project compiles:

```bash
cd ESP/<project>
idf.py build
```

Expected: `Project build complete. To flash, run: idf.py flash`

### Functional Verification (Manual — After All Batches)

Since this is a pure refactoring with zero behavior changes:

1. **Flash a sensor node** → verify serial console works (`read`, `duty:50`, `r`, `s` commands in `idf.py monitor`)
2. **Flash the GATT gateway** → verify mesh provisioning completes (Provisioner serial monitor shows "FULLY CONFIGURED")
3. **Start `gateway.py`** → verify it connects and can poll sensor data
4. **Run `ALL:READ`** → verify group addressing still works
5. **Run `threshold 5000`** → verify PowerManager still balances

> [!TIP]
> If a build fails after splitting, the most common issues are:
>
> - Missing `#include` in the new file
> - Function was `static` in `main.c` — remove `static` and add declaration to header
> - Forgot to add the `.c` file to `CMakeLists.txt`
> - Global variable accessed from two files — use `extern` in header

---

## Rollback Plan

Git revert — the refactoring is the only commit in each batch. Since behavior is unchanged, revert is trivial.

---

## Files Changed Summary

| Project | Files Before | Files After | Lines Moved |
|---|---|---|---|
| Sensor Node | 1 `.c` + 2 dead files | 5 `.c` + 5 `.h` | 781 reorganized |
| GATT Gateway | 1 `.c` | 6 `.c` + 6 `.h` | 1036 reorganized |
| Provisioner | 1 `.c` | 5 `.c` + 5 `.h` | 868 reorganized |
| Relay Node | 1 `.c` | 3 `.c` + 3 `.h` | 311 reorganized |

**Total: 4 monolithic files → 19 `.c` + 19 `.h` files. Zero behavior changes.**
