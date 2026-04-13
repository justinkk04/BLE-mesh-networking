# Prompt: System Architecture Overview Flowchart

> **Usage:** Copy everything below the `---` line and paste it as a prompt in a new conversation.

---

## Task

Create a `.drawio` XML file for a **System Architecture Overview** flowchart of my BLE Mesh DC Power Monitor project. Save it to `Documentation/Flowcharts/01-system-architecture-overview.drawio`.

## What This Diagram Must Show

This is the "hero diagram" — the full end-to-end system with every major component and the communication protocols between them.

### Components (boxes/groups)

1. **Raspberry Pi 5 Gateway** (top-level box, contains sub-components):
   - `gateway.py` — entry point (mode selector)
   - `dc_gateway.py` — BLE GATT client (bleak), notification handler, command routing
   - `tui_app.py` — Textual terminal UI (DataTable, RichLog, Input)
   - `web_server.py` — FastAPI + WebSocket server (REST API + real-time push)
   - `power_manager.py` — Equilibrium-based power balancer
   - `db.py` — SQLite time-series storage
   - `ble_thread.py` — Dedicated asyncio event loop for BLE I/O

2. **ESP32-C6 Universal Node** (middle box, represents 1..N nodes, each contains):
   - `main.c` — Boot orchestrator
   - `mesh_node.c` — BLE Mesh stack (Vendor Server + Client models, provisioning callbacks)
   - `gatt_service.c` — NimBLE GATT service (Service UUID: 0xDC01, Sensor Data char + Command char)
   - `command_parser.c` — Parses `NODE_ID:COMMAND:VALUE` from Pi 5, routes to local or mesh
   - `command.c` — Executes commands locally (read/duty/ramp/stop), formats sensor response
   - `sensor.c` — INA260 I2C driver (voltage, current, power)
   - `load_control.c` — LEDC PWM output (duty cycle control)

3. **ESP32-C6 Provisioner** (separate box):
   - Auto-provisions unprovisioned nodes (UUID prefix 0xdddd)
   - Assigns unicast addresses, AppKey, model bindings, group subscription (0xC000)

4. **ESP32-C6 Relay Node** (optional, smaller box):
   - Relay-only, no sensor, extends mesh range, TTL=7

5. **Web Browser** (client box):
   - Dashboard UI (`index.html`, D3.js topology, Chart.js telemetry)

### Communication Links (arrows with protocol labels)

- **Pi 5 ↔ Connected Universal Node**: `BLE GATT (Service 0xDC01)` — bidirectional (Write commands, Notify sensor data). Show chunking note: messages >20 bytes split with '+' prefix.
- **Connected Node ↔ Remote Nodes**: `BLE Mesh (Vendor Model, VND_OP_SEND/VND_OP_STATUS)` — commands forwarded via mesh, responses returned to connected node.
- **Connected Node (self-addressing)**: Arrow looping back to itself labeled `Self-Addressing: target_addr == node_state.addr → process locally (no mesh round-trip)`
- **All Nodes ← Group Address**: `BLE Mesh Group (0xC000)` — ALL:READ broadcasts simultaneously to all subscribed nodes.
- **Provisioner → Nodes**: `BLE Mesh Provisioning (PB-ADV / PB-GATT)` — one-time setup.
- **Relay Nodes ↔ Mesh**: `BLE Mesh Relay (TTL=7)` — extends range.
- **Pi 5 ↔ Web Browser**: `HTTP REST (/api/state, /api/history, /api/command)` and `WebSocket (ws:///ws)` — real-time sensor data push + command input.
- **Sensor Nodes ↔ INA260**: `I2C` — voltage/current/power reading.
- **Sensor Nodes → Load**: `LEDC PWM` — duty cycle output to MOSFET circuit.

### Layout

- Top: Web Browser
- Middle-top: Pi 5 Gateway (horizontal layout of sub-components)
- Middle: Connected Universal Node (highlighted, slightly larger)
- Middle-bottom: Remote Universal Node(s) + Relay Node(s)
- Bottom-left: Provisioner
- Bottom-right: INA260 sensor + Load circuit

### Style

- Use color-coded groups: Blue for Pi 5, Green for ESP nodes, Orange for provisioner, Gray for relay, Purple for web browser
- Rounded rectangles for software modules
- Dashed border for the "BLE Mesh Network" region encompassing all ESP nodes
- Protocol labels on every arrow
- Include version note: "v0.7.1 — Universal Nodes + Web Dashboard"
