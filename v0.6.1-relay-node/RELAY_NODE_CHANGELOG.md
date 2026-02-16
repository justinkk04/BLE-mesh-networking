# v0.6.1 Relay-Only Node - Implementation Changelog

**Date:** February 15, 2026
**Status:** COMPLETE AND VERIFIED
**Result:** Relay node extends mesh range — silently forwards packets, blinks LED heartbeat, auto-rejoins mesh after power cycle via NVS persistence.

---

## Final File Changes Summary

| File | Lines Changed | What Changed |
|---|---|---|
| `ESP/ESP-Mesh-Relay-Node/CMakeLists.txt` | 1 modified | Project name: `node-1-ble-mesh` → `relay-node-ble-mesh` |
| `ESP/ESP-Mesh-Relay-Node/main/main.c` | 781 → 270 | Full rewrite: stripped I2C, PWM, vendor model, command processing, console task. Added LED heartbeat task |
| `ESP/ESP-Mesh-Relay-Node/sdkconfig.defaults` | 1 removed | Removed `CONFIG_BLE_MESH_GENERIC_ONOFF_CLI=y` |

**Total: ~510 lines removed, ~270 lines final** (stripped from 781-line sensing firmware).

---

## Task 1: CMakeLists.txt - PASSED

### Change Made

- Line 8: `project(node-1-ble-mesh)` → `project(relay-node-ble-mesh)`

---

## Task 2: main.c Rewrite - PASSED

### What Was Removed

| Component | Functions/Defines Removed |
|---|---|
| I2C/INA260 | `I2C_PORT`, `I2C_SDA_PIN`, `I2C_SCL_PIN`, `INA260_ADDR`, `i2c_scan()`, `sensor_init()`, `ina260_read_voltage()`, `ina260_read_current()` |
| PWM/Load | `PWM_GPIO`, `pwm_init()`, `set_duty()` |
| Vendor Model | `VND_MODEL_ID_SERVER`, `VND_OP_SEND`, `VND_OP_STATUS`, `vnd_op[]`, `vnd_models[]`, `custom_model_cb()` |
| Command Processing | `format_sensor_response()`, `process_command()` |
| OnOff Client/Server | `onoff_client`, `onoff_server`, `onoff_cli_pub`, `onoff_srv_pub`, `generic_server_cb()`, `generic_client_cb()` |
| Console | `console_task()` |
| Includes | `driver/i2c.h`, `driver/ledc.h`, `<math.h>` |

### What Was Kept

| Component | Details |
|---|---|
| Mesh Init | `ble_mesh_init()` with provisioning + config server callbacks |
| Config Server | Relay enabled, `relay_retransmit = TRANSMIT(4, 20)`, TTL=7 |
| NVS Persistence | `save_node_state()`, `restore_node_state()` with key `"mesh_relay"` |
| Provisioning | Full callback chain: register, enable, link open/close, complete, reset |
| UUID | `0xdd 0xdd` prefix for auto-provisioning |

### What Was Added

| Component | Details |
|---|---|
| LED Heartbeat | `led_init()` on GPIO8, `led_heartbeat_task()` FreeRTOS task |
| LED Patterns | Fast blink (200ms) = unprovisioned, Slow blink (1s) = provisioned & active |
| `provisioned` flag | Tracks state for LED pattern switching |

---

## Task 3: sdkconfig.defaults Cleanup - PASSED

### Change Made

- Removed `CONFIG_BLE_MESH_GENERIC_ONOFF_CLI=y` — relay has no OnOff client
- Deleted stale `sdkconfig` (83KB), `sdkconfig.old` (83KB), and `build/` directory

---

## Hardware Verification - PASSED

### Provisioning

Provisioner auto-discovered relay node (UUID prefix `0xdd`) and provisioned it successfully. LED switched from fast blink to slow blink on provisioning complete.

### Pi 5 Gateway Discovery

```
[POWER] Probing 3 sensing node(s)...
[POWER] Found node 1
[POWER] Found node 2
[POWER] Node 3 no response
[POWER] Discovery complete: 2 node(s)
```

Gateway correctly identified 2 sensing nodes, gracefully handled the relay's non-response, and proceeded with PowerManager balancing.

### Command Forwarding

- `ALL:DUTY:100` and `ALL:DUTY:0` — both sensing nodes responded, relay silent ✓
- `ALL:READ` — both NODE1 and NODE2 returned sensor data, no NODE3 response ✓
- PowerManager priority balancing worked correctly with relay in the mesh ✓

---

## Design Decisions

### 1. No Vendor Model

The relay has no vendor model, so it never responds to READ/DUTY/RAMP commands. The Pi 5 gateway's `_bootstrap_discovery()` naturally skips it after one failed probe. This is simpler and more robust than adding a "relay type" flag.

### 2. Separate NVS Key

Uses `"mesh_relay"` instead of `"mesh_node"` to avoid conflicts if both firmware types are flashed to the same device during development.

### 3. Config Server Only

The element contains only `ESP_BLE_MESH_MODEL_CFG_SRV` — no OnOff, no vendor. This is the absolute minimum for a functioning relay node. The provisioner's `bind_next_model()` chain completes quickly since there are fewer models to bind.

### 4. LED Heartbeat Over Serial

An LED heartbeat provides immediate visual feedback without needing a serial monitor. Two patterns (fast/slow) distinguish unprovisioned from active states at a glance.

---

## Known Limitations

1. **BLE scan count inflation:** Gateway reports `"3 sensing node(s) in mesh"` because the relay advertises as `ESP-BLE-MESH`. This is cosmetic — the relay is correctly excluded from PowerManager after the failed probe.
2. **Single GPIO for LED:** Currently uses GPIO8. Must be changed in `main.c` if wiring differs.
