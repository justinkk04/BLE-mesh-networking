# Command Formats

## Pi -> GATT Gateway

Format:

`NODE_ID:COMMAND[:VALUE]`

Examples:

- `1:READ`
- `2:DUTY:50`
- `ALL:READ`

Parser:

- `ESP/ESP_GATT_BLE_Gateway/main/main.c:563`

## GATT Gateway -> Mesh Node (vendor payload)

Mapped text payloads:

- `READ` -> `read`
- `DUTY:50` -> `duty:50`
- `RAMP` -> `r`
- `STOP` -> `s`

Mapping lives in:

- `ESP/ESP_GATT_BLE_Gateway/main/main.c:624`

## Mesh Node -> Pi (via gateway notify)

Wrapped response format:

`NODE<id>:DATA:<payload>`

Payload example:

`D:50%,V:12.003V,I:250.00mA,P:3000.8mW`

Reformatted/forwarded by:

- `ESP/ESP_GATT_BLE_Gateway/main/main.c:403`

Parsed by:

- `gateway-pi5/gateway.py:760`
