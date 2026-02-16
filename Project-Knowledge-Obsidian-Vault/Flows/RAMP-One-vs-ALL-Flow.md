# RAMP: One Node vs ALL Nodes

## One-node ramp

Example:

- target `node 1`
- command `ramp`

Path:

1. Pi builds `1:RAMP`
2. GATT gateway maps to node-native `r`
3. Sends unicast vendor command
4. One node executes ramp sequence

## ALL ramp

Example:

- target `node ALL`
- command `ramp`

Path:

1. Pi builds `ALL:RAMP`
2. GATT gateway routes to group `0xC000`
3. All subscribed sensing nodes execute ramp behavior
4. Responses come back per node

## Practical difference

- One-node: isolates test/control to one load
- ALL: synchronized multi-node action, useful for broad test but higher total power impact

Code anchors:

- `send_to_node()` ALL/single dispatch: `gateway-pi5/gateway.py:929`
- GATT command parse/map: `ESP/ESP_GATT_BLE_Gateway/main/main.c:563`
- Group send path: `ESP/ESP_GATT_BLE_Gateway/main/main.c:663`
- Sensing node command processing: `ESP/ESP-Mesh-Node-sensor-test/main/main.c:363`

Diagram:

- `Drawio/09-RAMP-One-vs-ALL.drawio`
