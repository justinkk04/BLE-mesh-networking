# Prompt: Auto-Reconnect & Failover Flowchart

> **Usage:** Copy everything below the `---` line and paste it as a prompt in a new conversation.

---

## Task

Create a `.drawio` XML file for the **Auto-Reconnect & Failover Flow** (v0.7.0). Save it to `Documentation/Flowcharts/06-auto-reconnect-failover-flow.drawio`.

## Source File
`gateway-pi5/gateway-code/dc_gateway.py` — method `_auto_reconnect_loop()`

## Context
In v0.7.0, every ESP32-C6 sensing node doubles as a GATT gateway (no dedicated gateway hardware). If the Pi 5's connected node dies, it automatically fails over to another universal node. This diagram shows that failover sequence.

## Background Monitor Loop

Show as a continuous loop running on the BLE thread:

1. **Start**: `_auto_reconnect_loop()` called after initial connection
2. **Sleep 2 seconds** (health check interval)
3. Diamond: **`client is None OR not client.is_connected?`**
   - **No** (connected, healthy) → go back to sleep
   - **Yes** (disconnection detected) → continue

## Disconnect Detected

4. Diamond: **`_was_connected?`** (was this a real connection loss, not just startup?)
   - **No** → Diamond: **`_reconnecting?`** → No → go back to sleep
   - **Yes** → First detection, enter reconnect mode:

5. **Log**: `"[RECONNECT] Connection lost! Attempting reconnect..."` (bold red)
6. Set `_was_connected = False`
7. Set `_reconnecting = True`
8. If web enabled: broadcast `"reconnecting"` event to all WebSocket clients
9. **Pause PowerManager** (if active):
   - Set `pm._paused = True`
   - Log `"[RECONNECT] PowerManager paused"`
10. **Clean up**: `client = None`, `connected_device = None`

## Failover Scan

11. **Scan for ALL available nodes**: `scan_for_nodes(timeout=5s)`
12. Diamond: **`Devices found?`**
    - **No** → Log `"No nodes found, retrying in 5s..."` → go back to sleep (step 2)
    - **Yes** → continue

## Try Alternate Nodes First

13. **For each device** (excluding the dead node's address):
    - `connect_to_node(device)` → subscribe to GATT notifications
    - Diamond: **`Connection successful?`**
      - **Yes** → 
        - Log `"[FAILOVER] Connected to <name>"` (bold green)
        - Set `_was_connected = True`, `_reconnecting = False`
        - Store new `_last_connected_address`
        - **Resume PowerManager**: `pm._paused = False`
        - Log `"[FAILOVER] PowerManager resumed"`
        - Web broadcast: `"connected"` event with new device info
        - **Break** → go back to health monitor loop
      - **No** → try next device

## Fall Back to Original Node

14. Diamond: **`All alternates failed AND dead_address exists?`**
    - **Yes** → Try connecting to the original (dead) node:
      - `connect_to_node(dead_device)`
      - Diamond: **`Success?`**
        - **Yes** → Log `"[RECONNECT] Reconnected to original node"` (bold green) → resume PM → break
        - **No** → continue to retry

## No Node Available

15. Log `"[FAILOVER] No node available, retrying in 5s..."` → go back to sleep (step 2)
16. Retry cycle continues indefinitely until a node becomes available

## State Flags Summary (reference box)
| Flag | Default | Set When | Cleared When |
|------|---------|----------|--------------|
| `_was_connected` | `False` | After any successful connection | On disconnect detection |
| `_reconnecting` | `False` | On disconnect detection | On successful reconnect |
| `_last_connected_address` | `None` | After any successful connection | Never (preserved for skip logic) |
| `pm._paused` | `False` | On disconnect (if PM active) | On successful reconnect |

## Web Dashboard Integration (annotation boxes)
- On disconnect: broadcast `{"type": "event", "event": "reconnecting"}` → dashboard shows "Reconnecting..." indicator
- On reconnect: broadcast `{"type": "event", "event": "connected", "data": {"device_name": ..., "device_address": ...}}` → dashboard updates connection status
- During reconnect: all web commands return `"[WARN] Cannot send — reconnecting..."`

## Connection Priority Strategy (annotation)
1. First: try any node EXCEPT the one that just died (failover to alternate)
2. Last resort: try the dead node again (it may have rebooted)
3. This ordering maximizes availability — if the dead node is truly dead, we don't waste time on it first

## Style
- Vertical flow from top (monitor loop) to bottom (retry)
- Clear loop arrows showing the continuous monitoring cycle
- Green path for successful reconnection
- Red path for failed attempts
- Yellow for "retrying" states
- Decision diamonds at each check point
- Dashed box grouping the "Try alternates" and "Fall back to original" sections
- State flag reference table in a separate box
- Annotation notes for web dashboard integration
