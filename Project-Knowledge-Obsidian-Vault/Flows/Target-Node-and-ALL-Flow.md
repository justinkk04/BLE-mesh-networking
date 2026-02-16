# Target Node and ALL Flow

This explains how node targeting works in `gateway.py`.

## Set target node in TUI/CLI

Command:

- `node 1`
- `node ALL`

Command parser paths:

- CLI interactive: `gateway-pi5/gateway.py:1038`
- TUI: `gateway-pi5/gateway.py:1296`

## How dispatch works

All feature commands eventually call `send_to_node(node, command, value)`:

- `gateway-pi5/gateway.py:929`

### If target is single node

Builds:

- `1:READ`
- `2:DUTY:50`

### If target is `ALL`

Builds:

- `ALL:READ`
- `ALL:DUTY:50`

Then GATT gateway translates `ALL:*` to mesh group address `0xC000`:

- `ESP/ESP_GATT_BLE_Gateway/main/main.c:663`

## Important distinction

- `ALL` is best for same command to every node
- PM priority/balancing still sends per-node duty updates when needed

## Validation checklist

1. Set `node ALL`
2. Run `read`
3. Confirm multiple `NODEX:DATA:` responses appear
