# Why This Is Hybrid Star + Mesh (and Why BLE Mesh Here)

## Short answer

This system is "hybrid" because:

- Control pattern is mostly **star-like** (one Pi/gateway sends commands to many nodes).
- Transport is **BLE Mesh** (multi-hop relay-capable network between ESP nodes).

So logically it feels like a star, but physically/radio-wise it is mesh.

---

## Why it is hybrid in this repo

## Star behavior (control plane)

- Pi app is one central controller:
  - `gateway-pi5/gateway.py`
- Commands are initiated from one point (`gateway.py`) and dispatched to target node(s).
- PowerManager decisions happen centrally on Pi, not distributed on each node.

## Mesh behavior (network plane)

- ESP nodes are provisioned into BLE Mesh.
- Relay is enabled on node firmware, so packets can hop through other nodes.
- Group address `0xC000` allows one-to-many mesh delivery for `ALL:*`.

Refs:

- Group address in GATT gateway: `ESP/ESP_GATT_BLE_Gateway/main/main.c:70`
- Group send path: `ESP/ESP_GATT_BLE_Gateway/main/main.c:663`
- Relay enabled on sensing node: `ESP/ESP-Mesh-Node-sensor-test/main/main.c:103`
- Relay node role: `ESP/ESP-Mesh-Relay-Node/main/main.c`

---

## Why BLE Mesh instead of pure GATT-only design

## Pure GATT-only (point-to-point) limits

- Usually central-to-peripheral links with limited scalable multi-hop behavior.
- Harder to extend range across rooms/obstacles without custom forwarding.
- More manual handling for many nodes and resilience.

This repo already moved away from older point-to-point style for scalability and reliability.

## BLE Mesh benefits used here

1. **Range extension**
   - Relay nodes forward packets; gateway does not need direct RF reach to every sensing node.
2. **Path redundancy**
   - Multi-hop routes can survive weak direct links.
3. **One-to-many command efficiency**
   - Group address (`0xC000`) lets `ALL:READ` fan out in one send.
4. **Provisioning + config framework**
   - Provisioner automatically adds keys, binds models, and subscribes groups.

---

## "Why not just SIG models only?"

Important distinction:

- BLE Mesh itself is SIG standard.
- This project uses both:
  - SIG models (config/onoff infra)
  - Vendor model (custom payload for sensor and duty commands)

Why vendor model here:

- Needed rich text commands (`read`, `duty:50`, etc.) and rich sensor payloads.
- Generic OnOff alone is too limited for this app.

Refs:

- Vendor opcodes in gateway: `ESP/ESP_GATT_BLE_Gateway/main/main.c:50`
- Vendor server in sensing node: `ESP/ESP-Mesh-Node-sensor-test/main/main.c:143`

---

## Methodology used by this architecture

1. Use Pi as centralized control/optimization brain (easy to evolve logic).
2. Use BLE Mesh as resilient radio transport.
3. Use vendor model for app-specific command/data protocol.
4. Use group addressing for bulk operations, unicast for per-node tuning.
5. Keep relay nodes simple/silent to improve coverage without extra app logic.

This combination gives practical remote control + monitoring with better scalability than single-hop BLE.
