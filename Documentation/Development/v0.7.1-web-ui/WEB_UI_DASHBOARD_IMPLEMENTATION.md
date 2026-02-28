# v0.7.1 — Web UI Dashboard Implementation Guide

**Date:** February 28, 2026
**Author:** Justin Kwarteng
**Purpose:** Feed this document to an agent to implement the BLE Mesh web dashboard.

> [!IMPORTANT]
> **v0.6.2 Modular Cleanup:** All codebases were split into single-responsibility modules.
> **DO NOT add code to monolithic files** — create new modules for new functionality.
> **DO NOT use JSON file IPC** — use in-process Python calls + WebSockets.
> Add to the correct module file instead.

---

## 1. System Summary (What We Have Now — v0.7.0)

### Architecture

```
Pi 5 (gateway.py entry point)
  ├── dc_gateway.py       — BLE GATT client, scan, connect, notification handler
  ├── power_manager.py    — Power budgeting and duty balancing
  ├── tui_app.py          — Textual TUI (current primary interface)
  ├── ble_thread.py       — Dedicated asyncio event loop for BLE operations
  ├── node_state.py       — NodeState dataclass
  └── constants.py        — UUIDs, regex, device name prefixes
       |  BLE GATT (Service 0xDC01)
       v
  ESP32-C6 Universal Node(s) — sensor + GATT gateway
       |  BLE Mesh (Vendor Model)
       v
  ESP32-C6 Universal Node(s) — remote sensors
```

### Key Data Structures

**`NodeState` dataclass** (`node_state.py`):

```python
@dataclass
class NodeState:
    node_id: str
    duty: int = 0
    target_duty: int = 0
    commanded_duty: int = 0
    voltage: float = 0.0
    current: float = 0.0
    power: float = 0.0
    last_seen: float = field(default_factory=time.monotonic)
    responsive: bool = True
    poll_gen: int = 0
```

**`DCMonitorGateway` key attributes** (`dc_gateway.py`):

```python
self.client                     # BleakClient instance
self.connected_device           # BLE device (has .name, .address)
self.known_nodes: set[str]      # Node IDs that sent sensor data
self.sensing_node_count = 0     # From BLE scan
self._power_manager             # PowerManager instance
self._reconnecting = False      # True during failover
```

**`PowerManager` key attributes** (`power_manager.py`):

```python
self.nodes: dict[str, NodeState]
self.threshold_mw: Optional[float]
self.priority_node: Optional[str]
self._paused = False
```

### Where Data Events Happen

1. **`notification_handler()`** in `dc_gateway.py` — called on every BLE GATT notification (sensor data, command responses). This is where WebSocket broadcast should be triggered.

2. **`on_sensor_data()`** in `power_manager.py` — called when sensor data is parsed. Updates `NodeState`.

3. **`_balance_proportional()`** in `power_manager.py` — called after each poll cycle. This is where PM state change broadcasts should happen.

4. **`connect_to_node()` / `disconnect()`** in `dc_gateway.py` — connection state changes.

5. **`_auto_reconnect_loop()`** in `dc_gateway.py` — failover events.

---

## 2. Phase 1: Backend — FastAPI + WebSocket Server

### 2.1 Create `db.py` — SQLite Database Module

**File:** `gateway-pi5/gateway-code/db.py`

This module manages all database operations. SQLite with WAL mode for concurrent read/write.

```python
"""SQLite database for sensor history and dashboard settings.

Uses WAL mode for concurrent reads from the web server while the gateway
writes sensor data. All functions are synchronous (called from asyncio
via run_in_executor when needed).
"""

import sqlite3
import time
import os
from pathlib import Path

DB_PATH = Path(__file__).parent / "mesh_data.db"

def get_connection() -> sqlite3.Connection:
    """Get a database connection with WAL mode and row factory."""
    conn = sqlite3.connect(str(DB_PATH), check_same_thread=False)
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA synchronous=NORMAL")
    conn.row_factory = sqlite3.Row
    return conn

def init_db():
    """Create tables if they don't exist."""
    conn = get_connection()
    conn.executescript("""
        CREATE TABLE IF NOT EXISTS sensor_readings (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp REAL NOT NULL,
            node_id TEXT NOT NULL,
            duty INTEGER,
            voltage REAL,
            current_ma REAL,
            power_mw REAL,
            commanded_duty INTEGER
        );
        CREATE INDEX IF NOT EXISTS idx_readings_node_time
            ON sensor_readings(node_id, timestamp);

        CREATE TABLE IF NOT EXISTS settings (
            key TEXT PRIMARY KEY,
            value TEXT
        );
    """)
    conn.commit()
    conn.close()

def insert_reading(node_id: str, duty: int, voltage: float,
                   current_ma: float, power_mw: float,
                   commanded_duty: int = 0):
    """Insert a sensor reading."""
    conn = get_connection()
    conn.execute(
        "INSERT INTO sensor_readings "
        "(timestamp, node_id, duty, voltage, current_ma, power_mw, commanded_duty) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)",
        (time.time(), node_id, duty, voltage, current_ma, power_mw, commanded_duty)
    )
    conn.commit()
    conn.close()

def get_history(node_id: str = None, minutes: int = 30,
                limit: int = 500) -> list[dict]:
    """Get historical readings, optionally filtered by node and time window."""
    conn = get_connection()
    since = time.time() - (minutes * 60)
    if node_id:
        rows = conn.execute(
            "SELECT * FROM sensor_readings "
            "WHERE node_id = ? AND timestamp > ? "
            "ORDER BY timestamp DESC LIMIT ?",
            (node_id, since, limit)
        ).fetchall()
    else:
        rows = conn.execute(
            "SELECT * FROM sensor_readings "
            "WHERE timestamp > ? "
            "ORDER BY timestamp DESC LIMIT ?",
            (since, limit)
        ).fetchall()
    conn.close()
    return [dict(r) for r in rows]

def purge_old_readings(days: int = 7):
    """Delete readings older than N days."""
    conn = get_connection()
    cutoff = time.time() - (days * 86400)
    conn.execute("DELETE FROM sensor_readings WHERE timestamp < ?", (cutoff,))
    conn.commit()
    conn.close()
```

### 2.2 Create `web_server.py` — FastAPI + WebSocket Manager

**File:** `gateway-pi5/gateway-code/web_server.py`

```python
"""FastAPI web server with WebSocket support for real-time mesh data.

Embedded in the gateway process — no separate Flask/file IPC needed.
Broadcasts sensor data via WebSocket the instant it arrives from BLE.
"""

import asyncio
import json
import time
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
from pydantic import BaseModel

import db

# --- WebSocket Manager ---

class ConnectionManager:
    """Manages WebSocket connections and broadcasts."""

    def __init__(self):
        self.active_connections: list[WebSocket] = []

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)

    def disconnect(self, websocket: WebSocket):
        self.active_connections.remove(websocket)

    async def broadcast(self, message: dict):
        """Send JSON message to all connected WebSocket clients."""
        data = json.dumps(message)
        disconnected = []
        for conn in self.active_connections:
            try:
                await conn.send_text(data)
            except Exception:
                disconnected.append(conn)
        for conn in disconnected:
            self.active_connections.remove(conn)

# --- FastAPI App ---

app = FastAPI(title="DC Monitor Mesh Dashboard")
manager = ConnectionManager()

# Reference to the gateway (set by gateway.py at startup)
_gateway = None

def set_gateway(gw):
    """Called by gateway.py to inject the DCMonitorGateway reference."""
    global _gateway
    _gateway = gw

# --- Static Files (Dashboard UI) ---

DASHBOARD_DIR = Path(__file__).parent.parent / "dashboard"

@app.get("/")
async def index():
    return FileResponse(DASHBOARD_DIR / "index.html")

app.mount("/css", StaticFiles(directory=str(DASHBOARD_DIR / "css")), name="css")
app.mount("/js", StaticFiles(directory=str(DASHBOARD_DIR / "js")), name="js")

# --- WebSocket Endpoint ---

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await manager.connect(websocket)
    try:
        # Send initial state on connect
        if _gateway:
            await websocket.send_text(json.dumps({
                "type": "state",
                "data": _build_state()
            }))
        # Listen for commands from the browser
        while True:
            data = await websocket.receive_text()
            msg = json.loads(data)
            if msg.get("type") == "command" and _gateway:
                cmd = msg.get("command", "")
                # Execute command on the gateway
                asyncio.create_task(_execute_command(cmd))
    except WebSocketDisconnect:
        manager.disconnect(websocket)

async def _execute_command(cmd: str):
    """Execute a gateway command and let the response come via normal notification flow."""
    if not _gateway:
        return
    await _gateway.send_command(cmd)

# --- REST API ---

@app.get("/api/state")
async def get_state():
    """Return current mesh state."""
    return _build_state()

@app.get("/api/history")
async def get_history(node_id: str = None, minutes: int = 30, limit: int = 500):
    """Return historical sensor readings."""
    return db.get_history(node_id=node_id, minutes=minutes, limit=limit)

class CommandRequest(BaseModel):
    command: str

@app.post("/api/command")
async def post_command(req: CommandRequest):
    """Send a command to the mesh."""
    if not _gateway:
        return {"error": "Gateway not connected"}
    await _gateway.send_command(req.command)
    return {"status": "sent", "command": req.command}

# --- State Builder ---

def _build_state() -> dict:
    """Build current mesh state dict from gateway + PM objects."""
    if not _gateway:
        return {"error": "Gateway not initialized"}

    state = {
        "timestamp": time.time(),
        "gateway": {
            "connected": _gateway.client is not None and _gateway.client.is_connected,
            "device_name": getattr(_gateway.connected_device, 'name', None),
            "device_address": getattr(_gateway.connected_device, 'address', None),
            "reconnecting": _gateway._reconnecting,
        },
        "power_manager": None,
        "nodes": {},
        "sensing_node_count": _gateway.sensing_node_count,
    }

    pm = _gateway._power_manager
    if pm:
        state["power_manager"] = {
            "active": pm.threshold_mw is not None,
            "threshold_mw": pm.threshold_mw,
            "budget_mw": (pm.threshold_mw * 0.9) if pm.threshold_mw else None,
            "priority_node": pm.priority_node,
            "total_power_mw": sum(ns.power for ns in pm.nodes.values()),
        }
        for nid, ns in pm.nodes.items():
            state["nodes"][nid] = {
                "duty": ns.duty,
                "voltage": ns.voltage,
                "current": ns.current,
                "power": ns.power,
                "responsive": ns.responsive,
                "last_seen": ns.last_seen,
                "commanded_duty": ns.commanded_duty,
                "target_duty": ns.target_duty,
            }

    return state

# --- Broadcast Helpers (called by gateway event hooks) ---

async def broadcast_sensor_data(node_id: str, data: dict):
    """Called by dc_gateway.notification_handler on sensor data."""
    await manager.broadcast({
        "type": "sensor_data",
        "node_id": node_id,
        "data": data,
        "timestamp": time.time(),
    })

async def broadcast_state_change(event: str, details: dict = None):
    """Called on connect, disconnect, failover, PM changes."""
    await manager.broadcast({
        "type": "event",
        "event": event,
        "data": details or {},
        "timestamp": time.time(),
    })

async def broadcast_log(text: str):
    """Called on every gateway log message for console streaming."""
    await manager.broadcast({
        "type": "log",
        "text": text,
        "timestamp": time.time(),
    })
```

### 2.3 Add Event Hooks to `dc_gateway.py`

In `DCMonitorGateway.notification_handler()`, after parsing sensor data (after PM feed), add:

```python
# Broadcast to WebSocket clients
import asyncio
try:
    from web_server import broadcast_sensor_data, broadcast_log
    asyncio.run_coroutine_threadsafe(
        broadcast_sensor_data(node_id, {
            "duty": duty, "voltage": voltage,
            "current": current, "power": power,
        }),
        self._ble_loop  # The asyncio event loop
    )
except ImportError:
    pass  # Web server not enabled
```

In `DCMonitorGateway.log()`, add at the end:

```python
# Stream to WebSocket console
try:
    from web_server import broadcast_log
    asyncio.run_coroutine_threadsafe(
        broadcast_log(text),
        self._ble_loop
    )
except ImportError:
    pass
```

In `connect_to_node()`, after successful connection:

```python
try:
    from web_server import broadcast_state_change
    asyncio.run_coroutine_threadsafe(
        broadcast_state_change("connected", {
            "device_name": device.name,
            "device_address": device.address,
        }),
        self._ble_loop
    )
except ImportError:
    pass
```

Similarly in `disconnect()` and `_auto_reconnect_loop()` for failover events.

### 2.4 Add Event Hooks to `power_manager.py`

In `PowerManager._balance_proportional()`, after the balancing changes are computed:

```python
try:
    from web_server import broadcast_state_change
    import asyncio
    asyncio.run_coroutine_threadsafe(
        broadcast_state_change("pm_update", {
            "total_power": total_power,
            "budget": self.threshold_mw * 0.9 if self.threshold_mw else None,
            "changes": changes,
        }),
        self.gateway._ble_loop
    )
except ImportError:
    pass
```

### 2.5 Add DB Recording to `dc_gateway.py`

In `notification_handler()`, after parsing sensor data:

```python
# Record to database
try:
    import db
    db.insert_reading(node_id, duty, voltage, current, power)
except Exception:
    pass  # Don't crash gateway if DB fails
```

### 2.6 Modify `gateway.py` Entry Point

Add `--web` and `--web-only` flags:

```python
parser.add_argument('--web', action='store_true',
    help='Enable web dashboard alongside TUI')
parser.add_argument('--web-only', action='store_true',
    help='Web dashboard only, no TUI')
parser.add_argument('--web-port', type=int, default=8000,
    help='Web dashboard port (default 8000)')
```

When `--web` or `--web-only` is set:

```python
import db
import web_server
import uvicorn

db.init_db()
web_server.set_gateway(gateway)

# Run uvicorn on the BLE thread's event loop
config = uvicorn.Config(web_server.app, host="0.0.0.0", port=args.web_port)
server = uvicorn.Server(config)
ble_thread.submit(server.serve())
```

---

## 3. Phase 2: Frontend — Dashboard UI

### 3.1 Design System

**Color palette:**

```css
:root {
    --bg-primary: #09090b;
    --bg-secondary: #0f0f13;
    --bg-card: rgba(28, 35, 51, 0.6);
    --bg-elevated: rgba(33, 40, 59, 0.8);
    --border: rgba(255, 255, 255, 0.08);
    --border-light: rgba(255, 255, 255, 0.15);
    --text-primary: #e6edf3;
    --text-secondary: #8b949e;
    --text-dim: #484f58;
    --accent-green: #3fb950;
    --accent-blue: #58a6ff;
    --accent-orange: #d29922;
    --accent-purple: #bc8cff;
    --accent-red: #f85149;
    --accent-cyan: #39c5cf;
    --font: 'Inter', -apple-system, system-ui, sans-serif;
}
```

**Design principles:**

- Glassmorphism cards with `backdrop-filter: blur(8px)`
- Smooth CSS transitions on all state changes (0.3-0.4s ease)
- Subtle glow effects on active/connected elements
- Micro-animations on data updates (number spin-up)
- Responsive: desktop (3-col), tablet (2-col), phone (1-col)

### 3.2 Module Architecture

**`app.js`** — Central WebSocket manager:

- Opens WebSocket to `ws://<host>/ws`
- Parses incoming messages by `type` field
- Dispatches to registered handlers (`topology.onData()`, `nodes.onData()`, etc.)
- Auto-reconnects with exponential backoff (1s, 2s, 4s, max 30s)
- Exports `sendCommand(cmd)` for other modules

**`topology.js`** — D3.js mesh graph:

- Builds nodes: Pi 5 (purple), connected node (orange glow), remote nodes (green), relays (blue)
- Links: Pi 5 → connected node, connected node → remote nodes
- Only restarts D3 force simulation on topology changes (add/remove node), not on data updates
- Click node → highlight, show details in sidebar
- Node pulse animation on data receive

**`nodes.js`** — Node card grid:

- One card per sensing node
- Shows: duty %, voltage, current, power, commanded duty
- Power bar visualization (colored fill based on % of budget)
- ⚙️ menu: Set Duty, Read, Stop
- Status badge: "online" (green) / "stale" (yellow, >10s) / "offline" (red, >30s)
- Smooth CSS transitions between states

**`charts.js`** — Time-series charts:

- Chart.js line charts for power over time per node
- Loads initial data from `GET /api/history`
- Appends new points from WebSocket `sensor_data` messages
- Time window selector: 5m, 30m, 1h, 24h
- Auto-scrolling X axis

**`console.js`** — Live command console:

- Input field at bottom
- Scrolling log output above
- Receives `log` WebSocket messages for real-time streaming
- Sends commands via `app.sendCommand()`
- Command history (up/down arrow)

### 3.3 HTML Structure

```html
<body>
  <header id="header">
    <h1>DC Monitor Mesh Gateway</h1>
    <div id="connection-badge">● Connected to ESP-BLE-MESH</div>
  </header>

  <main id="main-grid">
    <section id="topology-section">
      <svg id="mesh-graph"></svg>
    </section>

    <aside id="pm-panel">
      <h2>Power Manager</h2>
      <div id="pm-content"><!-- JS populated --></div>
    </aside>

    <section id="nodes-section">
      <h2>Nodes</h2>
      <div id="node-cards"><!-- JS populated --></div>
    </section>

    <section id="history-section">
      <h2>History</h2>
      <div id="time-controls"><!-- 5m 30m 1h 24h buttons --></div>
      <canvas id="power-chart"></canvas>
    </section>

    <section id="console-section">
      <h2>Console</h2>
      <div id="console-log"></div>
      <input id="console-input" placeholder="Enter command...">
    </section>
  </main>

  <footer id="status-bar">
    <span id="node-summary">2 sensing, 0 relays</span>
    <span id="ws-status">WebSocket: Connected</span>
  </footer>

  <script src="https://d3js.org/d3.v7.min.js"></script>
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <script type="module" src="/js/app.js"></script>
</body>
```

---

## 4. Phase 3: Polish — Resilience and Settings

### 4.1 WebSocket Auto-Reconnect

In `app.js`, when the WebSocket closes:

```javascript
let reconnectDelay = 1000;
const MAX_DELAY = 30000;

function connect() {
    ws = new WebSocket(`ws://${location.host}/ws`);
    ws.onopen = () => {
        reconnectDelay = 1000;
        updateConnectionBadge('connected');
    };
    ws.onclose = () => {
        updateConnectionBadge('disconnected');
        setTimeout(connect, reconnectDelay);
        reconnectDelay = Math.min(reconnectDelay * 2, MAX_DELAY);
    };
}
```

### 4.2 Node Stale/Offline Detection

In `nodes.js`, on each render cycle:

```javascript
const age = Date.now() / 1000 - node.last_seen;
if (age > 30) status = 'offline';
else if (age > 10) status = 'stale';
else status = 'online';
```

Apply CSS class transitions (0.4s ease) so the color change is smooth, not a jarring snap.

### 4.3 Data Retention

In `db.py`, add a scheduled purge. Call from `gateway.py` on a 1-hour timer:

```python
async def _db_maintenance_loop():
    while True:
        await asyncio.sleep(3600)
        db.purge_old_readings(days=7)
```

### 4.4 Settings API

REST endpoints for PM configuration from the dashboard:

```
PUT /api/settings/threshold  {"value": 5000}
PUT /api/settings/priority   {"value": "2"}
DELETE /api/settings/priority
```

These call `_gateway._power_manager.set_threshold()` etc. directly.

---

## 5. Threading Model

```
Main Thread (Textual TUI event loop)
  └── TUI rendering, key input handling

BLE Thread (asyncio event loop) ← ALL async work lives here
  ├── BleakClient (BLE GATT operations)
  ├── _auto_reconnect_loop()
  ├── PowerManager.poll_loop()
  ├── Uvicorn (FastAPI server)        ← NEW (Phase 1)
  │   ├── HTTP request handlers
  │   └── WebSocket connections
  └── _db_maintenance_loop()          ← NEW (Phase 3)

notification_handler (bleak callback thread)
  ├── Parses sensor data
  ├── Feeds PowerManager
  ├── Posts to TUI (call_from_thread)
  ├── Inserts DB reading              ← NEW (Phase 1)
  └── Broadcasts via WebSocket        ← NEW (Phase 1)
      (asyncio.run_coroutine_threadsafe)
```

> [!WARNING]
> `notification_handler` runs on **bleak's callback thread**, not the asyncio loop.
> All WebSocket broadcasts must be scheduled via `asyncio.run_coroutine_threadsafe()`.
> Database writes can be synchronous (SQLite with WAL handles concurrent access).

---

## 6. Dependencies

Add to `gateway-pi5/requirements.txt`:

```
fastapi>=0.104.0
uvicorn[standard]>=0.24.0
websockets>=12.0
```

These are lightweight — FastAPI + uvicorn add ~5MB to the venv. No heavy frameworks.
