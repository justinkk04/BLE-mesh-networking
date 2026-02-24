# Self-Healing and Auto-Rejoin (Power Loss Recovery)

## What "self-healing" means here

At current state, this repo has partial self-healing behavior:

1. Nodes persist mesh credentials in NVS.
2. After power returns, nodes restore state and rejoin mesh automatically.
3. Relay paths can provide alternate radio routes.

Future failover logic can extend this further at system-control level.

## Where auto-rejoin comes from

### Sensing node

- saves node state: `save_node_state()`  
  `ESP/ESP-Mesh-Node-sensor-test/main/main.c:163`
- restores node state: `restore_node_state()`  
  `ESP/ESP-Mesh-Node-sensor-test/main/main.c:171`

### Relay node

- saves relay state: `save_node_state()`  
  `ESP/ESP-Mesh-Relay-Node/main/main.c:63`
- restores relay state: `restore_node_state()`  
  `ESP/ESP-Mesh-Relay-Node/main/main.c:71`

### Gateway node (ESP GATT gateway firmware)

- saves gateway mesh state: `save_gw_state()`  
  `ESP/ESP_GATT_BLE_Gateway/main/main.c:116`
- restores mesh bind info: `restore_gw_state()`  
  `ESP/ESP_GATT_BLE_Gateway/main/main.c:124`

## Important distinction

Auto-rejoin after power cycle is not the same as full controller failover.

It restores node mesh membership/state quickly, but does not by itself create a second Pi control brain.
