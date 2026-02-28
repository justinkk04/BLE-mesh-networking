# v0.7.0 Gateway Failover Plan

**Date:** February 25, 2026 (Updated Feb 27)
**Author:** Justin Kwarteng
**Status:** Draft — Awaiting Review

---

## 1. Problem Statement

The current architecture uses a **dedicated GATT Gateway ESP32-C6** that bridges the Pi 5 to the mesh network. If this gateway loses power, disconnects, or crashes:

- `gateway.py` hangs or crashes — the user must manually restart it
- The entire mesh network becomes unreachable from the Pi 5
- No sensor data flows, no commands can be sent
- There is no auto-reconnect or failover mechanism

This is a **single point of failure** in the v0.6.2 architecture:

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

## 3. Four-Phase Approach

> [!IMPORTANT]
> Each phase is independently testable. Complete and verify each phase before starting the next.
> Phases are ordered by dependency — later phases build on earlier ones.

---

### Phase 1: Pi 5 Auto-Reconnect (Python only — no firmware changes)

**Scope:** Modify `gateway.py` modules to detect BLE disconnection and auto-reconnect to the same GATT gateway when it comes back online.

**What changes:**

| File | Change |
|------|--------|
| `gateway-pi5/gateway-code/dc_gateway.py` | Add `_auto_reconnect_loop()`, reconnect state fields, `send_command()` guard |
| `gateway-pi5/gateway-code/power_manager.py` | Add `_paused` flag to pause polling during reconnection |
| `gateway-pi5/gateway-code/tui_app.py` | Start reconnect loop from `connect_ble()` |

**Risk:** 🟢 Low — Python-only, no firmware changes, no reprovisioning
**Effort:** ~100 lines of Python across 3 modules

#### How to Test Phase 1

1. Deploy updated Python code to Pi 5
2. Start `gateway.py`, connect to the GATT gateway
3. Run `ALL:READ` — verify sensor data comes back normally
4. **Power-cycle the GATT Gateway ESP32** (unplug USB, wait 5s, plug back)
5. Watch the TUI — you should see:

   ```
   [RECONNECT] Connection lost! Attempting reconnect...
   [RECONNECT] PowerManager paused
   [RECONNECT] No gateway found, retrying in 5s...    ← while ESP is still booting
   [RECONNECT] Reconnected successfully!
   [RECONNECT] PowerManager resumed
   ```

6. Run `ALL:READ` again — verify data flows normally after reconnection
7. If PM was active (`threshold 5000`), verify it resumes balancing

**Phase 1 is DONE when:** Gateway auto-reconnects to the **same** GATT gateway after a power-cycle, without you touching anything.

---

### Phase 2: Universal Node Firmware (ESP C code — no Python changes)

**Scope:** Port the GATT Gateway's capabilities into the Sensor Node firmware, creating a single "Universal Node" that can both sense AND act as a GATT gateway.

**What changes — ESP side only:**

| File | Change |
|------|--------|
| `ESP/ESP-Mesh-Node-sensor-test/main/gatt_service.c/h` | **[NEW]** GATT service 0xDC01, read/write callbacks, advertising, chunked notify (ported from GATT Gateway) |
| `ESP/ESP-Mesh-Node-sensor-test/main/command_parser.c/h` | **[NEW]** `process_gatt_command()` — maps Pi 5 commands to mesh/local commands (ported from GATT Gateway) |
| `ESP/ESP-Mesh-Node-sensor-test/main/node_tracker.c/h` | **[NEW]** `register_known_node()` — tracks responding mesh nodes (ported from GATT Gateway) |
| `ESP/ESP-Mesh-Node-sensor-test/main/monitor.c/h` | **[NEW]** FreeRTOS timer for continuous polling mode (ported from GATT Gateway) |
| `ESP/ESP-Mesh-Node-sensor-test/main/mesh_node.c/h` | Add vendor CLIENT model alongside existing SERVER, dual-role `custom_model_cb()`, send serialization vars |
| `ESP/ESP-Mesh-Node-sensor-test/main/main.c` | Add `gatt_register_services()` + `gatt_start_advertising()` init calls (thin orchestrator — 2 new lines) |
| `ESP/ESP-Mesh-Node-sensor-test/main/CMakeLists.txt` | Add new `.c` files to SRCS list |

**Key technical details:**

- **Init order:** GATT services registered BEFORE `esp_ble_mesh_init()` (mesh locks the GATT table)
- **Self-addressing:** When `0:READ` targets the connected node itself, process locally (no mesh roundtrip)
- **Advertising name:** `"DC-Monitor"` instead of `"Mesh-Gateway"`
- **Dual vendor models:** Both `VND_MODEL_ID_SERVER` (0x0001) and `VND_MODEL_ID_CLIENT` (0x0000) on same element

**Risk:** 🟡 Medium — Significant firmware changes, requires reprovisioning, potential memory/BLE stack conflicts
**Effort:** ~400 lines of C across new modules

#### How to Test Phase 2

> [!CAUTION]
> This requires erasing flash and reprovisioning ALL ESP32-C6 devices.

1. Build the updated sensor node firmware:

   ```bash
   cd ESP/ESP-Mesh-Node-sensor-test
   idf.py build
   ```

   **Check:** It compiles with no errors.

2. Erase + flash the Provisioner (clean state):

   ```bash
   cd ESP/ESP-Provisioner
   idf.py erase-flash
   idf.py build flash monitor
   ```

3. Erase + flash one universal node:

   ```bash
   cd ESP/ESP-Mesh-Node-sensor-test
   idf.py erase-flash
   idf.py flash monitor
   ```

4. Watch the Provisioner serial output — verify it provisions successfully and binds **both** vendor models:

   ```
   Vendor Server bound on 0x0005
   Vendor Client bound on 0x0005    ← THIS IS NEW — must appear
   Subscribing Vnd Server on 0x0005 to group 0xC000
   Group subscription added on 0x0005
   ========== NODE 0x0005 FULLY CONFIGURED ==========
   ```

5. **Connect to the universal node from the Pi 5** (using the existing `gateway.py` unchanged — it should find the node's GATT service by UUID):

   ```bash
   python gateway.py
   ```

   If the scan doesn't find it by name, use `--address <MAC>` to force-connect.

6. Send commands through the GATT connection:
   - `0:READ` → should get sensor data back (self-addressed, local processing)
   - If a second node is provisioned: `1:READ` → should get data via mesh relay

**Phase 2 is DONE when:** A single universal node provisions with both vendor models, connects to the Pi 5 via GATT, and responds to `0:READ` with real sensor data.

---

### Phase 3: Python Failover Logic (Python only — uses Phase 2 firmware)

**Scope:** Update `gateway.py` modules so the Pi 5 can discover and connect to ANY universal node, and fail over to a different node when the connected one dies.

**What changes:**

| File | Change |
|------|--------|
| `gateway-pi5/gateway-code/constants.py` | Add `"DC-Monitor"` to `DEVICE_NAME_PREFIXES` |
| `gateway-pi5/gateway-code/dc_gateway.py` | Extend `_auto_reconnect_loop()` with failover (try ALL discovered nodes, not just the last one) |
| `gateway-pi5/gateway-code/dc_gateway.py` | Adjust `sensing_node_count` when connected node is also a sensor |

**Risk:** 🟢 Low — Python-only, builds on Phase 1 reconnect logic
**Effort:** ~50 lines of Python across 2 modules

#### How to Test Phase 3

> Requires at least 2 universal nodes provisioned and running (from Phase 2).

1. Deploy updated Python code to Pi 5
2. Start `gateway.py` — it should discover nodes by `"DC-Monitor"` name
3. Note which node it connects to (call it Node A)
4. Run `ALL:READ` — verify all nodes respond
5. **Power-cycle Node A** (unplug USB)
6. Watch the TUI:

   ```
   [RECONNECT] Connection lost! Attempting reconnect...
   [FAILOVER] Connected to DC-Monitor-XX:XX  ← different node (Node B)
   ```

7. Run `ALL:READ` again — remaining nodes should respond through Node B
8. Power Node A back on — it should rejoin the mesh (verify with `ALL:READ`)

**Phase 3 is DONE when:** Killing the connected node causes the Pi 5 to automatically connect to a **different** node and resume normal operation.

---

### Phase 4: Full Integration Test & Cleanup

**Scope:** End-to-end validation of the complete failover system. No new code — just testing and cleanup.

#### Integration Test Checklist

- [ ] **3+ universal nodes** provisioned and running
- [ ] Pi 5 connects to any node automatically
- [ ] `ALL:READ` returns data from all nodes
- [ ] `0:READ`, `1:READ`, `2:READ` each return correct node data
- [ ] `0:DUTY:50` sets duty on the correct node
- [ ] Power-cycle connected node → Pi 5 fails over to another node within ~10s
- [ ] Commands work normally after failover
- [ ] Power-cycle the NEW connected node → Pi 5 fails over AGAIN to a third node
- [ ] `threshold 5000` → PM balances load across all reachable nodes
- [ ] PM pauses during reconnection, resumes after
- [ ] Original nodes rejoin mesh when powered back on
- [ ] `ALL:READ` shows all nodes again after they rejoin

#### Cleanup

- [ ] Mark `ESP/ESP_GATT_BLE_Gateway/` as deprecated in README (keep for rollback)
- [ ] Update `MESH_IMPLEMENTATION.md` to reflect the new architecture
- [ ] Tag the repo as `v0.7.0`

**Phase 4 is DONE when:** All checklist items pass. The dedicated GATT gateway is no longer needed.

---

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

## 6. Rollback

- **Phase 1:** Revert `gateway.py` changes — no firmware impact
- **Phase 2:** Reflash original sensor node firmware — the old dedicated GATT gateway still exists as a fallback
- **Phase 3:** Revert Python changes — Phase 1 reconnect still works with old GATT gateway

## 7. Phase Summary

| Phase | What | Risk | Effort | Depends On |
|-------|------|------|--------|------------|
| **1** | Pi 5 auto-reconnect (Python) | 🟢 Low | ~100 LOC Python | Nothing |
| **2** | Universal Node firmware (ESP C) | 🟡 Medium | ~400 LOC C | Nothing (but test after Phase 1 is merged) |
| **3** | Python failover logic | 🟢 Low | ~50 LOC Python | Phase 1 + Phase 2 |
| **4** | Integration test + cleanup | 🟢 Low | No new code | Phase 1 + 2 + 3 |

## 8. Files Changed Summary (All Phases)

> [!IMPORTANT]
> **Modular rule:** All changes go into the correct module file. `main.c` stays as a thin orchestrator.
> New ESP functionality = new `.c`/`.h` pair + update `CMakeLists.txt`. New Python functionality = edit the right module.

| File | Phase | Changes |
|------|-------|---------|
| `gateway-pi5/gateway-code/dc_gateway.py` | 1 | Auto-reconnect loop, disconnect detection, reconnect guard |
| `gateway-pi5/gateway-code/power_manager.py` | 1 | `_paused` flag for reconnection |
| `gateway-pi5/gateway-code/tui_app.py` | 1 | Start reconnect loop from `connect_ble()` |
| `ESP/ESP-Mesh-Node-sensor-test/main/gatt_service.c/h` | 2 | **[NEW]** Ported from GATT Gateway |
| `ESP/ESP-Mesh-Node-sensor-test/main/command_parser.c/h` | 2 | **[NEW]** Ported from GATT Gateway |
| `ESP/ESP-Mesh-Node-sensor-test/main/node_tracker.c/h` | 2 | **[NEW]** Ported from GATT Gateway |
| `ESP/ESP-Mesh-Node-sensor-test/main/monitor.c/h` | 2 | **[NEW]** Ported from GATT Gateway |
| `ESP/ESP-Mesh-Node-sensor-test/main/mesh_node.c/h` | 2 | Add vendor CLIENT model alongside SERVER |
| `ESP/ESP-Mesh-Node-sensor-test/main/main.c` | 2 | Add 2 new init calls |
| `ESP/ESP-Mesh-Node-sensor-test/main/CMakeLists.txt` | 2 | Add new source files |
| `gateway-pi5/gateway-code/constants.py` | 3 | Add `"DC-Monitor"` to `DEVICE_NAME_PREFIXES` |
| `gateway-pi5/gateway-code/dc_gateway.py` | 3 | Failover logic (try all nodes), sensing_node_count adjust |
| `ESP/ESP_GATT_BLE_Gateway/` | 4 | **DEPRECATED** — kept for rollback |
