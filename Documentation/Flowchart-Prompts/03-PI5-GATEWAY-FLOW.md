# Prompt: Pi 5 Gateway Startup & Runtime Flowchart

> **Usage:** Copy everything below the `---` line and paste it as a prompt in a new conversation.

---

## Task

Create a `.drawio` XML file for the **Raspberry Pi 5 Gateway Startup & Runtime Flow**. Save it to `Documentation/Flowcharts/03-pi5-gateway-flow.drawio`.

## Source Files (all in `gateway-pi5/gateway-code/`)
- `gateway.py`, `dc_gateway.py`, `tui_app.py`, `ble_thread.py`, `web_server.py`, `power_manager.py`, `db.py`, `node_state.py`, `constants.py`

## Entry Point: `gateway.py → main()`

### Argument Parsing
Show a box: `argparse` with key flags:
- `--scan`, `--address`, `--node` (0-9 or ALL), `--duty`, `--ramp`, `--stop`, `--status`, `--read`, `--monitor`
- `--no-tui`, `--web`, `--web-only`, `--web-port`

### Mode Selection (3-way decision diamond)

Diamond: **"Which mode?"**

**Path A: Web-Only Mode** (`--web-only`)
1. Create `DCMonitorGateway()`
2. Set `_web_enabled = True`, `_web_poll_requested = True`
3. Create `BleThread()` and `bt.start()`
4. `db.init_db()`, `web_server.set_gateway(gateway)`
5. `startup_and_serve()`:
   - `scan_for_nodes()` → try each device → `connect_to_node()`
   - Start `_auto_reconnect_loop()`
   - Start `uvicorn.Server` on `0.0.0.0:8000`
6. Running state: Web dashboard + BLE polling active

**Path B: TUI Mode** (default, `_HAS_TEXTUAL and not one-shot and not --no-tui`)
1. Create `DCMonitorGateway()`
2. If `--web`: init `db`, `web_server.set_gateway()`, set web flags
3. Create `MeshGatewayApp(gateway, ...)`
4. `app.run()` (Textual event loop takes over)
5. Inside `on_mount()`:
   - Start `BleThread()`
   - If `--web`: start uvicorn on BLE thread
   - `connect_ble()` worker:
     - `scan_for_nodes()` via BLE thread
     - Try each device: `connect_to_node()` — skip devices without GATT service
     - Set `sensing_node_count = len(devices)`
     - Start `_auto_reconnect_loop()` on BLE thread
6. Running state: TUI + optional web dashboard

**Path C: One-Shot / Legacy CLI** (`--no-tui` or one-shot flags like `--ramp`, `--duty`, etc.)
1. `asyncio.run(_run_cli(args, node))`
2. Create `DCMonitorGateway()`
3. `scan_for_nodes()` → select device → `connect_to_node()`
4. Execute single command (stop/duty/ramp/status/read/monitor)
5. `disconnect()` and exit

## BLE Connection Flow (detail sub-section)

### `scan_for_nodes()`
1. `BleakScanner.discover(timeout=10s, return_adv=True)`
2. Match by: target address (if `--address`), OR name prefix (`DC-Monitor`, `ESP-BLE-MESH`), OR service UUID (0xDC01)
3. Return list of discovered devices

### `connect_to_node(device)`
1. Create `BleakClient(device.address)`
2. `client.connect()`
3. `start_notify(SENSOR_DATA_CHAR_UUID, notification_handler)` — if this fails, device lacks GATT service → skip
4. Report negotiated MTU
5. Set `_was_connected = True`, store `_last_connected_address`
6. If `_web_poll_requested`: auto-start poll loop
7. Return success

## Notification Handler: `notification_handler()` (key runtime path)

Show this as a detailed sub-flowchart:
1. Receive `data` bytearray from BLE notification callback (runs on bleak's thread, NOT Textual thread)
2. Decode UTF-8
3. **Chunked reassembly**: starts with `'+'`? → accumulate in `_chunk_buf`, return. Final chunk: combine with buffer.
4. Decision: **"Message type?"**
   - **`:DATA:` present** → Parse `NODE<id>:DATA:<payload>`
     - Regex extract: `node_id`, `duty`, `voltage`, `current`, `power`
     - Add to `known_nodes` set
     - Store in `_last_readings[node_id]`
     - Feed `PowerManager.on_sensor_data()`
     - Signal `_node_events[node_id]` (threading.Event)
     - Check if user-triggered response (`_pending_user_nodes`)
     - If web enabled: `broadcast_sensor_data()` + `db.insert_reading()`
     - Post `SensorDataMsg` to TUI via `call_from_thread()`
   - **`ERROR:` prefix** → If PM polling: swallow. Else: log in red.
   - **`SENT:` prefix** → Debug mode only log.
   - **`MESH_READY` prefix** → Log.
   - **`TIMEOUT:` prefix** → If PM polling: swallow. Else: log in yellow.
   - **Other** → Log as-is.

## Command Dispatch (TUI)

`dispatch_command(cmd)` decision tree:
- `q/quit/exit` → disable PM → disconnect → stop BLE thread → exit
- `node <id>` → switch target
- `s/stop` → `stop_node(target)`
- `r/ramp` → `start_ramp(target)`
- `status` → `read_status(target)`
- `read` → `read_sensor(target)`
- `duty <N>` → `set_duty(target, N)`
- `<bare number>` → `set_duty(target, N)`
- `threshold <mW>` → create PowerManager, `set_threshold()`, start `poll_loop()`
- `threshold off` → `PM.disable()`, cancel power_poll worker
- `priority <id>` / `priority off` → `PM.set_priority()` / `PM.clear_priority()`
- `poll <sec>` → `start_web_poll(interval)` / `poll stop` → `stop_web_poll()`
- `power` → `PM.status()` display
- `debug/d` → toggle debug mode (F2)
- `help` → show help text

## Style
- Top-down flow from `main()` to mode selection
- Three parallel vertical lanes for the three modes
- Blue for BLE operations, green for TUI, orange for Web, purple for Power Manager
- Decision diamonds for mode selection and message type routing
- Note boxes for threading model (bleak thread vs Textual thread vs BLE thread)
