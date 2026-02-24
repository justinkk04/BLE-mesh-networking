# Power Loss Auto-Rejoin Flow

## Scenario

A node loses power, then reboots later.

## Flow

1. Node boots
2. Opens NVS
3. Restores saved mesh state (net/app/address)
4. Rejoins mesh without full reprovision (when state valid)
5. Resumes normal message handling

## Where in code

- Sensing node restore path:
  - `ESP/ESP-Mesh-Node-sensor-test/main/main.c:171`
- Relay node restore path:
  - `ESP/ESP-Mesh-Relay-Node/main/main.c:71`
- Gateway mesh state restore:
  - `ESP/ESP_GATT_BLE_Gateway/main/main.c:124`

## Why this matters

This is the main current "self-healing" behavior in deployed firmware.

Diagram:

- `Drawio/10-Power-Loss-Auto-Rejoin.drawio`
