# Scan and Connect by Address Flow

Command example:

`python gateway.py --address 98:A3:16:B1:C9:8A`

## What happens

1. CLI args parsed (`--address`, `--scan`, `--node`, etc.)  
   `gateway-pi5/gateway.py:1613`
2. Scan starts via `scan_for_nodes()`  
   `gateway-pi5/gateway.py:726`
3. If address provided, scanner matches exact MAC  
   `gateway-pi5/gateway.py:739`
4. Selected device is passed to `connect_to_node()`  
   `gateway-pi5/gateway.py:855`
5. BLE connect is attempted
6. `start_notify()` subscribes to sensor char notifications  
   `gateway-pi5/gateway.py:875`

## Why this matters

If many BLE devices appear, `--address` removes ambiguity and avoids connecting to wrong device.

## Quick checks

1. Device found in scan output
2. "Subscribed to sensor notifications" appears
3. Running `read` returns `NODEX:DATA:...`
