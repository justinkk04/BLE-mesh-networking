# v0.6.2 Mesh Network Dashboard — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Create a standalone web dashboard that visualizes the BLE Mesh network topology in real-time, showing node status, sensor readings, and network health — accessible from any browser via WireGuard.

**Architecture:** `gateway.py` writes a `mesh_state.json` file on every data update. A standalone `dashboard.py` (Flask) serves a single-page web UI that reads this file and renders a D3.js force-directed network graph. The user accesses it at `http://<pi5-wireguard-ip>:5555` from their laptop.

**Tech Stack:** Python (Flask), HTML/CSS/JavaScript, D3.js (CDN), JSON file IPC

**Branch name:** `feat/mesh-dashboard`

---

## Project Context

### What This Project Is

A BLE Mesh network for DC power monitoring. The system currently has:

| Component | Directory | Role |
| --- | --- | --- |
| Provisioner | `ESP/ESP-Provisioner` | Auto-provisions all mesh nodes (UUID prefix `0xdd`) |
| GATT Gateway | `ESP/ESP_GATT_BLE_Gateway` | Pi 5 ↔ Mesh bridge |
| Mesh Sensing Node(s) | `ESP/ESP-Mesh-Node-sensor-test` | Reads INA260 via I2C, controls PWM load |
| Relay Node | `ESP/ESP-Mesh-Relay-Node` | Silent relay — extends range |
| Pi 5 Gateway | `gateway-pi5/gateway.py` | Python TUI with PowerManager |
| **Dashboard (NEW)** | **`gateway-pi5/dashboard/`** | **Web visualization of mesh topology** |

### Deployment Environment

- **Pi 5** is at `~/ble-gateway/` with its own git repo, `.venv`, and `direnv` (.envrc)
- **Port 8080 is taken** on the home server — dashboard uses **port 5555**
- **WireGuard VPN** connects: Laptop (uni) → Home Server → Pi 5
- The user accesses the Pi 5 via VS Code Remote-SSH

### Development Workflow

1. Code is written in this repo at `gateway-pi5/dashboard/`
2. User copies/syncs files to Pi 5's `~/ble-gateway/dashboard/`
3. Run `pip install flask` in the existing Pi 5 venv
4. Start with `python dashboard.py` alongside `gateway.py`

---

## Data Flow

```
gateway.py                    dashboard.py                 Browser
    |                              |                          |
    |-- writes mesh_state.json --> |                          |
    |   (on every sensor update)   |-- serves index.html ---> |
    |                              |                          |
    |                              |<-- GET /api/state -----  |
    |                              |--- JSON response ------>  |
    |                              |                          |
    |                              |   (polls every 2s via    |
    |                              |    JavaScript fetch)      |
```

### mesh_state.json Format

```json
{
  "timestamp": "2026-02-15T18:30:00",
  "gateway": {
    "connected": true,
    "device_name": "ESP-BLE-MESH",
    "device_address": "98:A3:16:B1:C9:8A"
  },
  "power_manager": {
    "active": true,
    "threshold_mw": 5000,
    "budget_mw": 4500,
    "priority_node": "2",
    "total_power_mw": 4243
  },
  "nodes": {
    "1": {
      "role": "sensing",
      "duty": 100,
      "voltage": 12.294,
      "current": 1.25,
      "power": 15.4,
      "responsive": true,
      "last_seen": 1708030200.5,
      "commanded_duty": 100
    },
    "2": {
      "role": "sensing",
      "duty": 0,
      "voltage": 11.735,
      "current": 502.5,
      "power": 5896.8,
      "responsive": true,
      "last_seen": 1708030201.2,
      "commanded_duty": 0
    }
  },
  "relay_nodes": 1,
  "sensing_node_count": 3
}
```

**Note about relay nodes:** Relay nodes are inferred, not tracked individually. `sensing_node_count` comes from BLE scan (total mesh devices - 1 for GATT gateway). `relay_nodes = sensing_node_count - len(nodes)` (nodes that were scanned but never responded with sensor data are relays).

---

## Existing Code Key Details (for the implementing agent)

### gateway.py Data Structures

**`NodeState` dataclass** (line ~126):

```python
@dataclass
class NodeState:
    node_id: str
    duty: int = 0              # Current duty from sensor reading
    target_duty: int = 0       # User-requested duty %
    commanded_duty: int = 0    # Last duty % sent by PowerManager
    voltage: float = 0.0       # V
    current: float = 0.0       # mA
    power: float = 0.0         # mW
    last_seen: float = field(default_factory=time.monotonic)
    responsive: bool = True
    poll_gen: int = 0          # Which poll cycle this data is from
```

**`DCMonitorGateway` key attributes** (line ~684):

```python
self.known_nodes: set[str]    # Node IDs that responded with sensor data
self.sensing_node_count = 0   # From BLE scan: total_mesh_devices - 1
self.connected_device          # BLE device object (has .name, .address)
self._power_manager            # PowerManager instance (may be None)
```

**`PowerManager` key attributes** (line ~159):

```python
self.nodes: dict[str, NodeState]       # Active sensing nodes
self.threshold_mw: Optional[float]     # None if PM disabled
self.priority_node: Optional[str]      # Node ID or None
```

### Where to Hook Into gateway.py

The `notification_handler` method (line ~760) is called on **every** incoming GATT notification from bleak's callback thread. When it parses a `NODE<id>:DATA:<payload>` message, it:

1. Updates `known_nodes`
2. Feeds `PowerManager.on_sensor_data()`
3. Signals `_node_events`
4. Posts to TUI

**The JSON file write should happen here** — after the sensor data is parsed and PowerManager is fed, write the current state to `mesh_state.json`. This ensures every sensor update is captured.

The write should also happen in:

- `PowerManager.set_threshold()` — when PM is enabled/disabled
- `PowerManager.set_priority()` / `clear_priority()` — when priority changes
- `connect_to_node()` — when gateway connects
- `disconnect()` — when gateway disconnects

---

## Tasks

### Task 1: Add mesh_state.json Export to gateway.py

**Files:**

- Modify: `gateway-pi5/gateway.py`

**Step 1: Add the export function**

Add this function to `DCMonitorGateway` class, after `__init__` (around line 696):

```python
def _export_mesh_state(self):
    """Write current mesh state to JSON file for dashboard consumption."""
    import json
    from datetime import datetime

    state = {
        "timestamp": datetime.now().isoformat(),
        "gateway": {
            "connected": self.client is not None and self.client.is_connected,
            "device_name": getattr(self.connected_device, 'name', None),
            "device_address": getattr(self.connected_device, 'address', None),
        },
        "power_manager": None,
        "nodes": {},
        "relay_nodes": 0,
        "sensing_node_count": self.sensing_node_count,
    }

    # Export PowerManager state if active
    pm = self._power_manager
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
                "role": "sensing",
                "duty": ns.duty,
                "voltage": ns.voltage,
                "current": ns.current,
                "power": ns.power,
                "responsive": ns.responsive,
                "last_seen": ns.last_seen,
                "commanded_duty": ns.commanded_duty,
            }
    else:
        # No PM — still export known_nodes with minimal data
        for nid in self.known_nodes:
            state["nodes"][nid] = {
                "role": "sensing",
                "duty": 0, "voltage": 0, "current": 0, "power": 0,
                "responsive": True,
                "last_seen": time.monotonic(),
                "commanded_duty": 0,
            }

    # Infer relay count
    known_sensing = len(state["nodes"])
    if self.sensing_node_count > known_sensing:
        state["relay_nodes"] = self.sensing_node_count - known_sensing

    # Write atomically (write to temp, then rename)
    import os
    state_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mesh_state.json")
    tmp_file = state_file + ".tmp"
    try:
        with open(tmp_file, 'w') as f:
            json.dump(state, f, indent=2)
        os.replace(tmp_file, state_file)
    except Exception as e:
        # Don't crash gateway if dashboard export fails
        pass
```

**Step 2: Call _export_mesh_state() in notification_handler**

In `notification_handler`, after the PowerManager feed (around line 808, after `self._power_manager.on_sensor_data(...)`), add:

```python
# Export state for dashboard
self._export_mesh_state()
```

**Step 3: Call _export_mesh_state() in PowerManager state changes**

Add `self.gateway._export_mesh_state()` at the end of:

- `PowerManager.set_threshold()` (after `self._needs_bootstrap = True`)
- `PowerManager.disable()` (after restoring duty cycles)
- `PowerManager.set_priority()` (after setting priority)
- `PowerManager.clear_priority()` (after clearing priority)

**Step 4: Call _export_mesh_state() in connect/disconnect**

- `connect_to_node()` — add after successful connection
- `disconnect()` — add after disconnecting

**Step 5: Verify**

- Run `gateway.py`, connect to the mesh
- Check that `mesh_state.json` is created in the same directory as `gateway.py`
- Send a `read` command and verify the file updates
- `cat mesh_state.json | python -m json.tool` should show valid JSON

**Step 6: Commit**

```bash
git add gateway-pi5/gateway.py
git commit -m "feat(dashboard): export mesh_state.json for web dashboard"
```

---

### Task 2: Create dashboard.py Flask Server

**Files:**

- New: `gateway-pi5/dashboard/dashboard.py`
- New: `gateway-pi5/dashboard/requirements.txt`

**Step 1: Create requirements.txt**

```
flask>=2.3.0
```

**Step 2: Create dashboard.py**

```python
#!/usr/bin/env python3
"""
BLE Mesh Dashboard — Standalone web visualizer

Reads mesh_state.json (exported by gateway.py) and serves a web UI
showing the mesh network topology, node status, and sensor data.

Usage:
    python dashboard.py                    # Default: port 5555
    python dashboard.py --port 8888        # Custom port
    python dashboard.py --mock             # Mock data for UI development
"""

import argparse
import json
import os
import time
from pathlib import Path

from flask import Flask, jsonify, render_template, send_from_directory

app = Flask(__name__,
            template_folder='templates',
            static_folder='static')

# Path to mesh_state.json (one directory up, next to gateway.py)
STATE_FILE = Path(__file__).parent.parent / "mesh_state.json"

# Mock data for development without a live gateway
MOCK_STATE = {
    "timestamp": "2026-02-15T18:30:00",
    "gateway": {
        "connected": True,
        "device_name": "ESP-BLE-MESH",
        "device_address": "98:A3:16:B1:C9:8A"
    },
    "power_manager": {
        "active": True,
        "threshold_mw": 5000,
        "budget_mw": 4500,
        "priority_node": "2",
        "total_power_mw": 4243
    },
    "nodes": {
        "1": {
            "role": "sensing",
            "duty": 100,
            "voltage": 12.294,
            "current": 1.25,
            "power": 15.4,
            "responsive": True,
            "last_seen": time.time(),
            "commanded_duty": 100
        },
        "2": {
            "role": "sensing",
            "duty": 0,
            "voltage": 11.735,
            "current": 502.5,
            "power": 5896.8,
            "responsive": True,
            "last_seen": time.time(),
            "commanded_duty": 0
        }
    },
    "relay_nodes": 1,
    "sensing_node_count": 3
}


@app.route('/')
def index():
    return render_template('index.html')


@app.route('/api/state')
def get_state():
    """Return current mesh state as JSON."""
    if app.config.get('MOCK_MODE'):
        # Update timestamps in mock data to keep it "alive"
        MOCK_STATE["timestamp"] = time.strftime("%Y-%m-%dT%H:%M:%S")
        for node in MOCK_STATE["nodes"].values():
            node["last_seen"] = time.time()
        return jsonify(MOCK_STATE)

    try:
        if STATE_FILE.exists():
            with open(STATE_FILE, 'r') as f:
                state = json.load(f)
            return jsonify(state)
        else:
            return jsonify({"error": "mesh_state.json not found", "hint": "Is gateway.py running?"}), 404
    except json.JSONDecodeError:
        return jsonify({"error": "mesh_state.json is malformed"}), 500
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route('/static/<path:filename>')
def serve_static(filename):
    return send_from_directory('static', filename)


def main():
    parser = argparse.ArgumentParser(description='BLE Mesh Dashboard')
    parser.add_argument('--port', type=int, default=5555, help='Port to serve on (default: 5555)')
    parser.add_argument('--host', default='0.0.0.0', help='Host to bind to (default: 0.0.0.0)')
    parser.add_argument('--mock', action='store_true', help='Use mock data for UI development')
    args = parser.parse_args()

    app.config['MOCK_MODE'] = args.mock

    if args.mock:
        print(f"\n  🎨 MOCK MODE — using fake mesh data for UI development\n")
    else:
        print(f"\n  Reading mesh state from: {STATE_FILE}")
        if not STATE_FILE.exists():
            print(f"  ⚠  mesh_state.json not found — start gateway.py first")
        print()

    print(f"  ╔══════════════════════════════════════╗")
    print(f"  ║   BLE Mesh Dashboard                 ║")
    print(f"  ║   http://{args.host}:{args.port}             ║")
    print(f"  ╚══════════════════════════════════════╝\n")

    app.run(host=args.host, port=args.port, debug=True)


if __name__ == '__main__':
    main()
```

**Step 3: Commit**

```bash
git add gateway-pi5/dashboard/
git commit -m "feat(dashboard): Flask server with /api/state endpoint and mock mode"
```

---

### Task 3: Create the Web UI (HTML + CSS + JavaScript)

**Files:**

- New: `gateway-pi5/dashboard/templates/index.html`
- New: `gateway-pi5/dashboard/static/style.css`
- New: `gateway-pi5/dashboard/static/dashboard.js`

This is the main visual task. The UI has three sections:

```
┌─────────────────────────────────────────────────────┐
│  BLE Mesh Dashboard                    ● Connected  │
├──────────────────────────────┬──────────────────────┤
│                              │  Node Details        │
│                              │                      │
│   D3.js Force Graph          │  NODE 1 (sensing)    │
│                              │  Duty: 100%          │
│      [Pi5] ── [GW]          │  Voltage: 12.294V    │
│               / \            │  Current: 1.25mA     │
│          [N1]   [R1]         │  Power: 15.4mW       │
│                  |           │                      │
│                [N2]          │  ──────────────       │
│                              │  Power Manager       │
│                              │  Threshold: 5000mW   │
│                              │  Budget: 4500mW      │
│                              │  Total: 4243mW       │
│                              │  Priority: Node 2    │
├──────────────────────────────┴──────────────────────┤
│  Status: 2 sensing, 1 relay │ Last update: 18:30:00 │
└─────────────────────────────────────────────────────┘
```

**Step 1: Create `templates/index.html`**

Single HTML file that:

- Loads D3.js from CDN (`https://d3js.org/d3.v7.min.js`)
- Loads Google Font (Inter)
- Links to `style.css` and `dashboard.js`
- Contains the layout: header bar, main area (graph + sidebar), status bar
- The graph SVG container and sidebar panels are empty — populated by JavaScript

Key HTML structure:

```html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>BLE Mesh Dashboard</title>
    <link rel="stylesheet" href="/static/style.css">
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&display=swap" rel="stylesheet">
    <script src="https://d3js.org/d3.v7.min.js"></script>
</head>
<body>
    <header id="header">
        <h1>BLE Mesh Dashboard</h1>
        <div id="connection-status">
            <span class="status-dot"></span>
            <span id="connection-text">Connecting...</span>
        </div>
    </header>

    <main id="main">
        <div id="graph-container">
            <svg id="mesh-graph"></svg>
        </div>
        <aside id="sidebar">
            <div id="node-details">
                <h2>Node Details</h2>
                <p class="placeholder">Click a node to inspect</p>
            </div>
            <div id="power-manager-panel">
                <h2>Power Manager</h2>
                <div id="pm-content"></div>
            </div>
        </aside>
    </main>

    <footer id="status-bar">
        <span id="node-summary">Loading...</span>
        <span id="last-update">—</span>
    </footer>

    <script src="/static/dashboard.js"></script>
</body>
</html>
```

**Step 2: Create `static/style.css`**

Design aesthetic (clean, dark theme, modern):

- Dark background (`#0d1117`) with card-style panels (`#161b22`)
- Color coding: green (#3fb950) for sensing nodes, blue (#58a6ff) for relay, orange (#d29922) for gateway, purple (#bc8cff) for Pi 5
- Node circles with subtle glow/shadow
- Connection lines with opacity based on signal quality
- Smooth transitions for state changes
- Responsive layout (graph takes 70%, sidebar 30%)
- Status dot: green = connected, red = disconnected, yellow = stale

Key CSS properties:

```css
:root {
    --bg-primary: #0d1117;
    --bg-secondary: #161b22;
    --bg-card: #1c2333;
    --border: #30363d;
    --text-primary: #e6edf3;
    --text-secondary: #8b949e;
    --accent-green: #3fb950;
    --accent-blue: #58a6ff;
    --accent-orange: #d29922;
    --accent-purple: #bc8cff;
    --accent-red: #f85149;
    --font: 'Inter', -apple-system, system-ui, sans-serif;
}

/* Use flexbox layout: header / main (graph + sidebar) / footer */
/* Graph SVG takes full width/height of its container */
/* Sidebar has fixed 300px width, scrollable */
/* Node detail cards use CSS grid for label-value pairs */
```

**Step 3: Create `static/dashboard.js`**

This is the core of the UI. It does three things:

1. **Polls `/api/state` every 2 seconds** via `fetch()`
2. **Renders/updates a D3.js force-directed graph** from the mesh state
3. **Updates the sidebar panels** (node details, Power Manager status)

Graph node structure:

- **Pi 5 node** (always present, top) — purple circle, label "Pi 5"
- **Gateway node** (always present) — orange circle, label "Gateway"
- **Sensing nodes** — green circles, labeled "Node 1", "Node 2", etc. Opacity dims if `responsive: false`
- **Relay nodes** — blue circles, labeled "Relay 1", etc. (inferred from relay_nodes count)
- **Links:** Pi5→Gateway, Gateway→each node, Relay→downstream nodes

Key JavaScript logic:

```javascript
// State management
let currentState = null;
let selectedNode = null;

// D3 force simulation
const simulation = d3.forceSimulation()
    .force('link', d3.forceLink().id(d => d.id).distance(100))
    .force('charge', d3.forceManyBody().strength(-300))
    .force('center', d3.forceCenter(width / 2, height / 2))
    .force('collision', d3.forceCollide().radius(40));

// Poll loop
async function pollState() {
    try {
        const resp = await fetch('/api/state');
        if (resp.ok) {
            currentState = await resp.json();
            updateGraph(currentState);
            updateSidebar(currentState);
            updateStatusBar(currentState);
        }
    } catch (e) {
        // Show disconnected state
    }
}
setInterval(pollState, 2000);

// Graph update function — uses D3 enter/update/exit pattern
function updateGraph(state) {
    // Build nodes array from state
    const nodes = [
        { id: 'pi5', label: 'Pi 5', type: 'pi5' },
        { id: 'gateway', label: 'Gateway', type: 'gateway',
          connected: state.gateway.connected },
    ];

    // Add sensing nodes
    for (const [nid, data] of Object.entries(state.nodes)) {
        nodes.push({
            id: `node-${nid}`, label: `Node ${nid}`, type: 'sensing',
            ...data
        });
    }

    // Add inferred relay nodes
    for (let i = 0; i < state.relay_nodes; i++) {
        nodes.push({
            id: `relay-${i+1}`, label: `Relay ${i+1}`, type: 'relay'
        });
    }

    // Build links
    const links = [
        { source: 'pi5', target: 'gateway' }
    ];
    // Gateway connects to all mesh nodes
    nodes.filter(n => n.type === 'sensing' || n.type === 'relay')
         .forEach(n => links.push({ source: 'gateway', target: n.id }));

    // Update D3 simulation with enter/update/exit
    // ... (standard D3 pattern)
}

// Node click handler
function onNodeClick(event, d) {
    selectedNode = d;
    updateNodeDetails(d);
}

// Update sidebar with selected node details
function updateNodeDetails(node) {
    // Show duty, voltage, current, power, responsive status
    // Format values nicely
}

// Update Power Manager panel
function updatePowerManager(state) {
    // Show threshold, budget, priority, total power
    // Show progress bar for budget usage
}
```

**Step 4: Verify locally**

```bash
cd gateway-pi5/dashboard
pip install flask
python dashboard.py --mock
```

Open `http://localhost:5555` in a browser. Should see:

- Dark-themed dashboard with force graph
- Pi5 → Gateway → Node1, Node2, Relay1
- Click nodes to see details in sidebar
- Power Manager panel shows mock budget data

**Step 5: Commit**

```bash
git add gateway-pi5/dashboard/
git commit -m "feat(dashboard): web UI with D3.js topology graph and dark theme"
```

---

### Task 4: Test with Live Data on Pi 5

This is a deployment task — the user handles this on hardware.

**Step 1: Copy files to Pi 5**

```bash
# From the Windows repo, copy dashboard folder to Pi 5
scp -r gateway-pi5/dashboard/ justin@jarvis:~/ble-gateway/dashboard/
```

Or if using git:

```bash
# On Pi 5
cd ~/ble-gateway
git pull  # If using the same repo
```

**Step 2: Install Flask in Pi 5 venv**

```bash
cd ~/ble-gateway
source .venv/bin/activate
pip install flask
```

**Step 3: Run gateway.py in one terminal**

```bash
python gateway.py --address 98:A3:16:B1:C9:8A
```

Verify `mesh_state.json` is created after connecting and sending a command.

**Step 4: Run dashboard.py in another terminal**

```bash
cd dashboard
python dashboard.py
```

**Step 5: Open in browser**

Navigate to `http://<pi5-wireguard-ip>:5555` from laptop at uni.

Expected: Live mesh topology with real node data updating every 2 seconds.

**Step 6: Commit**

```bash
git commit --allow-empty -m "chore(dashboard): verified live deployment on Pi 5"
```

---

### Task 5: Polish and Edge Cases

**Step 1: Handle stale data**

In `dashboard.js`, compare `state.timestamp` with current time. If data is older than 10 seconds, show a yellow "STALE" indicator. If older than 30 seconds, show red "OFFLINE".

**Step 2: Handle gateway disconnected**

When `state.gateway.connected` is false, fade the graph, show "Gateway Disconnected" overlay.

**Step 3: Handle zero nodes**

When no nodes are discovered yet, show "Waiting for mesh data..." in the graph area.

**Step 4: Responsive sizing**

Ensure the graph SVG resizes on window resize. Use `ResizeObserver` on the container.

**Step 5: Commit**

```bash
git add gateway-pi5/dashboard/
git commit -m "feat(dashboard): edge cases — stale data, disconnect, responsive sizing"
```

---

### Task 6: Documentation

**Step 1: Create a README for the dashboard**

- New: `gateway-pi5/dashboard/README.md`

Covers: what it is, how to run (mock mode + live mode), port config, deployment to Pi 5, screenshot placeholder.

**Step 2: Commit**

```bash
git add gateway-pi5/dashboard/README.md
git commit -m "docs(dashboard): add README with setup and usage instructions"
```

---

## Node Color + Style Reference

| Node Type | Color | Shape | Label |
| --- | --- | --- | --- |
| Pi 5 | Purple (#bc8cff) | Circle with ring | "Pi 5" |
| GATT Gateway | Orange (#d29922) | Circle with ring | "Gateway" |
| Sensing Node | Green (#3fb950) | Filled circle | "Node 1", "Node 2" |
| Relay Node | Blue (#58a6ff) | Dashed circle | "Relay 1" |
| Offline Node | Red glow (#f85149) | Faded + pulsing border | Same label |

## File Structure

```
gateway-pi5/
├── gateway.py              ← MODIFIED (add _export_mesh_state)
├── mesh_state.json         ← GENERATED (by gateway.py at runtime)
└── dashboard/
    ├── dashboard.py        ← NEW (Flask server)
    ├── requirements.txt    ← NEW (flask>=2.3.0)
    ├── README.md           ← NEW (documentation)
    ├── templates/
    │   └── index.html      ← NEW (single page)
    └── static/
        ├── style.css       ← NEW (dark theme)
        └── dashboard.js    ← NEW (D3.js graph + polling)
```

## Important Notes

- **Port 5555** is the default. Change via `--port` flag.
- **Mock mode** (`--mock`) lets you develop the UI on Windows without the Pi 5 or any BLE hardware.
- **Atomic writes:** `_export_mesh_state()` writes to a `.tmp` file then renames, so `dashboard.py` never reads a partially-written JSON.
- **No websockets needed:** 2-second polling via `fetch()` is plenty for this use case and avoids the complexity of websocket libraries.
- **gateway.py changes are minimal:** ~60 lines added (one method + 6 one-liner calls). Zero changes to existing logic.
