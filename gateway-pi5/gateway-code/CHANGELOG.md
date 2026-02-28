# Changelog

## [v0.7.1] - WIP

### Added

- **Web Dashboard Backend (Phase 1)**
  - Embedded FastAPI web server running natively alongside the textual TUI without multithreading conflicts (`web_server.py`).
  - Added REST API endpoints (`/api/state`, `/api/history`, `/api/command`) for mesh interaction.
  - Added SQLite database with WAL configuration (`db.py`) for efficient, non-blocking time-series sensor data logging.
  - Real-time WebSocket (`/ws`) implementation broadcasting node updates and power manager states.
  - New `--web`, `--web-only`, and `--web-port` CLI flags in `gateway.py` for headless or dual-mode operations.
  
### Changed

- `dc_gateway.py` now hooks into the web server to broadcast connectivity and live sensor feeds asyncronously to the event loop.
- `power_manager.py` now broadcasts `"pm_update"` messages when power limits are hit and re-balancing occurs.
- Configured gateway dev environments to allow testing with live physical ESP32-C6 nodes using TUI inputs alongside web clients.
