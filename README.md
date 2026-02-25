# BLE Mesh DC Power Monitor

**Version:** v0.6.2 (Modular Cleanup)
**Status:** Stable / In Development

A self-healing BLE Mesh network for remote DC power monitoring and control. The system uses ESP32-C6 nodes to read INA260 sensors directly via I2C and control loads via PWM, bridged to a Raspberry Pi 5 gateway for automated power management.

## System Architecture

```
Pi 5 (Python TUI Gateway)
  |  BLE GATT (Service 0xDC01)
  v
ESP32-C6 GATT Gateway
  |  BLE Mesh (Vendor Model 0x02E5)
  v
ESP32-C6 Relay Node(s) ──── Extends range, TTL=7
  |  (silent relay)
  v
ESP32-C6 Mesh Node(s)
  |  I2C (INA260) + LEDC PWM
  v
INA260 sensor + Load Circuit
```

## Key Features

- **Direct Sensing:** ESP32-C6 reads INA260 voltage/current/power directly (no secondary MCU).
- **Power Manager:** Equilibrium-based balancing algorithm maintains total power budget across N nodes.
- **Group Addressing:** O(1) polling using BLE Mesh group broadcasts (0xC000) for massive efficiency gains.
- **Relay Nodes:** Dedicated relay-only nodes extend mesh range with LED heartbeat and NVS auto-rejoin — no sensors, no vendor model.
- **Dynamic Discovery:** Automatically detects sensing nodes vs relays from BLE scan.
- **Event-Driven Pacing:** Fast command execution (~300ms/node) with no fixed delays.
- **TUI Gateway:** Textual-based terminal UI with live node table, scrolling logs, and debug mode.
- **Robustness:** 16+ critical bugs fixed (race conditions, ghost nodes, deadbands).

## Project Structure

All codebases are modular (v0.6.2) — each `main.c` is a thin orchestrator calling focused module files.

- `ESP/ESP-Provisioner/` — Auto-provisioner (`main.c` + `provisioning`, `mesh_config`, `node_registry`, `composition`, `model_binding` modules).
- `ESP/ESP_GATT_BLE_Gateway/` — Pi 5 ↔ Mesh bridge (`main.c` + `gatt_service`, `mesh_gateway`, `command_parser`, `monitor`, `node_tracker` modules).
- `ESP/ESP-Mesh-Node-sensor-test/` — Sensing nodes (`main.c` + `sensor`, `load_control`, `command`, `mesh_node` modules).
- `ESP/ESP-Mesh-Relay-Node/` — Relay-only nodes (`main.c` + `mesh_relay`, `led` modules).
- `gateway-pi5/gateway-code/` — Python 3 gateway (`gateway.py` entry point + `dc_gateway`, `tui_app`, `power_manager`, `ble_thread`, `node_state`, `constants` modules).
- `Documentation/` — Versioned docs, changelogs, and implementation guides.
- `MESH_IMPLEMENTATION.md` — Detailed technical documentation.

## Roadmap

- **v0.5.0 (Complete):** Removed Pico 2W (reduced latency 500ms→50ms, simplified architecture). PowerManager refinement, dynamic discovery.
- **v0.6.0 (Complete):** Group addressing implemented. Poll time reduced from O(N) to O(1).
- **v0.6.1 (Complete):** Relay-only node for range extension and path redundancy.
- **v0.6.01 (Complete):** Hardware failsafe mod — Q1 base pull-up moved from 3.3V to 12V (load stays OFF if ESP loses power).
- **v0.6.2 (Current):** Modular code cleanup — split all monolithic files into single-responsibility modules (26 module files across 5 codebases, zero behavior changes).
- **v0.7.0 (Next):** Self-healing gateway failover.
- **v1.0.0:** Final stress testing and release.

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
