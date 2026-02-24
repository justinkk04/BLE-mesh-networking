# Unicast vs Group Addressing

## Unicast (one node)

Unicast targets one specific node address.

Example:

- `1:READ` -> one node replies

Used when each node needs a different command/value.

## Group Addressing (many nodes at once)

Group addressing sends one message to a group address shared by multiple nodes.

In this repo:

- Group address: `0xC000`
- Defined in GATT gateway: `ESP/ESP_GATT_BLE_Gateway/main/main.c:70`

When Pi sends `ALL:READ`, gateway sends one group vendor command:

- `ESP/ESP_GATT_BLE_Gateway/main/main.c:663`

## Why Group Addressing Matters

Without group: send N commands for N nodes (slower).
With group: send 1 command, all subscribed nodes receive (faster).

This changes polling from roughly O(N) to O(1) behavior.

## Where Subscription Happens

Provisioner subscribes vendor server model to group:

- `ESP/ESP-Provisioner/main/main.c:312`
- called from bind chain: `ESP/ESP-Provisioner/main/main.c:337`

## Practical Rule in This Repo

- Use group for "same command to all" (`ALL:READ`, `ALL:DUTY:50`, etc.)
- Use unicast for per-node balancing (different duty per node)
