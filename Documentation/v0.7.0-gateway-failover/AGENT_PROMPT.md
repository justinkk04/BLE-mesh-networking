# v0.7.0 Gateway Failover — Agent Prompt

> **Instructions:** Copy this entire prompt and pass it to a coding agent to implement Phase 1 (auto-reconnect) of the Gateway Failover feature.
> After Phase 1 is verified working, use the Phase 2 prompt below for firmware consolidation.

---

## Phase 1 Prompt (Python Only — No Firmware Changes)

```
You are implementing Phase 1 of the BLE Mesh Gateway Failover feature (v0.7.0).

REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make `gateway.py` automatically detect BLE disconnection and reconnect without requiring a manual restart.

**Context:** Read these files first:
- `Documentation/v0.7.0-gateway-failover/GATEWAY_FAILOVER_PLAN.md` — high-level design
- `Documentation/v0.7.0-gateway-failover/GATEWAY_FAILOVER_IMPLEMENTATION.md` — Section 2 (Phase 1 details)
- `gateway-pi5/gateway.py` — current implementation (~2072 lines)
- `MESH_IMPLEMENTATION.md` — system architecture reference

**Architecture:** Pi 5 runs `gateway.py` which connects to an ESP32-C6 GATT Gateway via BLE (service 0xDC01). Currently, if the gateway ESP loses power or disconnects, gateway.py hangs/crashes and must be manually restarted.

**Tech Stack:** Python 3, bleak (BLE library), textual (TUI framework)

---

### Task 1: Add Reconnection State Fields

**Files:**
- Modify: `gateway-pi5/gateway.py` — `DCMonitorGateway.__init__()` (around line 694)

**Step 1:** Add these fields to `__init__()`:

```python
self._was_connected = False
self._reconnecting = False
self._last_connected_address = None
```

**Step 2:** In `connect_to_node()` (around line 1026), before `return True`, add:

```python
self._was_connected = True
self._last_connected_address = device.address
```

**Step 3:** Verify syntax:

```bash
python -c "import py_compile; py_compile.compile('gateway-pi5/gateway.py', doraise=True); print('OK')"
```

---

### Task 2: Add Pause Flag to PowerManager

**Files:**

- Modify: `gateway-pi5/gateway.py` — `PowerManager.__init__()` and `PowerManager.poll_loop()`

**Step 1:** In `PowerManager.__init__()` (around line 161-171), add:

```python
self._paused = False
```

**Step 2:** In `PowerManager.poll_loop()` (around line 391-419), at the TOP of the `while` loop body, add:

```python
if self._paused:
    await asyncio.sleep(1.0)
    continue
```

**Step 3:** Verify syntax.

---

### Task 3: Add Auto-Reconnect Loop

**Files:**

- Modify: `gateway-pi5/gateway.py` — `DCMonitorGateway` class

**Step 1:** Add `_auto_reconnect_loop()` method after `_dashboard_poll_loop()` (after line ~1274).

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

### Task 4: Guard send_command() During Reconnect

**Files:**

- Modify: `gateway-pi5/gateway.py` — `DCMonitorGateway.send_command()` (around line 1040)

**Step 1:** Add guard at the top of `send_command()`:

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

### Task 5: Start Reconnect Loop in TUI

**Files:**

- Modify: `gateway-pi5/gateway.py` — `MeshGatewayApp`

**Step 1:** In `connect_ble()` (around line 1521-1565), after the successful connection block, submit the reconnect loop to the BLE thread:

```python
self.gateway._ble_thread.submit(self.gateway._auto_reconnect_loop())
```

**Step 2:** Verify syntax.

**Step 3:** Commit:

```bash
git add gateway-pi5/gateway.py
git commit -m "feat(gateway.py): auto-reconnect on BLE disconnect (v0.7.0 Phase 1)"
```

---

### Task 6: Test Auto-Reconnect

**Step 1:** Deploy to Pi 5 and start gateway:

```bash
scp gateway-pi5/gateway.py pi@<pi-ip>:~/gateway-pi5/
ssh pi@<pi-ip>
cd gateway-pi5
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

**Step 4:** Commit and tag:

```bash
git add -A
git commit -m "v0.7.0-phase1: auto-reconnect on BLE disconnect"
git tag v0.7.0-phase1
```

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
- `ESP/ESP_GATT_BLE_Gateway/main/main.c` — GATT Gateway firmware (1036 lines) — you are porting FROM this
- `ESP/ESP-Mesh-Node-sensor-test/main/main.c` — Sensor Node firmware (781 lines) — you are porting INTO this
- `ESP/ESP-Provisioner/main/main.c` — Provisioner (868 lines) — may need minor changes
- `gateway-pi5/gateway.py` — Python gateway (~2072 lines) — failover logic
- `MESH_IMPLEMENTATION.md` — full architecture reference

**Architecture:** Currently, sensor nodes have Vendor SERVER model (receives commands, responds with sensor data) and the dedicated GATT gateway has Vendor CLIENT model (sends commands to nodes) + GATT service (Pi 5 connects here). You are merging both roles into one firmware.

**Tech Stack:** ESP-IDF 5.x (C), BLE Mesh, NimBLE GATT, Python 3 + bleak

**CRITICAL INIT ORDER:** GATT services MUST be registered BEFORE `esp_ble_mesh_init()` — mesh init locks the GATT table. This is already proven working in the GATT gateway code.

**Key architectural decisions:**

1. The consolidated node has BOTH VND_MODEL_ID_SERVER and VND_MODEL_ID_CLIENT
2. When Pi 5 connects via GATT, the node acts as gateway (forwards commands to mesh)
3. When a GATT command targets the node itself, it processes locally (no mesh roundtrip)
4. GATT advertising name: "DC-Monitor" (update gateway.py scan matching)
5. ESP_GATT_BLE_Gateway directory is DEPRECATED but kept for rollback

**IMPORTANT:** This requires erasing flash on ALL ESP devices and reprovisioning from scratch.

Refer to Section 3 of GATEWAY_FAILOVER_IMPLEMENTATION.md for exact code changes per subsection.

```
