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
