# v0.7.1 Web UI Dashboard Plan

**Date:** February 28, 2026
**Author:** Justin Kwarteng
**Status:** Draft — Awaiting Review

---

## 1. Problem Statement

The current interface is a Python TUI (Textual) running in a terminal. While functional, it has significant limitations:

- **No remote access** — must SSH into the Pi 5 or run a terminal locally
- **No historical data** — sensor readings are ephemeral, gone once scrolled past
- **No visualization** — numbers in a table, no graphs or charts
- **No multi-user** — only one terminal session at a time

### Previous Attempt (v0.6.3 — failed)

A Flask-based dashboard was attempted with this architecture:

```
gateway.py ──(writes)──> mesh_state.json ──(reads)──> Flask ──(polls 2s)──> Browser
                                                        └──> SQLite (history)
gateway.py <──(reads)──  mesh_commands.json <──(writes)── Flask <── Browser
```

**Why it failed:**

- **JSON file IPC** — gateway writes JSON to disk on every sensor update; Flask reads it on every poll. SD card I/O becomes the bottleneck
- **2-second polling lag** — browser always 0-2s behind reality
- **Command race conditions** — `mesh_commands.json` read/write/delete between two processes is fragile
- **Monolithic dashboard.py** — 689-line file mixing routes, DB logic, mock data, command parsing
- **Flask is blocking** — single-threaded WSGI server cannot push data; browser must pull

## 2. Goal

**Replace the TUI with a real-time web dashboard** that:

1. **Pushes data instantly** via WebSockets (no polling, no JSON file IPC)
2. **Stores history** in SQLite with WAL mode for concurrent reads/writes
3. **Visualizes the mesh** with a D3.js topology graph and Chart.js time-series
4. **Sends commands** through WebSocket (no file-based command queue)
5. **Runs on any browser** — phone, laptop, tablet, anywhere on the WireGuard VPN
6. **Is modular** — each component in its own file, following v0.6.2 best practices

### Target Architecture

```
Browser(s)
  ↕  WebSocket (ws://pi5:8000/ws)
  ↕  REST API  (http://pi5:8000/api/*)
FastAPI Server (embedded in gateway process)
  ↕  In-process Python calls (no file IPC)
DCMonitorGateway + PowerManager
  ↕  BLE GATT
Universal Node(s)
```

**Key difference from v0.6.3:** No JSON files, no separate Flask process, no polling. The FastAPI server lives inside the same process as the gateway, so it has direct access to `DCMonitorGateway` and `PowerManager` objects. WebSockets push data the millisecond a BLE notification arrives.

### Development Environment

All development and testing is done on **Windows** with real ESP32-C6 nodes connected via BLE. The Python gateway (TUI + web server) runs natively on Windows using `bleak`. No mock mode is needed — real BLE data flows from the start. Final deployment to Pi 5 is for WireGuard remote access and production use.

## 3. Four-Phase Approach

> [!IMPORTANT]
> Each phase is independently testable. Complete and verify each phase before starting the next.
> Phases are ordered by dependency — later phases build on earlier ones.

---

### Phase 1: Backend — FastAPI + WebSocket Server (Python only)

**Scope:** Add a FastAPI server with WebSocket support to the gateway process. No frontend yet — just the backend APIs.

**What changes:**

| File | Change |
|------|--------|
| `gateway-pi5/gateway-code/web_server.py` | **[NEW]** FastAPI app, WebSocket manager, REST endpoints |
| `gateway-pi5/gateway-code/db.py` | **[NEW]** SQLite database module (WAL mode, sensor history, settings) |
| `gateway-pi5/gateway-code/dc_gateway.py` | Add event hooks: `on_sensor_data`, `on_connect`, `on_disconnect` |
| `gateway-pi5/gateway-code/power_manager.py` | Add event hook: `on_pm_update` |
| `gateway-pi5/gateway-code/gateway.py` | Start FastAPI server alongside TUI (flag: `--web`) |
| `gateway-pi5/requirements.txt` | Add `fastapi`, `uvicorn[standard]`, `websockets` |

**Risk:** 🟡 Medium — integrating async FastAPI with the existing BLE asyncio loop requires careful event loop management
**Effort:** ~300 lines of Python across 4 modules

**Key design decisions:**

- FastAPI runs on the **same asyncio event loop** as the BLE thread (uvicorn embedded)
- WebSocket manager holds a set of connected clients and broadcasts via `asyncio.Queue`
- `db.py` uses `sqlite3` with WAL mode for lock-free concurrent reads
- Gateway events are Python callbacks, not file writes

#### How to Test Phase 1

1. Start gateway with `python gateway.py --web`
2. Open `ws://localhost:8000/ws` in a WebSocket client (e.g., websocat, Postman)
3. Send a `read` command on the TUI — WebSocket should receive the sensor data JSON immediately
4. `GET http://localhost:8000/api/state` returns current mesh state
5. `GET http://localhost:8000/api/history?node_id=0&minutes=5` returns historical readings
6. `POST http://localhost:8000/api/command {"command": "0:READ"}` sends a command and returns the result

**Phase 1 is DONE when:** WebSocket pushes live sensor data, REST API returns state/history, and commands can be sent via HTTP POST — all with zero JSON file IPC.

---

### Phase 2: Frontend — Dashboard UI (HTML/CSS/JS only)

**Scope:** Build the web frontend. No backend changes — assumes Phase 1 APIs are stable.

**What changes:**

| File | Change |
|------|--------|
| `gateway-pi5/dashboard/index.html` | **[NEW]** Main HTML shell with layout structure |
| `gateway-pi5/dashboard/css/style.css` | **[NEW]** Dark theme, glassmorphism, responsive grid |
| `gateway-pi5/dashboard/js/app.js` | **[NEW]** WebSocket connection, state management, event dispatch |
| `gateway-pi5/dashboard/js/topology.js` | **[NEW]** D3.js force-directed mesh graph |
| `gateway-pi5/dashboard/js/charts.js` | **[NEW]** Chart.js time-series (power, voltage, current) |
| `gateway-pi5/dashboard/js/nodes.js` | **[NEW]** Node card rendering, inline command menus |
| `gateway-pi5/dashboard/js/console.js` | **[NEW]** Live command console with log streaming |
| `gateway-pi5/gateway-code/web_server.py` | Add static file serving route for `/dashboard/*` |

**Risk:** 🟢 Low — pure frontend, no BLE or firmware changes
**Effort:** ~800 lines across HTML/CSS/JS

**Key design decisions:**

- **Vanilla JS** — no React/Vue/build step. Just ES modules loaded via `<script type="module">`
- **Single WebSocket connection** — `app.js` manages connect/reconnect and dispatches events to other modules
- **D3.js for topology** — fixed positions by role (Pi 5 at top, gateway below, nodes at bottom), smooth enter/update/exit
- **Chart.js for time-series** — rolling 30-minute window, one chart per node
- **CSS Grid layout** — header / topology+sidebar / node cards / console / footer

#### Dashboard Layout

```
┌──────────────────────────────────────────────────────────────┐
│  DC Monitor Mesh Gateway                  ● Connected  ⚙️    │
├─────────────────────────────┬────────────────────────────────┤
│                             │  Power Manager                 │
│   D3.js Topology            │  ┌──────────────────────┐      │
│                             │  │ Threshold: 5000mW    │      │
│    [Pi5] ── [N1*]           │  │ Budget:    4500mW    │      │
│              / \            │  │ Total:     4243mW ██ │      │
│           [N0]  [R1]        │  │ Priority:  Node 2    │      │
│                             │  └──────────────────────┘      │
├─────────────────────────────┴────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌──────────────────┐     │
│  │ Node 0   ⚙️ │  │ Node 1   ⚙️ │  │ Relay 1          │     │
│  │ D:30% V:12V │  │ D:41% V:12V │  │ Status: Active   │     │
│  │ I:196mA     │  │ I:194mA     │  │ Last seen: 2s    │     │
│  │ P:2357mW    │  │ P:2314mW    │  │                  │     │
│  │ ████████░░  │  │ ████████░░  │  │                  │     │
│  └─────────────┘  └─────────────┘  └──────────────────┘     │
├──────────────────────────────────────────────────────────────┤
│  📊 History    (Chart.js power/voltage graphs)               │
├──────────────────────────────────────────────────────────────┤
│  > read                                                      │
│  [00:40:08] NODE1 >> D:0%,V:0.254V,I:0.00mA,P:0.0mW        │
│  [00:40:09] NODE0 >> D:0%,V:0.111V,I:1.25mA,P:0.1mW        │
│  > _                                                         │
└──────────────────────────────────────────────────────────────┘
```

#### How to Test Phase 2

1. Start gateway on Windows: `python gateway.py --web` (BLE nodes connected)
2. Open `http://localhost:8000` in a browser
3. Verify: dark theme, topology graph shows Pi 5 + connected nodes
4. Send `read` in TUI → node cards update in real-time in the browser (no page refresh)
5. Click ⚙️ on a node card → set duty → confirm command executes
6. Type `read` in console → output appears immediately
7. History tab shows Chart.js graphs with rolling 30-minute power data

**Phase 2 is DONE when:** The browser shows a fully functional dashboard with real-time data, interactive node cards, topology graph, charts, and console — all driven by WebSocket.

---

### Phase 3: Polish — Resilience, History Charts, Settings

**Scope:** Edge cases, data retention, and user settings. Makes the dashboard production-ready.

**What changes:**

| File | Change |
|------|--------|
| `gateway-pi5/dashboard/js/app.js` | WebSocket auto-reconnect with exponential backoff |
| `gateway-pi5/dashboard/js/nodes.js` | Stale/offline visual states (fade nodes after 10s, red after 30s) |
| `gateway-pi5/dashboard/js/charts.js` | Configurable time window (5m, 30m, 1h, 24h), per-node charts |
| `gateway-pi5/dashboard/css/style.css` | Responsive breakpoints (mobile, tablet, desktop) |
| `gateway-pi5/gateway-code/db.py` | Data retention policy (auto-purge readings older than 7 days) |
| `gateway-pi5/gateway-code/web_server.py` | Settings API (threshold, budget, priority, poll interval) |

**Risk:** 🟢 Low — incremental improvements on a working system
**Effort:** ~200 lines

#### How to Test Phase 3

1. Kill the gateway → browser shows "Disconnected" overlay → restart → auto-reconnects
2. Power-cycle a node → its card fades to "stale" → "offline" → glows green when it recovers
3. Switch history chart to 1h window → verify data loads from SQLite
4. On Pi 5 deployment: open on phone (WireGuard) → verify responsive layout
5. Check `mesh_data.db` size after 7+ days → old records purged

**Phase 3 is DONE when:** Dashboard handles all edge cases gracefully and has production-grade resilience.

---

### Phase 4: Integration Test & TUI Deprecation

**Scope:** End-to-end validation. Decide whether to keep TUI as fallback.

#### Integration Test Checklist

- [ ] 2+ universal nodes provisioned and running
- [ ] Browser shows topology with correct node count
- [ ] ALL:READ data appears on dashboard within 500ms
- [ ] Set duty from node card → node responds → dashboard updates
- [ ] Power-cycle connected node → failover → dashboard shows new connection
- [ ] PM threshold set from dashboard → balancing starts → charts show convergence
- [ ] Console sends commands and shows responses
- [ ] History charts show 30-minute rolling data
- [ ] Deploy to Pi 5 → open on phone via WireGuard → full functionality
- [ ] Gateway runs 24h without dashboard crashes or memory leaks

#### Cleanup

- [ ] Add `--web-only` flag to `gateway.py` (no TUI, web dashboard only)
- [ ] Update `README.md` with web dashboard setup
- [ ] Tag the repo as `v0.7.1`

**Phase 4 is DONE when:** All checklist items pass. The web dashboard is the primary interface.

---

## 4. Key Technical Challenges

### Challenge 1: Embedding FastAPI in the BLE Event Loop

The gateway already uses `asyncio` for BLE operations via `BleThread`. FastAPI/uvicorn also needs an event loop. Solution: run uvicorn programmatically on the same event loop using `uvicorn.Server` with `config.setup_event_loop = False`.

### Challenge 2: Thread-Safe WebSocket Broadcasting

BLE notifications arrive on the bleak callback thread. WebSocket sends must happen on the asyncio loop. Solution: use `asyncio.run_coroutine_threadsafe()` to schedule broadcasts from the notification handler, similar to how the TUI uses `call_from_thread()`.

### Challenge 3: SQLite Concurrency

The gateway writes sensor data frequently; the web server reads it for history queries. Solution: WAL mode + separate read/write connections + `check_same_thread=False`.

### Challenge 4: Keeping the TUI Working

Some users may prefer the TUI. Solution: `--web` flag enables the web server alongside TUI. `--web-only` disables TUI entirely. Default behavior (no flags) is TUI-only for backward compatibility.

---

## 5. Lessons Learned from v0.6.3

| Problem | v0.6.3 Approach | v0.7.1 Fix |
|---------|-----------------|------------|
| Data staleness | JSON file + 2s browser polling | WebSocket push on every BLE notification |
| Command latency | Write JSON → gateway reads next poll | WebSocket → in-process function call |
| Dashboard crashes | Separate Flask process, file contention | Embedded in gateway process |
| Monolithic code | 689-line `dashboard.py` | Modular: `web_server.py`, `db.py`, separate JS modules |
| No history | Ephemeral JSON state only | SQLite with WAL mode, 7-day retention |
| Jittery graph | D3 force restart on every update | Only restart on topology changes, smooth data transitions |

---

## 6. Tech Stack

| Layer | Technology | Justification |
|-------|-----------|---------------|
| Backend | FastAPI + Uvicorn | Async-native, WebSocket support, shares asyncio loop with BLE |
| Database | SQLite + WAL | Zero-config, embedded, fast enough for <10 nodes at 1 reading/sec |
| WebSocket | `websockets` library (via FastAPI) | Real-time push, no polling |
| Frontend | Vanilla HTML/CSS/JS | No build step, no React/Vue complexity, runs anywhere |
| Topology | D3.js v7 (CDN) | Industry standard for network graphs |
| Charts | Chart.js v4 (CDN) | Lightweight time-series, no build step |
| Font | Inter (Google Fonts) | Clean, modern, great for dashboards |

---

## 7. File Structure (v0.7.1 Final)

```
gateway-pi5/
├── gateway-code/
│   ├── gateway.py          # Entry point — add --web and --web-only flags
│   ├── dc_gateway.py       # Add event hooks (on_sensor_data, on_connect, etc.)
│   ├── power_manager.py    # Add on_pm_update hook
│   ├── web_server.py       # [NEW] FastAPI + WebSocket + REST API
│   ├── db.py               # [NEW] SQLite module (history, settings)
│   ├── ble_thread.py       # (no changes)
│   ├── node_state.py       # (no changes)
│   └── constants.py        # (no changes)
├── dashboard/
│   ├── index.html          # [NEW] Main HTML shell
│   ├── css/
│   │   └── style.css       # [NEW] Dark theme, glassmorphism
│   └── js/
│       ├── app.js          # [NEW] WebSocket manager, state dispatcher
│       ├── topology.js     # [NEW] D3.js mesh graph
│       ├── charts.js       # [NEW] Chart.js time-series
│       ├── nodes.js        # [NEW] Node cards + inline commands
│       └── console.js      # [NEW] Command console + log stream
└── requirements.txt        # Add fastapi, uvicorn, websockets
```
