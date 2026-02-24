# Provisioning Explained

Provisioning is how new mesh devices get admitted and configured.

## What the Provisioner Does

1. Finds unprovisioned devices with UUID prefix `0xdd`
2. Adds/uses keys
3. Gets composition data
4. Binds required models
5. Subscribes vendor server to group `0xC000`

Main callbacks and flow are in:

- `ESP/ESP-Provisioner/main/main.c:524`
- `ESP/ESP-Provisioner/main/main.c:647`

## Important Concepts

- NetKey: mesh network-level key
- AppKey: application-level key
- Model bind: connects a model to AppKey so messages can be processed
- Composition data: device-reported model list (what it supports)

## Why `ROLE_PROVISIONER` Matters

Provisioner messages set `msg_role = ROLE_PROVISIONER`:

- `ESP/ESP-Provisioner/main/main.c:275`
- `ESP/ESP-Provisioner/main/main.c:301`
- `ESP/ESP-Provisioner/main/main.c:326`

If role is wrong, config messages can fail even if keys look correct.

## Your Mental Model

Provisioner is "mesh setup automation."  
If nodes are not behaving, first verify they were provisioned/bound/subscribed correctly.
