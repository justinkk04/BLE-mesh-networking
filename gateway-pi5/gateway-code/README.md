# Pi 5 Python Gateway

**Runtime:** Python 3 · **Dependencies:** bleak (BLE), textual (TUI)

Connects to the ESP32-C6 GATT Gateway via BLE and provides a TUI (or CLI) for controlling and monitoring the mesh network. Includes an equilibrium-based power manager that automatically balances load across nodes.

## Architecture

```
[THIS CODE] ──GATT──> ESP32-C6 GATT Gateway ──Mesh──> Nodes
   Pi 5                Service 0xDC01
```

## Module Map

| File | Responsibility | Key Class/Functions |
|---|---|---|
| `gateway.py` | **Entry point** — argparse, TUI/CLI mode selection | `main()`, `_run_cli()` |
| `dc_gateway.py` | BLE scanning, GATT connection, notification parsing, command routing | `DCMonitorGateway` |
| `tui_app.py` | Textual TUI — sidebar, DataTable, RichLog, command dispatch | `MeshGatewayApp` |
| `power_manager.py` | Equilibrium-based power balancer with priority weighting | `PowerManager` |
| `ble_thread.py` | Dedicated asyncio event loop for bleak BLE I/O | `BleThread` |
| `node_state.py` | Per-node state tracking dataclass | `NodeState` |
| `constants.py` | UUIDs, regex patterns, device name prefixes | `DC_MONITOR_SERVICE_UUID`, `SENSOR_RE` |

## Import Graph

```
gateway.py ──> dc_gateway.py ──> power_manager.py ──> node_state.py
    |               |                   |
    └──> tui_app.py ──> ble_thread.py   └──> constants.py
              └──> dc_gateway.py
```

No circular imports. `power_manager.py` uses `TYPE_CHECKING` guard for `DCMonitorGateway` type hint.

## Usage

```bash
# TUI mode (default — requires textual)
python gateway.py

# Plain CLI mode
python gateway.py --no-tui

# One-shot commands
python gateway.py --scan
python gateway.py --node 0 --ramp
python gateway.py --node 1 --duty 50
python gateway.py --node all --stop
python gateway.py --node 0 --read
python gateway.py --node 0 --monitor
python gateway.py --address AA:BB:CC:DD:EE:FF
```

## TUI Commands

| Command | Action |
|---|---|
| `node <0-9/ALL>` | Switch target node |
| `duty <0-100>` | Set duty cycle |
| `ramp` / `r` | Start ramp test |
| `stop` / `s` | Stop load |
| `read` | Single sensor reading |
| `monitor` / `m` | Continuous monitoring |
| `threshold <mW>` | Enable power management |
| `priority <id>` | Set priority node |
| `threshold off` | Disable power management |
| `power` | Show PM status |
| `debug` / `d` / F2 | Toggle debug mode |
| `clear` / F3 | Clear log |

## Power Manager

The `PowerManager` maintains total power near `threshold - 500mW headroom` by nudging duty cycles each poll cycle:

- **Equal shares:** Each node gets `budget / N` milliwatts
- **Priority mode:** Priority node gets `2×` normal share
- **Bidirectional:** Increases duty when under budget, decreases when over
- **Gradual:** Estimates mW/% per node to calculate ideal duty
- **Dead band:** Skips adjustments when within 5% of budget

## Key Design Decisions

- **`BleThread`** exists because bleak on Linux/BlueZ needs a persistent event loop for D-Bus signal delivery. Textual's `@work` workers create short-lived loops that miss notifications.
- **`_HAS_TEXTUAL`** is duplicated in `dc_gateway.py` (log routing) and `gateway.py` (mode selection). `tui_app.py` imports textual unconditionally.
- **Chunked notification reassembly** in `notification_handler()`: `+` prefix = continuation, no prefix = final chunk.

## Install

```bash
pip install bleak textual
```

## Adding New Modules

1. Create `new_module.py` with one class
2. Import from the appropriate existing module
3. Keep `gateway.py` as thin entry point — don't add logic there
