# BLE Mesh DC Power Monitor

**Version:** v0.7.0 (Gateway Failover & Universal Nodes)
**Status:** Stable / In Development

A self-healing BLE Mesh network for remote DC power monitoring and control. The system uses ESP32-C6 "universal nodes" that each function as both a sensor AND a GATT gateway, bridged to a Raspberry Pi 5 for automated power management with automatic failover.

## System Architecture (v0.7.0)

```
Pi 5 (Python TUI Gateway)
  |  BLE GATT (Service 0xDC01)
  |  Auto-connects to any Universal Node
  |  Fails over to another node on disconnect
  v
ESP32-C6 Universal Node (sensor + gateway)  ◄── Connected node
  |  INA260 I2C + LEDC PWM (local sensor)
  |  BLE Mesh (Vendor Server + Client models)
  v
ESP32-C6 Universal Node(s)  ◄── Remote nodes
  |  INA260 I2C + LEDC PWM
  |  Commands arrive via mesh group (0xC000)
  v
ESP32-C6 Relay Node(s) ──── Extends range, TTL=7 (no sensor)
```

**Key change from v0.6.2:** The dedicated GATT gateway is eliminated. Every sensing node can act as the Pi 5's gateway. If the connected node dies, the Pi 5 automatically fails over to another universal node.

## Key Features

- **Universal Nodes:** Every sensor node doubles as a GATT gateway — no dedicated gateway hardware needed.
- **Auto-Failover:** Pi 5 detects BLE disconnection within 2s, scans for available nodes, and reconnects to a different one automatically.
- **Self-Addressing:** When the Pi 5 sends a command targeting the connected node, it processes locally (no mesh round-trip). Remote commands route through mesh normally.
- **Direct Sensing:** ESP32-C6 reads INA260 voltage/current/power via I2C (no secondary MCU).
- **Power Manager:** Equilibrium-based balancing algorithm maintains total power budget across N nodes. Converges in 2-3 cycles.
- **Group Addressing:** O(1) polling using BLE Mesh group broadcasts (0xC000).
- **Relay Nodes:** Dedicated relay-only nodes extend mesh range with LED heartbeat and NVS auto-rejoin.
- **Dynamic Discovery:** Automatically detects and counts sensing nodes from BLE scan.
- **TUI Gateway:** Textual-based terminal UI with live node table, scrolling logs, and debug mode.

## Project Structure

All codebases are modular — each `main.c` is a thin orchestrator calling focused module files.

- `ESP/ESP-Provisioner/` — Auto-provisioner (`main.c` + `provisioning`, `mesh_config`, `node_registry`, `composition`, `model_binding` modules).
- `ESP/ESP-Mesh-Node-sensor-universal/` — **Universal nodes** (sensor + GATT gateway). Modules: `main.c`, `sensor`, `load_control`, `command`, `mesh_node`, `gatt_service`, `command_parser`, `monitor`, `node_tracker`, `nvs_store`.
- `ESP/ESP-Mesh-Relay-Node/` — Relay-only nodes (`main.c` + `mesh_relay`, `led` modules).
- `ESP/ESP_GATT_BLE_Gateway/` — ~~Dedicated GATT gateway~~ **DEPRECATED** (v0.7.0) — functionality merged into universal node. Kept for rollback reference.
- `gateway-pi5/gateway-code/` — Python 3 gateway (`gateway.py` entry point + `dc_gateway`, `tui_app`, `power_manager`, `ble_thread`, `node_state`, `constants` modules).
- `Documentation/` — Versioned docs, changelogs, meeting notes, and implementation guides.
- `MESH_IMPLEMENTATION.md` — Detailed technical documentation.

## Getting Started

### Hardware Required

- 1x Raspberry Pi 5 (gateway host)
- 2+ ESP32-C6 boards (universal nodes — each with INA260 sensor + load circuit)
- 1x ESP32-C6 board (provisioner)
- Optional: ESP32-C6 relay nodes for range extension

### Flashing

1. **Provisioner:** `cd ESP/ESP-Provisioner && idf.py build flash`
2. **Universal Nodes:** `cd ESP/ESP-Mesh-Node-sensor-universal && idf.py set-target esp32c6 && idf.py build flash`
3. **Relay Nodes (optional):** `cd ESP/ESP-Mesh-Relay-Node && idf.py build flash`

### Running the Gateway

```bash
cd gateway-pi5
source .venv/bin/activate  # or .venv\Scripts\activate on Windows
cd gateway-code
python gateway.py
```

The gateway auto-discovers universal nodes by name (`DC-Monitor` or `ESP-BLE-MESH`) and connects to the first one with a valid GATT service. No `--address` flag needed.

## Roadmap

- **v0.5.0 (Complete):** Removed Pico 2W (reduced latency 500ms→50ms, simplified architecture). PowerManager refinement, dynamic discovery.
- **v0.6.0 (Complete):** Group addressing implemented. Poll time reduced from O(N) to O(1).
- **v0.6.1 (Complete):** Relay-only node for range extension and path redundancy.
- **v0.6.01 (Complete):** Hardware failsafe mod — Q1 base pull-up moved from 3.3V to 12V (load stays OFF if ESP loses power).
- **v0.6.2 (Complete):** Modular code cleanup — split all monolithic files into single-responsibility modules (26 module files across 5 codebases, zero behavior changes).
- **v0.7.0 (Current):** Gateway failover — universal nodes, auto-reconnect, PM mesh fixes. Dedicated GATT gateway deprecated.
- **v1.0.0:** Final stress testing, 3+ node integration tests, and release.

## Legacy Architecture (v0.3.0)

> **Note:** This architecture is preserved for historical reference. The system switched to direct ESP32-C6 I2C sensing in v0.5.0 to eliminate the Pico UART bridge.
>
> **Reasons for Removal:**
>
> 1. **Latency:** Reduced round-trip time from ~500ms-2s (variable) to <50ms (synchronous).
> 2. **Reliability:** Eliminated UART bridge issues (stuck flags, mismatched responses).
> 3. **Complexity:** Simplified from a 7-hop async path to a 4-hop synchronous mesh callback.

In v0.3.0, the system used a 3-tier architecture:

- **Pico 2W:** Read INA260 sensor and controlled PWM load. Sent data via UART.
- **ESP32-C6:** Acted as a UART-to-BLE bridge (GATT Server).
- **Pi 5:** GATT Client receiving notifications.

Original documentation:

- [Point-to-Point GATT Documentation](Documentation/pi5-GATT-ESP-pico2w-documentation/GATT_Point_to_Point_Documentation.md)
- [v0.3.0 Progress Report](Documentation/pi5-GATT-ESP-pico2w-documentation/progress_report.md)
