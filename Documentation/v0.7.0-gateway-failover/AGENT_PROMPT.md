# v0.7.0 Gateway Failover — Agent Prompt

> **Instructions:** Copy this entire prompt and pass it to a coding agent to implement Phase 1 (auto-reconnect) of the Gateway Failover feature.
> After Phase 1 is verified working, use the Phase 2 prompt below for firmware consolidation.

---

## Phase 1 Prompt (Python Only — No Firmware Changes)

```
You are implementing Phase 1 of the BLE Mesh Gateway Failover feature (v0.7.0).

REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make the gateway automatically detect BLE disconnection and reconnect without requiring a manual restart.

**Context:** Read these files first:
- `Documentation/v0.7.0-gateway-failover/GATEWAY_FAILOVER_PLAN.md` — high-level design
- `Documentation/v0.7.0-gateway-failover/GATEWAY_FAILOVER_IMPLEMENTATION.md` — Section 2 (Phase 1 details)
- `MESH_IMPLEMENTATION.md` — system architecture reference

Then read the MODULAR Python gateway code:
- `gateway-pi5/gateway-code/gateway.py` — entry point (~140 lines)
- `gateway-pi5/gateway-code/dc_gateway.py` — DCMonitorGateway class (~480 lines) — MAIN TARGET
- `gateway-pi5/gateway-code/power_manager.py` — PowerManager class (~540 lines)
- `gateway-pi5/gateway-code/tui_app.py` — MeshGatewayApp TUI (~470 lines)
- `gateway-pi5/gateway-code/constants.py` — UUIDs, regex (~17 lines)
- `gateway-pi5/gateway-code/ble_thread.py` — BLE I/O thread (~68 lines)
- `gateway-pi5/gateway-code/node_state.py` — NodeState dataclass (~19 lines)

**Architecture:** Pi 5 runs a Python gateway that connects to an ESP32-C6 GATT Gateway via BLE (service 0xDC01). Currently, if the gateway ESP loses power or disconnects, the gateway hangs/crashes and must be manually restarted.

**Tech Stack:** Python 3, bleak (BLE library), textual (TUI framework)

---

### CRITICAL: Modular Code Structure (v0.6.2)

> The codebase was refactored into single-responsibility modules in v0.6.2.
> DO NOT create monolithic files. Each class lives in its own module:
>
> | Module | Contains |
> |--------|----------|
> | `dc_gateway.py` | `DCMonitorGateway` class |
> | `power_manager.py` | `PowerManager` class |
> | `tui_app.py` | `MeshGatewayApp` class |
> | `ble_thread.py` | `BleThread` class |
> | `node_state.py` | `NodeState` dataclass |
> | `constants.py` | UUIDs, regex, device prefixes |
> | `gateway.py` | Entry point (main + _run_cli) |
>
> Changes go to the CORRECT module. Do not combine classes back into one file.

---

### Task 1: Add Reconnection State Fields to `dc_gateway.py`

**File:** `gateway-pi5/gateway-code/dc_gateway.py`

**Step 1:** Add these fields to `DCMonitorGateway.__init__()`:

```python
self._was_connected = False
self._reconnecting = False
self._last_connected_address = None
```

**Step 2:** In `connect_to_node()`, before `return True`, add:

```python
self._was_connected = True
self._last_connected_address = device.address
```

**Step 3:** Verify syntax:

```bash
python -c "import py_compile; py_compile.compile('gateway-pi5/gateway-code/dc_gateway.py', doraise=True); print('OK')"
```

---

### Task 2: Add Pause Flag to `power_manager.py`

**File:** `gateway-pi5/gateway-code/power_manager.py`

**Step 1:** In `PowerManager.__init__()`, add:

```python
self._paused = False
```

**Step 2:** In `PowerManager.poll_loop()`, at the TOP of the `while` loop body, add:

```python
if self._paused:
    await asyncio.sleep(1.0)
    continue
```

**Step 3:** Verify syntax.

---

### Task 3: Add Auto-Reconnect Loop to `dc_gateway.py`

**File:** `gateway-pi5/gateway-code/dc_gateway.py`

**Step 1:** Add `_auto_reconnect_loop()` method to `DCMonitorGateway` class.

See Section 2.2 of `GATEWAY_FAILOVER_IMPLEMENTATION.md` for the full implementation.

Key behavior:

- Check `self.client.is_connected` every 2 seconds
- On first disconnect detection: log, set `_reconnecting = True`, pause PM
- Rescan for gateway (try last address first, then any)
- Reconnect and resubscribe to notifications
- On success: clear reconnection state, resume PM
- On failure: retry every 5 seconds

**Step 2:** Verify syntax.

---

### Task 4: Guard send_command() During Reconnect in `dc_gateway.py`

**File:** `gateway-pi5/gateway-code/dc_gateway.py`

**Step 1:** Add guard at the top of `DCMonitorGateway.send_command()`:

```python
if self._reconnecting:
    self.log("[WARN] Cannot send — reconnecting...", style="yellow")
    return
if self.client is None or not self.client.is_connected:
    self.log("[WARN] Not connected", style="yellow")
    return
```

**Step 2:** Verify syntax.

---

### Task 5: Start Reconnect Loop from `tui_app.py`

**File:** `gateway-pi5/gateway-code/tui_app.py`

**Step 1:** In `MeshGatewayApp.connect_ble()`, after the successful connection block, submit the reconnect loop:

```python
self._ble_thread.submit(self.gateway._auto_reconnect_loop())
```

**Step 2:** Verify syntax.

---

### Task 6: Test Auto-Reconnect

**Step 1:** Deploy to Pi 5 and start gateway:

```bash
scp -r gateway-pi5/gateway-code/ pi@<pi-ip>:~/gateway-pi5/gateway-code/
ssh pi@<pi-ip>
cd gateway-pi5/gateway-code
python gateway.py
```

**Step 2:** Connect normally, verify sensor data flows.

**Step 3:** Power-cycle the GATT Gateway ESP32-C6 (unplug USB, wait 5s, plug back in).

**Verify:**

- TUI shows `[RECONNECT] Connection lost!` message
- TUI shows `[RECONNECT] Attempting reconnect...`
- After ESP powers back up: `[RECONNECT] Reconnected successfully!`
- Commands work normally after reconnection
- PM resumes if it was active

```

---

## Phase 2 Prompt (ESP Firmware + Python — Use After Phase 1 Is Verified)

```

You are implementing Phase 2 of the BLE Mesh Gateway Failover feature (v0.7.0).

REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Consolidate the GATT Gateway and Sensor Node into a single "Universal Node" firmware, so any sensor node can act as the Pi 5's gateway. This eliminates the dedicated GATT Gateway as a single point of failure.

**Context:** Read these files FIRST:

- `Documentation/v0.7.0-gateway-failover/GATEWAY_FAILOVER_PLAN.md` — high-level design
- `Documentation/v0.7.0-gateway-failover/GATEWAY_FAILOVER_IMPLEMENTATION.md` — Section 3-6 (Phase 2 details)
- `Documentation/v0.6.2-classes-cleanup/CHANGELOG.md` — modular structure reference
- `MESH_IMPLEMENTATION.md` — full architecture reference

Then read the MODULAR ESP firmware:

### GATT Gateway (porting FROM — reference only)

- `ESP/ESP_GATT_BLE_Gateway/main/gatt_service.c/h` — GATT service definition, callbacks, advertising, chunked notify
- `ESP/ESP_GATT_BLE_Gateway/main/mesh_gateway.c/h` — Vendor CLIENT model, send_vendor_command
- `ESP/ESP_GATT_BLE_Gateway/main/command_parser.c/h` — Pi 5 command parsing
- `ESP/ESP_GATT_BLE_Gateway/main/monitor.c/h` — Continuous monitoring task
- `ESP/ESP_GATT_BLE_Gateway/main/node_tracker.c/h` — Known node tracking

### Sensor Node (porting INTO — primary target)

- `ESP/ESP-Mesh-Node-sensor-test/main/main.c` — THIN orchestrator (DO NOT bloat!)
- `ESP/ESP-Mesh-Node-sensor-test/main/mesh_node.c/h` — Mesh composition, vendor SERVER model
- `ESP/ESP-Mesh-Node-sensor-test/main/sensor.c/h` — I2C sensor reads
- `ESP/ESP-Mesh-Node-sensor-test/main/load_control.c/h` — PWM control
- `ESP/ESP-Mesh-Node-sensor-test/main/command.c/h` — Command processing
- `ESP/ESP-Mesh-Node-sensor-test/main/CMakeLists.txt` — MUST UPDATE with new files

### Pi 5 Gateway (modular Python)

- `gateway-pi5/gateway-code/dc_gateway.py` — Add failover logic
- `gateway-pi5/gateway-code/constants.py` — Add "DC-Monitor" to name prefixes

---

### CRITICAL: Modular Code Structure (v0.6.2)

> The codebase was refactored into single-responsibility modules in v0.6.2.
>
> **ESP rules:**
>
> - `main.c` is a THIN ORCHESTRATOR (~50-90 lines). Only add init function calls to it.
> - Create NEW `.c`/`.h` module pairs for new functionality (e.g. `gatt_service.c/h`)
> - Update `CMakeLists.txt` SRCS list for every new `.c` file
> - Add vendor CLIENT model definitions to `mesh_node.c/h` (same file as SERVER model)
>
> **Python rules:**
>
> - Each class lives in its own module. Changes go to the CORRECT module file.
> - `dc_gateway.py` = DCMonitorGateway, `constants.py` = shared constants, etc.

**Architecture:** Currently, sensor nodes have Vendor SERVER model (receives commands, responds with sensor data) and the dedicated GATT gateway has Vendor CLIENT model (sends commands to nodes) + GATT service (Pi 5 connects here). You are merging both roles into one firmware.

**Tech Stack:** ESP-IDF 5.x (C), BLE Mesh, NimBLE GATT, Python 3 + bleak

**CRITICAL INIT ORDER:** GATT services MUST be registered BEFORE `esp_ble_mesh_init()` — mesh init locks the GATT table. This is already proven working in the GATT gateway code.

**Key architectural decisions:**

1. The consolidated node has BOTH VND_MODEL_ID_SERVER and VND_MODEL_ID_CLIENT
2. When Pi 5 connects via GATT, the node acts as gateway (forwards commands to mesh)
3. When a GATT command targets the node itself, it processes locally (no mesh roundtrip)
4. GATT advertising name: "DC-Monitor" (update constants.py scan matching)
5. ESP_GATT_BLE_Gateway directory is DEPRECATED but kept for rollback
6. New functionality goes into NEW MODULE FILES, not into main.c

**IMPORTANT:** This requires erasing flash on ALL ESP devices and reprovisioning from scratch.

Refer to Section 3 of GATEWAY_FAILOVER_IMPLEMENTATION.md for exact code changes per subsection.

```
