# v0.7.0 Gateway Failover — CHANGELOG

## Phase 1: Pi 5 Auto-Reconnect (Feb 27, 2026)

**Goal:** Gateway automatically detects BLE disconnection and reconnects without manual restart.

### Changes

#### `dc_gateway.py`

- Added reconnect state fields: `_was_connected`, `_reconnecting`, `_last_connected_address`
- Added `_auto_reconnect_loop()` — background task checks connection every 2s, rescans + reconnects on disconnect
- Added reconnect guard on `send_command()` — returns early with warning during reconnection
- Set `_was_connected` and `_last_connected_address` on successful connect
- **Bug fix:** `connect_to_node()` now returns `False` if subscribe fails (wrong device — no DC01 service), disconnects, and lets the caller try the next device
- **Bug fix:** Uses `dangerous_use_bleak_cache=False` in `BleakClient` to force fresh GATT service discovery (prevents stale BlueZ cache)

#### `power_manager.py`

- Added `_paused` flag to `__init__`
- `poll_loop()` skips polling when `_paused` is `True` (set by reconnect loop during disconnection)

#### `tui_app.py`

- `connect_ble()` now iterates through ALL discovered devices until one connects AND subscribes successfully (eliminates need for `--address`)
- Target address device is tried first if `--address` is given
- Starts `_auto_reconnect_loop()` on the BLE thread after successful connection
- `on_unmount()` sets `gateway.running = False` before stopping BLE thread for clean shutdown

### Bugs Fixed (bonus)

- **`--address` no longer required** — gateway probes each discovered ESP-BLE-MESH device and connects to the first one with the DC01 GATT service
- **Stale GATT cache** — forced fresh service discovery prevents "Characteristic not found" errors after device reboots

---

## Phase 2: Universal Node Firmware (Feb 28, 2026)

**Goal:** Consolidate GATT gateway capabilities into the sensor node firmware, creating a single "Universal Node."

### New Files (ESP-Mesh-Node-sensor-test/main/)

- `gatt_service.c/h` — NimBLE GATT service (DC01), chunked notify, GAP events, advertising as "DC-Monitor"
- `command_parser.c/h` — Pi 5 command parsing, self-addressing (local processing when target == self), ALL: support
- `node_tracker.c/h` — Known node tracking with dedup and self-exclusion
- `monitor.c/h` — FreeRTOS timer-based continuous polling

### Modified Files

- `main.c` — Added GATT init calls (still a thin 69-line orchestrator)
- `mesh_node.c/h` — Added Vendor CLIENT model alongside SERVER (dual-role), vendor client state exports, GATT notify integration
- `CMakeLists.txt` — Added all 4 new .c files
- `sdkconfig.defaults` — Added `CONFIG_BT_NIMBLE_ENABLED=y` and `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=185` (was using Bluedroid, needed NimBLE for GATT APIs)

### Build Issues Encountered

- **`host/ble_hs.h: No such file or directory`** — `gatt_service.h` included NimBLE header unnecessarily; removed from `.h` (only needed in `.c`)
- **`sdkconfig` still had Bluedroid enabled** — the sensor node's `sdkconfig.defaults` was missing `CONFIG_BT_NIMBLE_ENABLED=y`. Fix required deleting `sdkconfig` + `build/` and running `idf.py set-target esp32c6 && idf.py build` to regenerate from updated defaults

### Verified

- Compiles cleanly with `idf.py build`
- Provisioner detects and binds all 4 models (OnOff SRV/CLI, Vendor SRV/CLI)
- `ALL:READ` returns sensor data from both NODE0 (local) and NODE1 (mesh)

### Known Bugs (to fix in Phase 3)

- NODE0 data appears twice on `ALL:READ` (local + mesh echo)
- `sensing_node_count` shows 1 instead of 2 (hardcoded `len(devices) - 1` assumes old dedicated gateway)
