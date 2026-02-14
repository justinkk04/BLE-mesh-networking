# BLE Mesh DC Power Monitor

**Version:** v0.5.0 (Power Manager Refinement)
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
ESP32-C6 Mesh Node(s)
  |  I2C (INA260) + LEDC PWM
  v
INA260 sensor + Load Circuit
```

## Key Features

- **Direct Sensing:** ESP32-C6 reads INA260 voltage/current/power directly (no secondary MCU).
- **Power Manager:** Equilibrium-based balancing algorithm maintains total power budget across N nodes.
- **Dynamic Discovery:** Automatically detects sensing nodes vs relays from BLE scan.
- **Event-Driven Pacing:** Fast command execution (~300ms/node) with no fixed delays.
- **TUI Gateway:** Textual-based terminal UI with live node table, scrolling logs, and debug mode.
- **Robustness:** 16+ critical bugs fixed (race conditions, ghost nodes, deadbands).

## Project Structure

- `ESP/` — ESP-IDF firmware for Provisioner, GATT Gateway, and Mesh Nodes.
- `gateway-pi5/` — Python 3 gateway script (CLI/TUI).
- `Documentation/` — Historical docs and implementation details.
- `MESH_IMPLEMENTATION.md` — Detailed technical documentation.

## Roadmap

- **v0.5.0 (Current):** PowerManager refinement, dynamic discovery, stable firmware.
- **v0.6.0 (Next):** Group addressing (0xC000) to reduce poll time from O(N) to O(1).
- **v0.7.0:** Self-healing gateway failover.
- **v1.0.0:** Final stress testing and release.
