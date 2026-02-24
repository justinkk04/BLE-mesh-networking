# Pi5 gateway.py Map

File: `gateway-pi5/gateway.py`

## Main Classes

1. `PowerManager` at `:141`  
   Auto-balances total power against threshold
2. `DCMonitorGateway` at `:683`  
   BLE scan/connect/send/receive logic
3. `MeshGatewayApp` (Textual UI) at `:1134`

## Most Important Methods

- Scan BLE devices: `scan_for_nodes()` at `:726`
- Connect and start notify: `connect_to_node()` at `:855`
- Parse notifications: `notification_handler()` at `:760`
- Send to one/all nodes: `send_to_node()` at `:929`
- Power polling: `_poll_all_nodes()` at `:412`
- Power decision loop: `_evaluate_and_adjust()` at `:452`
- Priority balancing: `_balance_with_priority()` at `:639`

## Data Model You Need to Know

`NodeState` tracks:

- `duty`
- `target_duty`
- `commanded_duty`
- `voltage/current/power`
- responsive state

That is the heart of PowerManager behavior.

## High-Level Runtime

1. Scan and connect to GATT gateway
2. Send command string (`1:READ`, `ALL:READ`, etc.)
3. Receive `NODEX:DATA:...` notifications
4. Update node table + PM state
5. If threshold enabled, keep running poll-adjust loop
