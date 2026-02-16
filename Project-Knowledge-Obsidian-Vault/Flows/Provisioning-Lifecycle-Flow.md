# Provisioning Lifecycle Flow

This is the setup path before normal command traffic works.

## High-level steps

1. Provisioner detects unprovisioned device (UUID prefix match)
2. Provisioning completes (device gets unicast address)
3. Provisioner requests composition data
4. Provisioner adds AppKey
5. Provisioner binds models (OnOff, Vendor as applicable)
6. Provisioner subscribes Vendor Server to group `0xC000`
7. Node marked fully configured

## Key code anchors

- Provisioning callback: `ESP/ESP-Provisioner/main/main.c:524`
- Config client callback: `ESP/ESP-Provisioner/main/main.c:647`
- Bind chain: `ESP/ESP-Provisioner/main/main.c:337`
- Group subscription helper: `ESP/ESP-Provisioner/main/main.c:312`

## Why this flow matters

If provisioning/binding/subscription is incomplete, later command flows (`READ`, `ALL:*`) fail or behave inconsistently.

Diagram:

- `Drawio/07-Provisioning-Lifecycle.drawio`
