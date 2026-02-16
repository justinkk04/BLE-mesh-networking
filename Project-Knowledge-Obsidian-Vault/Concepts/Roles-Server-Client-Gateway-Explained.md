# Roles: Server vs Client vs Gateway vs Node

This repo uses these words in specific ways.

## 1) "Gateway" (project role)

Gateway means bridge between two worlds:

- Pi BLE GATT world
- BLE Mesh world

In this project:

- ESP GATT Gateway firmware is the bridge device
- Pi `gateway.py` is the control app talking to that bridge

## 2) "Server" and "Client" (mesh model role)

These are model-level roles, not "whole device" roles.

- Server model: receives commands / serves state
- Client model: sends requests/commands

One physical node can contain multiple models.

## 3) "Vendor Server" (important)

Vendor Server means the custom model instance that handles your app commands.

In sensing node firmware:

- Vendor Server receives `VND_OP_SEND`
- executes command (`read`, `duty:50`, etc.)
- responds with `VND_OP_STATUS`

Refs:

- `ESP/ESP-Mesh-Node-sensor-test/main/main.c:143`
- `ESP/ESP-Mesh-Node-sensor-test/main/main.c:597`

## 4) "Vendor Client"

Vendor Client sends vendor commands into mesh.

In this project it lives on GATT Gateway firmware:

- `ESP/ESP_GATT_BLE_Gateway/main/main.c:160`
- send path: `ESP/ESP_GATT_BLE_Gateway/main/main.c:353`

## 5) Can there be multiple?

Yes by design:

- Multiple sensing nodes can all be vendor servers
- One gateway vendor client usually controls them
- You can have relay-only nodes with no vendor server

Current practical deployment is usually:

- 1 provisioner
- 1 GATT gateway
- N sensing nodes
- 0..N relay nodes

## 6) GATT Server vs Mesh Server

Do not mix these:

- GATT server/client is BLE attribute protocol layer
- Mesh server/client is model role inside mesh protocol

An ESP device can be GATT server and mesh client at the same time.

## 7) Why sensing nodes show an OnOff "client" in code

In this repo, sensing node composition includes both:

- Generic OnOff Server
- Generic OnOff Client

Refs:

- `ESP/ESP-Mesh-Node-sensor-test/main/main.c:132`
- `ESP/ESP-Mesh-Node-sensor-test/main/main.c:133`

But practically today, sensing nodes mainly act as servers for app behavior:

- Vendor Server handles command receive + response.
- OnOff Server handles fallback OnOff set/get.

The node does not currently send generic OnOff client set/get commands in normal flow.
Its generic client callback is present mostly for event handling/logging if ever used.

Also important:

- Sending sensor data back is still server behavior (`VND_OP_STATUS` response from server path),
  not "client mode".
