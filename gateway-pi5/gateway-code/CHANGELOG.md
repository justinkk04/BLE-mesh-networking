# Changelog

## [v0.7.1] - WIP

### Added

- **Web Dashboard Backend (Phase 1)**
  - Embedded FastAPI web server running natively alongside the textual TUI without multithreading conflicts (`web_server.py`).
  - Added REST API endpoints (`/api/state`, `/api/history`, `/api/command`) for mesh interaction.
  - Added SQLite database with WAL configuration (`db.py`) for efficient, non-blocking time-series sensor data logging.
  - Real-time WebSocket (`/ws`) implementation broadcasting node updates and power manager states.
  - New `--web`, `--web-only`, and `--web-port` CLI flags in `gateway.py` for headless or dual-mode operations.

- **Web Dashboard Frontend (Phase 2)**
  - Dark-themed dashboard UI with glassmorphism cards and CSS Grid responsive layout (`dashboard/index.html`, `css/style.css`).
  - D3.js force-directed mesh topology graph showing Pi 5 → GATT node → remote nodes with live status coloring (`js/topology.js`).
  - Real-time node cards with duty/voltage/current/power metrics, power bars, and online/stale/offline status badges (`js/nodes.js`).
  - Chart.js time-series power chart with area fill, formatted tooltips, and 5m/30m/1h/24h time windows (`js/charts.js`).
  - Live command console with input history (up/down arrows), auto-scroll, and full command dispatcher (`js/console.js`).
  - WebSocket manager with auto-reconnect and exponential backoff (`js/app.js`).
  - Command dispatcher in `web_server.py` parsing user-friendly commands (`read`, `node 0 duty 50`, etc.) into BLE gateway calls.

### Changed

- `dc_gateway.py` now hooks into the web server to broadcast connectivity and live sensor feeds asyncronously to the event loop.
- `power_manager.py` now broadcasts `"pm_update"` messages when power limits are hit and re-balancing occurs.
- Configured gateway dev environments to allow testing with live physical ESP32-C6 nodes using TUI inputs alongside web clients.
