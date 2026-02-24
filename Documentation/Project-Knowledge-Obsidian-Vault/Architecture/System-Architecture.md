# System Architecture

Core chain:

`Pi 5 gateway.py` -> `ESP GATT Gateway` -> `BLE Mesh` -> `ESP Sensing Node(s)`

Supporting roles:

- Provisioner configures nodes/models/keys
- Relay node forwards packets and extends range

## Code Anchors

- Pi gateway class: `gateway-pi5/gateway.py:683`
- GATT bridge parser: `ESP/ESP_GATT_BLE_Gateway/main/main.c:563`
- Sensing node command engine: `ESP/ESP-Mesh-Node-sensor-test/main/main.c:363`
- Provisioner bind flow: `ESP/ESP-Provisioner/main/main.c:337`

## Command Data Path

1. Pi sends `NODE:COMMAND[:VALUE]`
2. GATT gateway maps and mesh-sends vendor opcode
3. Target node executes local action
4. Node returns status payload
5. GATT gateway notifies Pi
6. Pi parses and updates state/UI/PowerManager
