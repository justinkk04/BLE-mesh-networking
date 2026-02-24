# Remote Access: WireGuard + SSH Workflow

Goal: control Pi 5 from university over VPN safely.

## Topology

Laptop (uni) -> WireGuard tunnel -> Home network -> Pi 5 (SSH) -> run `gateway.py`

## Practical setup model

1. Home server hosts WireGuard
2. Laptop is WireGuard client
3. Pi 5 is reachable on home LAN from WireGuard peers
4. SSH into Pi 5 through WG IP path
5. Run BLE gateway commands on Pi terminal

## Daily operation flow

1. Connect laptop to WireGuard
2. Verify tunnel (ping WG/home target)
3. SSH to Pi 5
4. Activate environment
5. Run:
   - `python gateway.py --address 98:A3:16:B1:C9:8A`
6. Send commands in TUI/CLI (`node ALL`, `read`, `threshold`, `priority`)

## Why this works

BLE never leaves your home.

Only terminal control traffic crosses VPN/SSH.

## Troubleshooting checklist

1. WireGuard connected?
2. Can you SSH to Pi?
3. Is BLE gateway device powered?
4. Does `gateway.py --scan` find expected device?
5. If not, use exact `--address` again.
