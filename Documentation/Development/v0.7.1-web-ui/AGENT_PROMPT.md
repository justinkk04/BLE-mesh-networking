# v0.7.1 Web UI Dashboard — Agent Prompts

> **Instructions:** Use these prompts one at a time, in order.
> Each phase is independently testable — verify it passes before moving to the next.

---

## Phase 1 Prompt (Backend — FastAPI + WebSocket + SQLite)

```
You are implementing Phase 1 of the BLE Mesh Web Dashboard (v0.7.1).

REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a FastAPI web server with WebSocket support and SQLite history database to the gateway process. No frontend yet — just the backend APIs and data plumbing.

**Context:** Read these files FIRST:
- `Documentation/Development/v0.7.1-web-ui/WEB_UI_DASHBOARD_PLAN.md` — high-level design (Phase 1 section)
- `Documentation/Development/v0.7.1-web-ui/WEB_UI_DASHBOARD_IMPLEMENTATION.md` — Sections 1-2 (Phase 1 details)

Then read the MODULAR Python gateway code:
- `gateway-pi5/gateway-code/gateway.py` — entry point (~140 lines)
- `gateway-pi5/gateway-code/dc_gateway.py` — DCMonitorGateway class — MAIN HOOK TARGET
- `gateway-pi5/gateway-code/power_manager.py` — PowerManager class
- `gateway-pi5/gateway-code/ble_thread.py` — BLE I/O thread (~68 lines)
- `gateway-pi5/gateway-code/node_state.py` — NodeState dataclass
- `gateway-pi5/gateway-code/constants.py` — UUIDs, regex

**Architecture:** The Python gateway connects to ESP32-C6 universal nodes via BLE (service 0xDC01). The v0.6.3 Flask attempt failed because it used JSON file IPC and polling. This version uses in-process FastAPI with WebSockets for instant data push.

**Dev environment:** Windows with real ESP32-C6 nodes connected via BLE. The TUI + web server both run natively. No mock mode needed — real BLE data from the start. Final deployment target is Pi 5.

**Tech Stack:** Python 3, FastAPI, Uvicorn, SQLite (WAL mode), Websockets, bleak, textual

---

### CRITICAL: Modular Code Structure (v0.6.2)

> The codebase was refactored into single-responsibility modules in v0.6.2.
> DO NOT create monolithic files. Each class/feature lives in its own module:
>
> | Module | Contains |
> |--------|----------|
> | `dc_gateway.py` | `DCMonitorGateway` class |
> | `power_manager.py` | `PowerManager` class |
> | `tui_app.py` | `MeshGatewayApp` class (keep working) |
> | `ble_thread.py` | `BleThread` class |
> | `node_state.py` | `NodeState` dataclass |
> | `constants.py` | UUIDs, regex, device prefixes |
> | `gateway.py` | Entry point (main + _run_cli) |
> | `web_server.py` | **[NEW]** FastAPI + WebSocket server |
> | `db.py` | **[NEW]** SQLite database module |
>
> Changes go to the CORRECT module. Do not combine classes back into one file.

### CRITICAL: Threading Model

> `notification_handler` runs on bleak's callback thread, NOT the asyncio loop.
> All WebSocket broadcasts MUST use `asyncio.run_coroutine_threadsafe()`.
> Database writes can be synchronous (SQLite WAL handles concurrent access).
> FastAPI/Uvicorn runs on the SAME asyncio event loop as the BLE thread.

---

### Task 1: Create `db.py` — SQLite Database Module

**File:** `gateway-pi5/gateway-code/db.py` — **[NEW]**

Create a SQLite module with WAL mode for concurrent read/write. Functions:
- `init_db()` — create tables (sensor_readings, settings)
- `insert_reading()` — insert one sensor reading
- `get_history()` — query readings by node_id and time window
- `purge_old_readings()` — delete readings older than N days
- `get_connection()` — returns WAL-mode connection with Row factory

See Section 2.1 of IMPLEMENTATION.md for exact code.

**Verify:** `python -c "import db; db.init_db(); print('OK')"`

---

### Task 2: Create `web_server.py` — FastAPI + WebSocket Manager

**File:** `gateway-pi5/gateway-code/web_server.py` — **[NEW]**

Create a FastAPI app with:
- `ConnectionManager` class for WebSocket client management
- `set_gateway(gw)` to inject gateway reference
- `GET /` — serve dashboard index.html (placeholder for now)
- `GET /api/state` — return current mesh state as JSON
- `GET /api/history` — return historical readings from SQLite
- `POST /api/command` — send command to mesh
- `WS /ws` — WebSocket endpoint (accept, send initial state, listen for commands)
- `broadcast_sensor_data()` — async helper called from notification_handler
- `broadcast_state_change()` — async helper called on connect/disconnect/failover
- `broadcast_log()` — async helper for console log streaming

See Section 2.2 of IMPLEMENTATION.md for exact code.

**Verify:** `python -c "from web_server import app; print('OK')"`

---

### Task 3: Add Event Hooks to `dc_gateway.py`

**File:** `gateway-pi5/gateway-code/dc_gateway.py`

Add these hooks (see Section 2.3 of IMPLEMENTATION.md):

1. In `notification_handler()` — after parsing sensor data, call `broadcast_sensor_data()` and `db.insert_reading()` via `asyncio.run_coroutine_threadsafe()`
2. In `log()` — call `broadcast_log()` for WebSocket console streaming
3. In `connect_to_node()` — call `broadcast_state_change("connected")`
4. In `disconnect()` — call `broadcast_state_change("disconnected")`
5. In `_auto_reconnect_loop()` — call `broadcast_state_change("failover")`

All WebSocket calls must use `asyncio.run_coroutine_threadsafe()` because notification_handler runs on bleak's thread.

**Verify:** `python -c "import py_compile; py_compile.compile('dc_gateway.py', doraise=True); print('OK')"`

---

### Task 4: Add PM Event Hooks to `power_manager.py`

**File:** `gateway-pi5/gateway-code/power_manager.py`

After `_balance_proportional()` computes changes, broadcast PM state update via WebSocket.

See Section 2.4 of IMPLEMENTATION.md.

**Verify:** `python -c "import py_compile; py_compile.compile('power_manager.py', doraise=True); print('OK')"`

---

### Task 5: Add `--web` Flags to `gateway.py`

**File:** `gateway-pi5/gateway-code/gateway.py`

Add CLI flags:
- `--web` — enable web dashboard alongside TUI
- `--web-only` — web dashboard only, no TUI
- `--web-port` — port number (default 8000)

When web mode is enabled:
1. Initialize database (`db.init_db()`)
2. Inject gateway reference (`web_server.set_gateway(gw)`)
3. Start uvicorn on the BLE thread event loop

See Section 2.6 of IMPLEMENTATION.md.

**Verify:** `python gateway.py --web --help` should show the new flags.

---

### Task 6: Update `requirements.txt`

**File:** `gateway-pi5/requirements.txt`

Add: `fastapi>=0.104.0`, `uvicorn[standard]>=0.24.0`, `websockets>=12.0`

**Verify:** `pip install -r requirements.txt`

---

### Task 7: End-to-End Backend Test (on Windows with real BLE nodes)

1. `python gateway.py --web` — gateway starts with both TUI and web server
2. Open `http://localhost:8000/api/state` — should return JSON with gateway state
3. Connect a WebSocket client to `ws://localhost:8000/ws` — should receive initial state
4. Send `read` command in TUI — WebSocket client should receive sensor data message
5. `http://localhost:8000/api/history?minutes=5` — should show the reading just recorded
6. POST `{"command": "ALL:READ"}` to `/api/command` — should return `{"status": "sent"}`

```

---

## Phase 2 Prompt (Frontend — Dashboard UI)

```
You are implementing Phase 2 of the BLE Mesh Web Dashboard (v0.7.1).

REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build the web frontend — a beautiful, real-time dashboard with mesh topology graph, node cards, power charts, and command console. All driven by WebSocket from the Phase 1 backend.

**Pre-requisite:** Phase 1 (FastAPI + WebSocket + SQLite backend) is complete and verified.

**Dev environment:** Windows with real ESP32-C6 nodes connected via BLE. Run `python gateway.py --web` and open `http://localhost:8000` in a browser — real data flows immediately.

**Context:** Read these files FIRST:
- `Documentation/Development/v0.7.1-web-ui/WEB_UI_DASHBOARD_PLAN.md` — Phase 2 section + dashboard layout
- `Documentation/Development/v0.7.1-web-ui/WEB_UI_DASHBOARD_IMPLEMENTATION.md` — Section 3 (Frontend)

Then read the backend API:
- `gateway-pi5/gateway-code/web_server.py` — API endpoints and WebSocket messages

**Tech Stack:** Vanilla HTML/CSS/JavaScript, D3.js v7 (CDN), Chart.js v4 (CDN), Inter font (Google Fonts)

---

### CRITICAL: Design Requirements

> The dashboard MUST look premium and modern. Follow these rules:
>
> 1. **Dark theme** — deep blacks (#09090b), glassmorphism cards, subtle borders
> 2. **Smooth transitions** — all state changes (online/stale/offline) use CSS transitions (0.3-0.4s)
> 3. **Micro-animations** — number spin-up on data updates, pulse on node data received
> 4. **No D3 jitter** — only restart force simulation on topology changes, not data updates
> 5. **Responsive** — CSS Grid with breakpoints for desktop/tablet/phone
> 6. **No build step** — vanilla JS with ES modules, CDN libraries
> 7. **Modular JS** — separate files per concern (app.js, topology.js, nodes.js, charts.js, console.js)

---

### Task 1: Create `index.html` — Main HTML Shell

**File:** `gateway-pi5/dashboard/index.html` — **[NEW]**

Single HTML page with:
- Meta tags, viewport, title
- Google Fonts (Inter) link
- D3.js v7 and Chart.js v4 CDN scripts
- CSS link to `css/style.css`
- Layout sections: header, topology, PM panel, node cards, history charts, console, footer
- ES module script loading `js/app.js`

See Section 3.3 of IMPLEMENTATION.md for HTML structure.

---

### Task 2: Create `css/style.css` — Design System

**File:** `gateway-pi5/dashboard/css/style.css` — **[NEW]**

Complete dark theme with:
- CSS custom properties (color palette, fonts, spacing)
- Glassmorphism card styles (backdrop-filter, translucent backgrounds)
- CSS Grid layout for main page structure
- Node card styles with hover elevation and status badges
- Smooth transitions on background-color, border-color, opacity
- Connection status dot (green/yellow/red)
- Console styling (monospace, scrollable)
- Chart container styling
- Responsive breakpoints (@media queries)
- Command menu popover styling

See Section 3.1 of IMPLEMENTATION.md for color palette.

---

### Task 3: Create `js/app.js` — WebSocket Manager

**File:** `gateway-pi5/dashboard/js/app.js` — **[NEW]**

Central module that:
- Opens WebSocket to `ws://${location.host}/ws`
- Auto-reconnects with exponential backoff on disconnect
- Parses incoming JSON messages by `type` field
- Dispatches to registered handler modules
- Exports `sendCommand(cmd)` for other modules to use
- Updates connection badge in header
- Imports and initializes all other modules

---

### Task 4: Create `js/topology.js` — D3.js Mesh Graph

**File:** `gateway-pi5/dashboard/js/topology.js` — **[NEW]**

D3.js force-directed graph:
- Node types: Pi 5 (purple, top), connected node (orange glow), remote nodes (green), relays (blue)
- Links: Pi 5 → connected node → remote nodes
- Enter/update/exit pattern for smooth add/remove
- Only restart simulation (alpha 0.3) on topology changes
- Data-only updates: just update labels and badges, no force restart
- Click node to select → dispatch event for node cards
- SVG auto-sizes to container via ResizeObserver

---

### Task 5: Create `js/nodes.js` — Node Cards

**File:** `gateway-pi5/dashboard/js/nodes.js` — **[NEW]**

Node card grid:
- One card per sensing node
- Metrics: duty %, voltage, current, power, commanded duty
- Power bar (colored fill proportional to budget share)
- Status badge: online (green >10s), stale (yellow 10-30s), offline (red >30s)
- ⚙️ menu button → popover: Set Duty, Read, Stop
- Commands sent via `app.sendCommand()`
- Smooth number animation on updates

---

### Task 6: Create `js/charts.js` — Time-Series Charts

**File:** `gateway-pi5/dashboard/js/charts.js` — **[NEW]**

Chart.js time-series:
- Line chart for power (mW) per node over time
- Load initial data from `GET /api/history`
- Append new points from WebSocket `sensor_data` messages
- Time window buttons: 5m, 30m, 1h, 24h
- Auto-scrolling X axis
- One dataset per node, color-coded

---

### Task 7: Create `js/console.js` — Command Console

**File:** `gateway-pi5/dashboard/js/console.js` — **[NEW]**

Live console:
- Scrolling log output (receives `log` WebSocket messages)
- Input field at bottom
- Send commands via `app.sendCommand()` on Enter
- Command history with up/down arrow keys
- Auto-scroll to bottom on new messages
- Monospace font, terminal styling

---

### Task 8: Update `web_server.py` Static File Serving

**File:** `gateway-pi5/gateway-code/web_server.py`

Ensure the `GET /` route serves `dashboard/index.html` and static mounts for `/css/` and `/js/` point to the dashboard directory.

---

### Task 9: End-to-End UI Test (on Windows with real BLE nodes)

1. Start `python gateway.py --web` (BLE nodes connected)
2. Open `http://localhost:8000` in browser
3. **Verify:** Dark themed dashboard loads with topology graph
4. **Verify:** Node cards show real-time sensor data (numbers update without refresh)
5. **Verify:** Click ⚙️ on a node → Set Duty → command executes → node responds
6. **Verify:** Console shows live log output, accepts commands
7. **Verify:** History chart shows rolling power data
8. **Verify:** Power-cycle a node → topology updates, node card shows "offline" → "online"

```

---

## Phase 3 Prompt (Polish — Resilience, Mobile, Settings)

```
You are implementing Phase 3 of the BLE Mesh Web Dashboard (v0.7.1).

REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Polish the dashboard for production use — WebSocket auto-reconnect, stale/offline visual states, responsive mobile layout, data retention, and PM settings API.

**Pre-requisite:** Phases 1 (backend) and 2 (frontend) are complete and verified.

**Context:** Read these files FIRST:
- `Documentation/Development/v0.7.1-web-ui/WEB_UI_DASHBOARD_PLAN.md` — Phase 3 section
- `Documentation/Development/v0.7.1-web-ui/WEB_UI_DASHBOARD_IMPLEMENTATION.md` — Section 4 (Polish)

Then read the existing frontend code:
- `gateway-pi5/dashboard/js/app.js` — WebSocket manager
- `gateway-pi5/dashboard/js/nodes.js` — Node card rendering
- `gateway-pi5/dashboard/css/style.css` — Current styling

---

### Task 1: WebSocket Auto-Reconnect with Exponential Backoff

**File:** `gateway-pi5/dashboard/js/app.js`

If `app.js` doesn't already have it, add exponential backoff reconnection (1s, 2s, 4s... max 30s). Show "Disconnected — Reconnecting..." overlay on the page. Reset delay on successful connection.

---

### Task 2: Stale/Offline Node Visual States

**File:** `gateway-pi5/dashboard/js/nodes.js`, `gateway-pi5/dashboard/css/style.css`

Compare `node.last_seen` with current time:
- <10s → online (green badge, full opacity)
- 10-30s → stale (yellow badge, reduced opacity)
- >30s → offline (red badge, dimmed card)

Use CSS transitions (0.4s ease) so color changes are smooth, not jarring.

---

### Task 3: Responsive Mobile Layout

**File:** `gateway-pi5/dashboard/css/style.css`

Add @media breakpoints:
- Desktop (>1200px): 3 columns — topology, PM panel, node cards side-by-side
- Tablet (768-1200px): 2 columns — topology full-width, PM + nodes below
- Phone (<768px): 1 column — everything stacked, topology smaller

---

### Task 4: Configurable Chart Time Window

**File:** `gateway-pi5/dashboard/js/charts.js`

Add time window selector buttons (5m, 30m, 1h, 24h). On click, fetch historical data from `/api/history?minutes=N` and reload the chart. Default to 30m.

---

### Task 5: Data Retention Policy

**File:** `gateway-pi5/gateway-code/db.py`, `gateway-pi5/gateway-code/gateway.py`

Add a `_db_maintenance_loop()` that runs every hour and calls `db.purge_old_readings(days=7)`. Start it alongside the web server.

---

### Task 6: PM Settings API

**File:** `gateway-pi5/gateway-code/web_server.py`

Add REST endpoints:
- `PUT /api/settings/threshold` — set PM threshold
- `PUT /api/settings/priority` — set priority node
- `DELETE /api/settings/priority` — clear priority

These call the gateway's PowerManager methods directly.

---

### Task 7: Verify All Edge Cases

1. Kill gateway → browser shows "Disconnected" overlay → restart → auto-reconnects
2. Power-cycle a node → card fades yellow → red → green when it recovers
3. Open on phone via WireGuard → responsive layout works
4. Chart 1h window → loads data from SQLite → new data appends live
5. Set threshold from dashboard settings → PM starts balancing

```

---

## Phase 4 Prompt (Integration Test & Cleanup)

```
You are running Phase 4 of the BLE Mesh Web Dashboard (v0.7.1).

**Goal:** End-to-end integration testing and cleanup. No new code — just testing and documentation.

**Context:** Read the Phase 4 checklist in:
- `Documentation/Development/v0.7.1-web-ui/WEB_UI_DASHBOARD_PLAN.md` — Phase 4 section

### Integration Test Checklist

Run through every item. For each, note PASS or FAIL with details:

- [ ] 2+ universal nodes provisioned and running
- [ ] Browser shows topology with correct node count
- [ ] ALL:READ data appears on dashboard within 500ms
- [ ] Set duty from node card → node responds → dashboard updates
- [ ] Power-cycle connected node → failover → dashboard shows new connection
- [ ] PM threshold set from dashboard → balancing starts → charts show convergence
- [ ] Console sends commands and shows responses
- [ ] History charts show 30-minute rolling data
- [ ] Deploy to Pi 5 → open on phone via WireGuard → full functionality
- [ ] Gateway runs stable for 30+ minutes without crashes or memory leaks

### Cleanup

- [ ] Add `--web-only` flag to `gateway.py` (no TUI, web dashboard only)
- [ ] Update `README.md` with web dashboard setup instructions
- [ ] Update `MESH_IMPLEMENTATION.md` with new architecture diagram
- [ ] Log all changes in `Documentation/Development/v0.7.1-web-ui/CHANGELOG.md`
- [ ] Tag the repo as `v0.7.1`

```
