# GATT Gateway & Provisioner Fixes Walkthrough

## Summary

This document summarizes the fixes made to enable the **ESP32-C6 GATT Gateway** to work correctly with **BLE Mesh** and be discoverable by the **Pi 5 Gateway**.

---

## Problem 1: Provisioner Bind Sequence Failed for Gateway

### Issue

The provisioner's `config_client_cb` returned early on any bind error. Since the GATT Gateway only has an **OnOff Client** (no Server), the provisioner would:

1. Try to bind OnOff Server → fail
2. Return early without binding OnOff Client
3. Leave the Gateway half-configured

### Fix: Composition-Data-Aware Binding

#### ESP-Provisioner/main/main.c

1. **Extended `mesh_node_info_t` struct** with model tracking fields:

```c
bool has_onoff_srv;
bool has_onoff_cli;
bool srv_bound;
bool cli_bound;
```

1. **Added `parse_composition_data()`** — parses BLE Mesh composition data to detect which models are present on a node.

2. **Added `bind_model()` helper** — encapsulates model binding logic to reduce duplication.

3. **Rewrote bind logic in `config_client_cb`**:
   - After `APP_KEY_ADD`: Binds Server if present, else binds Client
   - After `MODEL_APP_BIND`: Chains to next model if needed, marks node as fully configured when done

---

## Problem 2: GATT Init Failed on Gateway

### Issue

Multiple conflicts when initializing custom GATT alongside BLE Mesh:

- `nimble_port_init()` → failed (already called by mesh)
- `ble_svc_gap_init()` → crashed (already initialized)
- `ble_gatts_add_svcs()` → error 15 (GATT table locked after mesh init)

### Fix: Restructured Initialization Order

#### ESP_GATT_BLE_Gateway/main/main.c

1. **Removed duplicate init calls**:

```diff
-  nimble_port_init();        // Already done by bluetooth_init()
-  ble_svc_gap_init();        // Already done by mesh
-  ble_svc_gatt_init();       // Already done by mesh
```

1. **Split `gatt_init()` into two functions**:
   - `gatt_register_services()` — registers custom GATT services **before** mesh init
   - `gatt_start_advertising()` — starts advertising **after** mesh init

2. **Changed `app_main()` init order**:

```c
bluetooth_init();           // Init NimBLE
gatt_register_services();   // Register GATT services BEFORE mesh
mesh_init();                // Mesh locks GATT table
gatt_start_advertising();   // Start GATT advertising
```

1. **Fixed uninitialized variable warning**:

```diff
-uint16_t target_addr;
+uint16_t target_addr = 0;  // Initialize to avoid warning
```

---

## Problem 3: Gateway Not Discoverable by Pi 5

### Issue

After provisioning, the Gateway was not visible in BLE scans because mesh controlled all advertising and disabled proxy.

### Fix: Enabled Mesh Proxy

#### ESP_GATT_BLE_Gateway/main/main.c

Changed `cfg_srv` configuration:

```diff
-.gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
+.gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
```

This allows the Gateway to advertise as a **mesh proxy** after provisioning, making it discoverable.

---

## Problem 4: Pi Gateway Didn't Recognize Device Name

### Issue

`gateway.py` looked for `"Mesh-Gateway"` but the device advertises as `"ESP-BLE-MESH"`.

### Fix: Updated gateway.py

```python
# Old:
DEVICE_NAME_PREFIX = "Mesh-Gateway"

# New - match both names:
DEVICE_NAME_PATTERNS = ["Mesh-Gateway", "ESP-BLE-MESH"]

# In scan_for_nodes():
if device.name and any(p in device.name for p in DEVICE_NAME_PATTERNS):
```

**Or connect by MAC address:**

```bash
python gateway.py --address 98:A3:16:B1:C9:8A
```

---

## Verification

After applying all fixes:

1. **Provisioner** correctly identifies node models:

   ```
   Node 0x0006 models: srv=0, cli=1
   Binding OnOff Client to node 0x0006
   OnOff Client bound on 0x0006
   ========== NODE 0x0006 FULLY CONFIGURED ==========
   ```

2. **Gateway** initializes without errors:

   ```
   GATT services registered
   Mesh initialized, waiting for provisioner...
   GATT advertising started
   Gateway fully configured!
   ```

3. **Pi 5** connects successfully:

   ```
   ✓ Found: ESP-BLE-MESH [98:A3:16:B1:C9:8A]
   ✓ Connected!
   ✓ Subscribed to sensor notifications
   ```

---

## Files Modified

| File | Changes |
|------|---------|
| `ESP-Provisioner/main/main.c` | Composition-aware bind sequence |
| `ESP_GATT_BLE_Gateway/main/main.c` | GATT init order, proxy enabled |
| `gateway.py` (Pi 5) | Device name pattern matching |
