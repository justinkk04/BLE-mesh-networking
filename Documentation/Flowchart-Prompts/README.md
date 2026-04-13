# Flowchart Prompt Templates

Reusable prompts for generating `.drawio` flowchart files. Each `.md` file contains a self-contained prompt you can copy and paste into a new AI conversation to generate the corresponding draw.io XML file.

## How to Use

1. Open a new conversation
2. Open the `.md` file for the flowchart you want
3. Copy everything **below the `---` line**
4. Paste it as your prompt
5. The AI will generate a `.drawio` XML file and save it to `Documentation/Flowcharts/`

## Flowcharts

| # | File | Diagram | Description |
|---|------|---------|-------------|
| 1 | [01-SYSTEM-ARCHITECTURE-OVERVIEW.md](01-SYSTEM-ARCHITECTURE-OVERVIEW.md) | System Architecture | Full end-to-end system: Pi 5, ESP nodes, provisioner, web browser, all protocols |
| 2 | [02-ESP32-UNIVERSAL-NODE-FIRMWARE-FLOW.md](02-ESP32-UNIVERSAL-NODE-FIRMWARE-FLOW.md) | ESP32 Firmware | Boot sequence + 3 runtime event sources (GATT, Mesh Server, Mesh Client) |
| 3 | [03-PI5-GATEWAY-FLOW.md](03-PI5-GATEWAY-FLOW.md) | Pi 5 Gateway | Startup mode selection, BLE connection, notification handler, command dispatch |
| 4 | [04-POWER-MANAGER-ALGORITHM-FLOW.md](04-POWER-MANAGER-ALGORITHM-FLOW.md) | Power Manager | Equilibrium balancing: poll loop, deadband, cooldown, nudging, priority |
| 5 | [05-COMMAND-FLOW-TUI-AND-WEB.md](05-COMMAND-FLOW-TUI-AND-WEB.md) | Command Flow | Complete command lifecycle through TUI and Web UI to mesh and back |
| 6 | [06-AUTO-RECONNECT-FAILOVER-FLOW.md](06-AUTO-RECONNECT-FAILOVER-FLOW.md) | Auto-Reconnect | v0.7.0 failover: disconnect detection, scan, try alternates, resume PM |
| 7 | [07-PWM-LOAD-DRIVE-CIRCUIT-FLOW.md](07-PWM-LOAD-DRIVE-CIRCUIT-FLOW.md) | PWM Load Drive Circuit | ESP32 GPIO → Q1 inverter → Q2 power switch → 12V load; inversion logic + failsafe |
| 8 | [08-POWER-MANAGER-ALGORITHM-FLOW.md](08-POWER-MANAGER-ALGORITHM-FLOW.md) | Power Manager Algorithm | Equilibrium poll loop, deadband, cooldown, equal vs priority balancing, nudge + ceiling |

## Output Location

Generated `.drawio` files save to: `Documentation/Flowcharts/`
