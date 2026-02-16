# ESP GATT Gateway Map

File: `ESP/ESP_GATT_BLE_Gateway/main/main.c`

## Role

Bridge between:

- Pi 5 (BLE GATT)
- BLE Mesh nodes (vendor model)

## Core Responsibilities

1. Receive command from Pi over GATT write char
2. Parse `NODE:COMMAND[:VALUE]`
3. Send mesh command (unicast or group)
4. Receive mesh responses
5. Notify Pi over GATT

## Key Functions

- GATT notify helper: `gatt_notify_sensor_data()` at `:222`
- Parse Pi command: `process_gatt_command()` at `:563`
- Send mesh vendor msg: `send_vendor_command()` at `:353`
- Receive mesh vendor response: `custom_model_cb()` at `:403`

## Group Addressing Hook

- Group constant `0xC000`: `:70`
- Group send path for ALL: `:663`

## Practical Debug Tip

When command path fails, check in this order:

1. Did `process_gatt_command()` parse correctly?
2. Did `send_vendor_command()` run?
3. Did `custom_model_cb()` receive `VND_OP_STATUS`?
4. Did `gatt_notify_sensor_data()` push back to Pi?
