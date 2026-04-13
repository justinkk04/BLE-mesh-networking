# Prompt: Command Flow (TUI + Web UI) Flowchart

> **Usage:** Copy everything below the `---` line and paste it as a prompt in a new conversation.

---

## Task

Create a `.drawio` XML file for the **Command Flow** — showing the complete lifecycle of a command from user input (via TUI or Web UI) through BLE to the mesh node and back. Save it to `Documentation/Flowcharts/05-command-flow-tui-and-web.drawio`.

## Overview

This diagram shows two parallel entry paths (TUI and Web UI) that converge on the same backend, travel through BLE GATT to the ESP32-C6, get routed through the mesh, and return sensor data back to both interfaces. It's the single most important diagram for understanding data flow.

## Source Files
- `tui_app.py` — TUI command dispatch
- `web_server.py` — WebSocket command dispatch
- `dc_gateway.py` — BLE GATT client, notification handler
- `command_parser.c` — ESP-side command routing
- `command.c` — ESP-side command execution
- `mesh_node.c` — Mesh send/receive
- `gatt_service.c` — GATT characteristic handlers

## Path A: TUI Command (left lane)

1. **User types command** in Textual `Input` widget (e.g., `duty 50`)
2. `on_cmd_submitted()` → log "> duty 50" in cyan
3. `dispatch_command("duty 50")` — `@work(exclusive=True, group="cmd")`
4. **Preempt poll**: set `_poll_interrupt` (wakes poll loop so user command takes priority)
5. **Mark user command**: `mark_user_command(target_node)` — adds node to `_pending_user_nodes` set
6. Parse command → match to handler (e.g., `set_duty(target, 50)`)
7. All BLE calls go through `bt.submit_async()` — submits coroutine to BLE thread's event loop

## Path B: Web UI Command (right lane)

1. **User types command** in browser WebSocket console (e.g., `duty 50`)
2. Browser sends JSON via WebSocket: `{"type": "command", "command": "duty 50"}`
3. `websocket_endpoint()` receives message
4. `_execute_command("duty 50")` — `asyncio.create_task()`
5. **Preempt poll**: set `_poll_interrupt`
6. **Mark user command**: `mark_user_command(target_node)`
7. Parse command → match to handler (same dispatch logic as TUI: `read`, `stop`, `ramp`, `duty`, `threshold`, `priority`, `poll`, `node`, `help`)
8. Direct call to `gateway.set_duty(target, 50)` (already on BLE thread event loop)

## Shared Backend: `DCMonitorGateway` (middle, converged)

### Sending
1. `set_duty(node, 50)`:
   - Clamp to [0, 100]
   - If PowerManager active: `PM.set_target_duty(node, 50)` (update ceiling)
   - Call `send_to_node(node, "DUTY", "50")`
2. `send_to_node()`:
   - If `node == "ALL"`: format `"ALL:DUTY:50"`
   - Else: format `"1:DUTY:50"`
3. `send_command(cmd)`:
   - Check: not reconnecting, client connected
   - `async with _ble_cmd_lock:` (serialize GATT writes)
   - `client.write_gatt_char(COMMAND_CHAR_UUID, cmd.encode())`
   - Log "Sent: 1:DUTY:50"

## ESP32-C6 Side (bottom section)

### Receiving Command
1. **GATT Write** arrives on Command characteristic (UUID 0xDC02)
2. `command_access_cb()` → captures `conn_handle`
3. `process_gatt_command("1:DUTY:50", len)`:
   - Parse: `node_id = 1`, `command = "DUTY"`, `value = "50"`
   - Build `pico_cmd = "duty:50"`
   - Stop any active monitor

### Routing (decision diamond)
4. Diamond: **"Target is self?"** (`target_addr == node_state.addr`)
   - **Yes** → `process_local_and_notify("duty:50")`
     - `process_command("duty:50")` → `set_duty(50)` → format response `"D:50%,V:12.003V,I:250.00mA,P:3000.8mW"`
     - Format as `"NODE1:DATA:D:50%,V:12.003V,..."`
     - `gatt_notify_sensor_data()` → GATT notify to Pi 5
   - **No** → Diamond: **"ALL?"**
     - **Yes** → Process locally FIRST (group send doesn't reach local server) → THEN `send_vendor_command(0xC000, "duty:50")` to mesh group
     - **No** → `send_vendor_command(target_addr, "duty:50")` unicast
   - After mesh send: `gatt_notify_sensor_data("SENT:DUTY")`

### Remote Node Processing
5. Remote node receives via `custom_model_cb()` → `VND_OP_SEND`
6. Skip self-echo check
7. `process_command("duty:50")` → execute locally
8. Send `VND_OP_STATUS` response back with sensor data
9. Response arrives at connected node via `custom_model_cb()` → `VND_OP_STATUS`
10. Format as `"NODE<id>:DATA:<payload>"` → `gatt_notify_sensor_data()`

## Response Path: Back to User (upward arrows)

### GATT Notification → Pi 5
1. `notification_handler()` fires on bleak's callback thread
2. Chunked reassembly (if needed)
3. Parse `":DATA:"` → extract sensor values via regex
4. Add to `known_nodes`, store in `_last_readings`

### To TUI (left return arrow)
5. Post `SensorDataMsg` via `app.call_from_thread()` (thread-safe cross-thread posting)
6. `on_mesh_gateway_app_sensor_data_msg()` → `_update_node_table()` (update DataTable row)
7. If `is_user_response` or `poll_show_log` or `debug_mode`: write to RichLog
8. `update_status()` → refresh sidebar

### To Web UI (right return arrow)
5. `broadcast_sensor_data(node_id, data, user_triggered=True)` → WebSocket push to ALL connected browsers
6. `db.insert_reading(node_id, duty, voltage, current, power)` → SQLite time-series storage
7. Browser receives JSON: `{"type": "sensor_data", "node_id": "1", "data": {...}}`
8. Dashboard UI updates in real-time

## Auto-Poll Integration (annotation)
- Web auto-poll runs `ALL:READ` every N seconds (default 2s)
- Poll loop is interruptible: user commands preempt via `_poll_interrupt` event
- `_pending_user_nodes` distinguishes user-triggered vs poll-triggered responses
- User-triggered responses show in TUI log; poll responses only update the table silently

## Style
- **Two parallel entry lanes** at top: TUI (left, green), Web UI (right, purple)
- **Converged middle lane**: DCMonitorGateway (blue)
- **Bottom section**: ESP32-C6 (orange) — split into connected node and remote node
- **Return arrows** going back up to both TUI and Web UI
- Label every arrow with the data format (e.g., `"1:DUTY:50"`, `"NODE1:DATA:D:50%,..."`
- Decision diamonds for routing logic
- Dashed line separating Pi 5 from ESP32 (labeled "BLE GATT Connection")
- Dashed line separating connected node from remote node (labeled "BLE Mesh")
