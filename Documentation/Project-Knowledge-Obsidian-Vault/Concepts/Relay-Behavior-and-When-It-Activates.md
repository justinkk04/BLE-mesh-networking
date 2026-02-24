# Relay Behavior and When It Activates

## Key idea

Relay nodes are not always "actively used."

They become useful when a direct path is weak or unavailable.

## In-range case

If sensing node is in direct range of gateway, mesh can deliver directly.
Relay may be idle most of the time.

## Out-of-range / weak-link case

If node is too far or blocked, mesh can forward via relay hops.
Then relay becomes part of the effective route.

## Why keep relay even when idle

1. Range insurance
2. Path redundancy
3. Better resilience to changing RF conditions

## In this repo

Relay-only firmware:

- no sensor vendor responses
- no duty/read application behavior
- packet forwarding role + heartbeat + NVS restore

Refs:

- `ESP/ESP-Mesh-Relay-Node/main/main.c`
- relay enabled in sensing config too: `ESP/ESP-Mesh-Node-sensor-test/main/main.c:103`

## Discovery nuance on Pi

Pi can count mesh devices from BLE scan, but relay nodes may not answer sensing READ probes.
That is expected behavior, not a bug.
