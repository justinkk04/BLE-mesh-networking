# ESP Relay Node Map

File: `ESP/ESP-Mesh-Relay-Node/main/main.c`

## Role

Range extender and path redundancy.

It relays packets. It does not read sensors and does not answer vendor `READ`/`DUTY`.

## Key Sections

- Relay config server: `config_server` at `:81`
- Heartbeat task: `led_heartbeat_task()` at `:128`
- Provisioning callback: `provisioning_cb()` at `:161`
- Config server callback: `config_server_cb()` at `:200`

## What You Should See

- Fast LED blink before provisioning
- Slow LED blink after provisioning
- No sensor data responses from relay

That "silent relay" behavior is expected.
