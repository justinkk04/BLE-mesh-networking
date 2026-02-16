# ESP Relay Node (ESP-IDF)

Main file:

- `ESP/ESP-Mesh-Relay-Node/main/main.c`

Read these sections first:

1. `config_server` at `:81`
2. `led_heartbeat_task()` at `:128`
3. `provisioning_cb()` at `:161`
4. `config_server_cb()` at `:200`

Key point:

This node relays packets and does not serve sensor/vendor command responses.

Then read [[Components/ESP-Relay-Node-Map]].
