# WireGuard Concepts (for this project)

## What WireGuard is

WireGuard is a VPN tunnel protocol.

It creates a secure private network path between your remote laptop and home network.

## Why it matters here

Your BLE radios stay at home.

From university, you do:

1. WireGuard tunnel to home
2. SSH into Pi 5
3. Run `gateway.py` on Pi
4. Pi talks BLE locally to ESP gateway

So remote control traffic is VPN+SSH, while BLE traffic stays local.

## Components in your setup

1. WireGuard server/host at home
2. Laptop as WireGuard client
3. Pi 5 reachable over the VPN-routed home path

## Security model

- Encrypted tunnel between peers
- Private addressing on tunnel
- No need to expose Pi BLE services publicly

## Typical workflow

1. Connect WG on laptop
2. Test tunnel reachability
3. SSH to Pi
4. Run:
   - `python gateway.py --address 98:A3:16:B1:C9:8A`

See:

- `[[Architecture/Remote-Access-WireGuard-and-SSH]]`
- `Drawio/06-WireGuard-SSH-Remote-Control.drawio`
