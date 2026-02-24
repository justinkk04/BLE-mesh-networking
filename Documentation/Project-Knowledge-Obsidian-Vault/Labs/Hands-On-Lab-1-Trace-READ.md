# Lab 1 - Trace One READ Command

Goal: Build confidence by tracing one command from Pi to node and back.

## Commands

From Pi:

1. Connect gateway app
2. Run: `node 1`
3. Run: `read`

## What to Observe

1. Pi side sends command (`gateway.py send_to_node`)
2. GATT gateway parses and mesh-sends (`process_gatt_command`, `send_vendor_command`)
3. Sensing node handles vendor op (`custom_model_cb`)
4. Sensing node creates response (`process_command`)
5. GATT gateway notifies Pi
6. Pi `notification_handler` parses `NODE1:DATA:...`

## Code Checkpoints

- Pi send: `gateway-pi5/gateway.py:929`
- Gateway parse: `ESP/ESP_GATT_BLE_Gateway/main/main.c:563`
- Gateway mesh send: `ESP/ESP_GATT_BLE_Gateway/main/main.c:353`
- Node command execute: `ESP/ESP-Mesh-Node-sensor-test/main/main.c:363`
- Node vendor callback: `ESP/ESP-Mesh-Node-sensor-test/main/main.c:597`
- Pi parse notify: `gateway-pi5/gateway.py:760`

## Completion Criteria

You can explain each of the 6 checkpoints above in one sentence.
