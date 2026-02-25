# v0.7.0 Gateway Failover Plan

**Date:** February 25, 2026
**Author:** Justin Kwarteng
**Status:** Draft — Awaiting Review

---

## 1. Problem Statement

The current architecture uses a **dedicated GATT Gateway ESP32-C6** that bridges the Pi 5 to the mesh network. If this gateway loses power, disconnects, or crashes:

- `gateway.py` hangs or crashes — the user must manually restart it
- The entire mesh network becomes unreachable from the Pi 5
- No sensor data flows, no commands can be sent
- There is no auto-reconnect or failover mechanism

This is a **single point of failure** in the v0.6.1 architecture:

```
Pi 5 ──GATT──> GATT Gateway (SPOF) ──Mesh──> Sensor Node(s)
                    ↑ if this dies, everything stops
```

## 2. Goal

**Eliminate the GATT Gateway as a single point of failure** by:

1. **Pi 5 auto-reconnect:** `gateway.py` detects disconnection and automatically rescans + reconnects (no manual restart)
2. **Consolidated "Universal Node" firmware:** Every sensing node also runs the GATT service, so the Pi 5 can connect to ANY node — not just the dedicated gateway
3. **Seamless failover:** If the connected node dies, `gateway.py` automatically connects to the next available node

### Target Architecture

```
Pi 5 ──GATT──> Any Universal Node ──Mesh──> Other Universal Node(s)
                  ↕ can failover to any other node
```

## 3. Two-Phase Approach

### Phase 1: Pi 5 Auto-Reconnect (gateway.py only — no firmware changes)

**Scope:** Modify `gateway.py` to detect BLE disconnection and auto-reconnect to the same or next available GATT gateway.

**What changes:**

- Add `_reconnect_loop()` to `dc_gateway.py` (`DCMonitorGateway` class)
- Add reconnect state fields to `DCMonitorGateway.__init__()`
- Add `_paused` flag to `power_manager.py` (`PowerManager` class)
- Guard `send_command()` in `dc_gateway.py` during reconnection
- Start reconnect loop from `tui_app.py` (`MeshGatewayApp.connect_ble()`)
- On disconnect: rescan, reconnect, resubscribe to notifications
- PM and dashboard poll loops pause during reconnection, resume after
- TUI shows reconnection status

**Risk:** 🟢 Low — Python-only, no firmware changes, no reprovisioning
**Effort:** ~100 lines of Python across 3 modules

### Phase 2: Consolidated Universal Node Firmware (ESP firmware changes)

**Scope:** Merge the GATT Gateway's capabilities into the Sensor Node firmware, creating a single "Universal Node" that can both sense AND act as a GATT gateway.

**What changes on the ESP side:**

- Sensor node gets: GATT service (0xDC01), vendor CLIENT model, command parser, chunked notify
- Sensor node keeps: I2C sensor, PWM control, vendor SERVER model, relay
- When Pi 5 connects via GATT, node enables gateway mode (forwards commands to mesh)
- When no GATT connection, node operates as a normal sensor/relay
- **New modules** should be created (e.g. `gatt_service.c`/`.h`, `command_parser.c`/`.h`) — do NOT dump code into `main.c`
- Update `CMakeLists.txt` SRCS list for any new files

**What changes on the Pi 5 side:**

- `constants.py` — update `DEVICE_NAME_PREFIXES` to include `"DC-Monitor"`
- `dc_gateway.py` — add failover logic to `_auto_reconnect_loop()`
- `dc_gateway.py` — adjust `sensing_node_count` when connected node is also a sensor

**Risk:** 🟡 Medium — Significant firmware changes, requires reprovisioning, potential memory/BLE stack conflicts
**Effort:** ~400 lines of C across new modules, ~50 lines of Python across 2 modules

> [!IMPORTANT]
> Phase 1 should be completed and verified independently before starting Phase 2. Phase 1 alone is valuable — it fixes the "must restart gateway.py" annoyance even without firmware consolidation.

## 4. Key Technical Challenges

### 4.1 ESP-IDF BLE Mesh + NimBLE GATT Coexistence

The current GATT Gateway already proves this works. The critical init order is:

1. `bluetooth_init()` — initializes NimBLE stack
2. `ble_gatts_count_cfg()` + `ble_gatts_add_svcs()` — register GATT services
3. `esp_ble_mesh_init()` — locks the GATT table
4. `gatt_advertise()` — start GATT advertising

The sensor node currently skips steps 2 and 4. Adding them follows the exact same pattern already working in `ESP_GATT_BLE_Gateway/main/main.c`.

### 4.2 Dual-Role Vendor Model

The consolidated node needs **both** vendor models:

| Model | Current Location | Purpose |
|-------|-----------------|---------|
| Vendor SERVER (`0x0001`) | Sensor Node | Receives commands, responds with sensor data |
| Vendor CLIENT (`0x0000`) | GATT Gateway | Sends commands to other nodes, receives responses |

Both can coexist on the same element. The provisioner already handles nodes with both models — it just hasn't seen one yet.

### 4.3 Self-Addressing

When a universal node receives a GATT command targeting itself (e.g., `0:READ` where node 0 is the connected node), it must process the command locally instead of sending it over mesh. The current GATT gateway never had this issue because it had no sensors.

### 4.4 Memory Budget

The ESP32-C6 has 320KB SRAM. Adding the GATT service + vendor client + command parser adds ~15-20KB. The current sensor node uses ~180KB, and the GATT gateway uses ~195KB. A consolidated node should fit within ~210KB, well within budget.

### 4.5 Provisioner Impact

The provisioner detects models via composition data (`parse_composition_data()`). A consolidated node will report both `VND_MODEL_ID_SERVER` and `VND_MODEL_ID_CLIENT`. The provisioner already handles both — it just needs to bind both on the same node.

## 5. What Does NOT Change

- Relay Node firmware — stays as-is (no sensors, no GATT)
- Provisioner firmware — minimal changes (already handles both models)
- PowerManager algorithm — unchanged
- TUI layout — unchanged
- Mesh protocol — still the same vendor opcodes
- BLE Mesh group addressing — unchanged

## 6. Verification Plan

### Phase 1 Verification

1. Start `gateway.py`, connect to GATT gateway
2. Power-cycle the GATT gateway ESP32
3. **Verify:** `gateway.py` detects disconnection, shows reconnection attempt, automatically reconnects when gateway powers back up
4. **Verify:** Commands work normally after reconnection
5. **Verify:** PM resumes polling after reconnection

### Phase 2 Verification

1. Flash consolidated firmware on all sensing nodes
2. Reprovision all nodes
3. Connect Pi 5 to any node — verify commands + sensor data work
4. Power-cycle the connected node
5. **Verify:** Pi 5 automatically connects to a different node
6. **Verify:** Commands reach all remaining nodes through the new gateway
7. **Verify:** Original node rejoins mesh when powered back on

## 7. Rollback

- **Phase 1:** Revert `gateway.py` changes — no firmware impact
- **Phase 2:** Reflash original sensor node firmware — the old dedicated GATT gateway still exists as a fallback

## 8. Files Changed Summary (Both Phases)

> [!IMPORTANT]
> **Modular rule:** All changes go into the correct module file. `main.c` stays as a thin orchestrator.
> New ESP functionality = new `.c`/`.h` pair + update `CMakeLists.txt`. New Python functionality = edit the right module.

| File | Phase | Changes |
|------|-------|---------|
| `gateway-pi5/gateway-code/dc_gateway.py` | 1 | Auto-reconnect loop, disconnect detection, reconnect guard |
| `gateway-pi5/gateway-code/power_manager.py` | 1 | `_paused` flag for reconnection |
| `gateway-pi5/gateway-code/tui_app.py` | 1 | Start reconnect loop from `connect_ble()` |
| `gateway-pi5/gateway-code/dc_gateway.py` | 2 | Failover logic (try all nodes), sensing_node_count adjust |
| `gateway-pi5/gateway-code/constants.py` | 2 | Add `"DC-Monitor"` to `DEVICE_NAME_PREFIXES` |
| `ESP/ESP-Mesh-Node-sensor-test/main/` | 2 | New modules: `gatt_service.c/h`, `command_parser.c/h` (ported from GATT gateway) |
| `ESP/ESP-Mesh-Node-sensor-test/main/mesh_node.c` | 2 | Add vendor CLIENT model alongside SERVER |
| `ESP/ESP-Mesh-Node-sensor-test/main/main.c` | 2 | Add `gatt_register_services()` + `gatt_start_advertising()` calls |
| `ESP/ESP-Mesh-Node-sensor-test/main/CMakeLists.txt` | 2 | Add new source files |
| `ESP/ESP-Provisioner/main/provisioning.c` | 2 | Handle dual vendor models on same node |
| `ESP/ESP_GATT_BLE_Gateway/` | 2 | **Deprecated** — kept for rollback |
