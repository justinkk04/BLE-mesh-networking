# Vendor Model Explained

## What It Means

BLE Mesh has standard models (SIG models), but this project also uses a custom "vendor model" for text commands and sensor responses.

In this repo:

- `VND_OP_SEND` = gateway sends command to node
- `VND_OP_STATUS` = node sends response back

## Where It Is Defined

- GATT gateway: `ESP/ESP_GATT_BLE_Gateway/main/main.c:50`
- Sensing node: `ESP/ESP-Mesh-Node-sensor-test/main/main.c:48`

## Why Use It Here

Standard OnOff is too limited (1-bit style state).
This project needs rich command strings like:

- `read`
- `duty:50`
- `r`
- `s`

And rich responses like:

`D:50%,V:12.003V,I:250.00mA,P:3000.8mW`

## Flow Through Vendor Model

1. GATT gateway parses Pi command in `process_gatt_command()`  
   `ESP/ESP_GATT_BLE_Gateway/main/main.c:563`
2. Gateway sends vendor message with `send_vendor_command()`  
   `ESP/ESP_GATT_BLE_Gateway/main/main.c:353`
3. Sensing node receives in `custom_model_cb()`  
   `ESP/ESP-Mesh-Node-sensor-test/main/main.c:597`
4. Sensing node runs `process_command()` and sends `VND_OP_STATUS`  
   `ESP/ESP-Mesh-Node-sensor-test/main/main.c:363`
5. GATT gateway forwards back to Pi via GATT notify  
   `ESP/ESP_GATT_BLE_Gateway/main/main.c:222`

## Mental Model

Treat vendor model as your "application protocol tunnel" inside BLE Mesh.
