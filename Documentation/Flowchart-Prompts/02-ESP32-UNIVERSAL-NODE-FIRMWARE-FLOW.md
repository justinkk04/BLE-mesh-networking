# Prompt: ESP32-C6 Universal Node Firmware Flowchart

> **Usage:** Copy everything below the `---` line and paste it as a prompt in a new conversation.

---

## Task

Create a `.drawio` XML file for the **ESP32-C6 Universal Node Firmware Flow** — the boot sequence and runtime event-driven processing. Save it to `Documentation/Flowcharts/02-esp32-universal-node-firmware-flow.drawio`.

## Source Files (all in `ESP/ESP-Mesh-Node-sensor-universal/main/`)

- `main.c`, `mesh_node.c`, `gatt_service.c`, `command_parser.c`, `command.c`, `sensor.c`, `load_control.c`, `monitor.c`, `node_tracker.c`, `nvs_store.c`

## Boot Sequence (top-down linear flow)

Show these as a vertical chain of boxes with arrows:

1. **`app_main()`** starts
2. **NVS Init** — `nvs_flash_init()`, erase if no free pages
3. **NVS Handle Open** — `ble_mesh_nvs_open(&NVS_HANDLE)`
4. **Bluetooth Init** — `bluetooth_init()`
5. **Generate UUID** — `ble_mesh_get_dev_uuid(dev_uuid)` (prefix 0xdd 0xdd for auto-provisioning)
6. **Sensor Init** — `sensor_init()` (INA260 I2C setup)
7. **PWM Init** — `pwm_init()` (LEDC channel + timer)
8. **GATT Register** — `gatt_register_services()` — **MUST happen BEFORE mesh init** (mesh locks GATT table). Service UUID 0xDC01, two characteristics: Sensor Data (read+notify), Command (write).
9. **Mesh Init** — `ble_mesh_init()`:
   - Registers callbacks: `provisioning_cb`, `config_server_cb`, `generic_server_cb`, `generic_client_cb`, `custom_model_cb`
   - `esp_ble_mesh_init(&provision, &composition)`
   - `esp_ble_mesh_client_model_init(&vnd_models[1])`
   - Enables provisioning: PB-ADV + PB-GATT
10. **GATT Start Advertising** — `gatt_start_advertising()` — advertises as "DC-Monitor"
11. **Console Task** — `xTaskCreate(console_task, ...)` — FreeRTOS task for serial debug commands
12. **Node Running** — prints status (INA260 OK/NOT FOUND, GATT advertising)

## Provisioning Phase (decision diamond after boot)

After boot, show a diamond: **"Provisioned?"**
- **No** → Waiting for provisioner (advertising unprovisioned beacon with UUID 0xdddd prefix)
- **Yes (or just provisioned)** → `prov_complete()` callback fires:
  - Stores `net_idx`, `addr` in `node_state`
  - Caches `net_idx` for sending

Then show **Config Server callbacks** that fire during configuration:
- `APP_KEY_ADD` → caches `net_idx` + `app_idx`, saves to NVS
- `MODEL_APP_BIND` → when Vendor Client bound: sets `vnd_bound = true`, notifies GATT `"MESH_READY:VENDOR"`

## Runtime: Three Event Sources (parallel swim lanes or branching paths)

### Event Source 1: GATT Command from Pi 5
Arrow into `command_access_cb()`:
1. Pi 5 writes to Command characteristic (UUID 0xDC02)
2. `process_gatt_command(buf, len)` — parses `NODE_ID:COMMAND:VALUE`
3. Decision diamond: **"Target is self?"** (`target_addr == node_state.addr`)
   - **Yes** → `process_local_and_notify()` → `process_command()` → format sensor response → `gatt_notify_sensor_data()` back to Pi 5
   - **No** → Decision: **"ALL?"**
     - **Yes (ALL)** → Process locally FIRST, THEN `send_vendor_command(MESH_GROUP_ADDR, ...)` to 0xC000
     - **No** → `send_vendor_command(target_addr, ...)` to unicast address
4. After mesh send: `gatt_notify_sensor_data("SENT:COMMAND")`

### Event Source 2: Mesh Command from Another Node
Arrow into `custom_model_cb()` → `VND_OP_SEND`:
1. Check: **"src_addr == self?"** → Skip (self-echo from group send)
2. Extract command string from mesh message
3. `process_command(cmd, response, ...)` — execute locally
4. `esp_ble_mesh_server_model_send_msg()` → send `VND_OP_STATUS` response back to sender
5. If group message: override `recv_dst` with unicast for reply

### Event Source 3: Mesh Response from Remote Node
Arrow into `custom_model_cb()` → `VND_OP_STATUS`:
1. Parse response, extract `src_addr`
2. Calculate `node_id = src_addr - NODE_BASE_ADDR`
3. Format as `"NODE<id>:DATA:<payload>"`
4. `gatt_notify_sensor_data()` → forward to Pi 5 via GATT notify
5. `register_known_node(src)` — track for discovery
6. Clear `vnd_send_busy` flag

### Process Command Detail (sub-flowchart or expanded box)
`process_command()` decision tree:
- `"s"` / `"stop"` → `set_duty(0)` → format sensor response
- `"r"` / `"ramp"` → loop duty 0→25→50→75→100 (500ms each) → `set_duty(0)` → format
- `"duty:XX"` → `set_duty(XX)` → format
- `"read"` / `"status"` → format sensor response (no duty change)
- bare number → `set_duty(number)` → format
- else → `"ERR:UNKNOWN:<cmd>"`

### GATT Notify Chunking (annotation/note box)
- Messages ≤ 20 bytes: single notification
- Messages > 20 bytes: split into chunks with `'+'` prefix on continuation chunks, final chunk has no prefix
- Pi 5 reassembles by accumulating `'+'`-prefixed chunks

## Style
- Use vertical flow for boot sequence (top-down)
- Swim lanes or color-coded sections for the 3 runtime event sources
- Green = boot sequence, Blue = GATT events, Orange = Mesh server events, Purple = Mesh client events
- Decision diamonds for routing logic
- Note boxes for chunking protocol and self-addressing
