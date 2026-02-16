# Relay Out-of-Range Flow

This flow explains when relay truly matters.

## Case A: Node in direct range

Gateway mesh message reaches sensing node directly.
Relay is present but may not be used for that packet.

## Case B: Node out of direct range

Gateway cannot directly reach distant node reliably.
Mesh forwards through relay path.
Response returns through available path.

## Why this is important

Relay value is conditional on RF topology.
It provides resilience/range extension, not necessarily constant active forwarding.

## Related code/config anchors

- Relay firmware role: `ESP/ESP-Mesh-Relay-Node/main/main.c`
- Relay enabled in sensing nodes: `ESP/ESP-Mesh-Node-sensor-test/main/main.c:103`
- Group send path at gateway: `ESP/ESP_GATT_BLE_Gateway/main/main.c:663`

Diagram:

- `Drawio/08-Relay-Out-of-Range.drawio`
