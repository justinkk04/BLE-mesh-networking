# v0.7.0 — Gateway Failover Implementation Guide

**Date:** February 25, 2026
**Author:** Justin Kwarteng
**Purpose:** Feed this document to an agent to implement BLE Mesh gateway failover.

> [!IMPORTANT]
> **v0.6.2 Modular Cleanup:** All codebases were split into single-responsibility modules.
> See `Documentation/v0.6.2-classes-cleanup/CHANGELOG.md` for full details.
> **DO NOT add code to `main.c` files** — they are thin orchestrators.
> **DO NOT add code to monolithic `gateway.py`** — it was split into 7 modules.
> Add to the correct module file instead.

---

## 1. System Summary (What We Have Now — v0.6.1)

### Architecture

```
Pi 5 (gateway.py — Python TUI/CLI)
  |  BLE GATT (Service 0xDC01)
  v
ESP32-C6 GATT Gateway (ESP_GATT_BLE_Gateway) — SINGLE POINT OF FAILURE
  |  BLE Mesh (Vendor Client, CID 0x02E5)
  v
ESP32-C6 Relay Node(s) ─── silent relay, TTL=7
  |
  v
ESP32-C6 Sensor Node(s) (ESP-Mesh-Node-sensor-test)
  |  I2C (INA260) + LEDC PWM
  v
INA260 sensor + Load Circuit
```

### File Locations

| Component | Base Path | Key Modules |
|---|---|---|
| GATT Gateway | `ESP/ESP_GATT_BLE_Gateway/main/` | `main.c` (orchestrator), `gatt_service.c/h`, `mesh_gateway.c/h`, `command_parser.c/h`, `monitor.c/h`, `node_tracker.c/h`, `nvs_store.c/h` |
| Sensor Node | `ESP/ESP-Mesh-Node-sensor-test/main/` | `main.c` (orchestrator), `sensor.c/h`, `load_control.c/h`, `command.c/h`, `mesh_node.c/h`, `nvs_store.c/h` |
| Relay Node | `ESP/ESP-Mesh-Relay-Node/main/` | `main.c` (orchestrator), `mesh_relay.c/h`, `led.c/h`, `nvs_store.c/h` |
| Provisioner | `ESP/ESP-Provisioner/main/` | `main.c` (orchestrator), `mesh_config.c/h`, `node_registry.c/h`, `composition.c/h`, `model_binding.c/h`, `provisioning.c/h` |
| Pi 5 Gateway | `gateway-pi5/gateway-code/` | `gateway.py` (entry point), `constants.py`, `ble_thread.py`, `node_state.py`, `power_manager.py`, `dc_gateway.py`, `tui_app.py` |

### Current GATT Gateway Capabilities (what sensor nodes DON'T have)

1. **GATT Service 0xDC01** — characteristics `0xDC02` (sensor data, read+notify) and `0xDC03` (command, write)
2. **Vendor CLIENT model** (`VND_MODEL_ID_CLIENT 0x0000`) — sends `VND_OP_SEND` to mesh nodes, receives `VND_OP_STATUS`
3. **Command parser** — `process_gatt_command()` maps Pi 5 commands to mesh node commands
4. **Chunked GATT notifications** — splits messages > 20 bytes into `+`-prefixed chunks
5. **Known node tracking** — `register_known_node()` builds a list of responding nodes
6. **Monitor mode** — FreeRTOS timer polls a node periodically with READ commands
7. **Send serialization** — `vnd_send_busy` / `vnd_send_target_addr` prevents overlapping mesh sends

### Current Sensor Node Capabilities (what GATT gateway DOESN'T have)

1. **I2C sensor** — reads INA260 voltage/current directly
2. **PWM load control** — LEDC inverted PWM for 2N2222/MOSFET
3. **Vendor SERVER model** (`VND_MODEL_ID_SERVER 0x0001`) — receives `VND_OP_SEND`, responds with `VND_OP_STATUS`
4. **Command processing** — `process_command()` handles `read`, `duty:N`, `r`, `s`
5. **OnOff Server/Client** — Generic OnOff models (backup command path)

### Current Python `gateway.py` Disconnect Behavior

- `connect_to_node()` in `dc_gateway.py` connects to one device, subscribes to notifications
- **No disconnect detection** — if GATT link drops, bleak eventually throws on the next `write_gatt_char()`
- **No auto-reconnect** — user must manually kill and restart `gateway.py`
- `BleThread` (in `ble_thread.py`) runs a dedicated event loop but has no reconnection logic

---

## 2. Phase 1: Pi 5 Auto-Reconnect (Python Only)

### 2.1 Disconnect Detection in `notify_handler`

The bleak library calls the notification handler callback with error information when a disconnect occurs on Linux/BlueZ. Additionally, `BleakClient.is_connected` returns `False` after disconnect.

### 2.2 Add `_auto_reconnect_loop()` to `DCMonitorGateway` in `dc_gateway.py`

Add this new method to the `DCMonitorGateway` class in `gateway-pi5/gateway-code/dc_gateway.py`:

```python
async def _auto_reconnect_loop(self):
    """Monitor BLE connection health and auto-reconnect on disconnect.
    
    Runs as a background task on the BLE thread's event loop.
    Checks connection every 2 seconds. On disconnect:
    1. Logs the event
    2. Pauses PM polling
    3. Rescans for the gateway
    4. Reconnects and resubscribes
    5. Resumes PM polling
    """
    while self.running:
        await asyncio.sleep(2.0)
        
        if self.client is None or not self.client.is_connected:
            if self._was_connected:
                self.log("[RECONNECT] Connection lost! Attempting reconnect...",
                         style="bold red", _from_thread=True)
                self._was_connected = False
                self._reconnecting = True
                
                # Pause PM if active
                pm = self._power_manager
                if pm and pm.threshold_mw is not None:
                    pm._paused = True
                    self.log("[RECONNECT] PowerManager paused", _from_thread=True)
                
                # Clean up old client
                self.client = None
                self.connected_device = None
            
            if not self._reconnecting:
                continue
                
            # Attempt reconnect
            try:
                devices = await self.scan_for_nodes(
                    timeout=5.0,
                    target_address=self._last_connected_address
                )
                if not devices:
                    # Try scanning without target address (any gateway)
                    devices = await self.scan_for_nodes(timeout=5.0)
                
                if devices:
                    success = await self.connect_to_node(devices[0])
                    if success:
                        self.log("[RECONNECT] Reconnected successfully!",
                                 style="bold green", _from_thread=True)
                        self._was_connected = True
                        self._reconnecting = False
                        self._last_connected_address = devices[0].address
                        
                        # Resume PM
                        if pm and pm._paused:
                            pm._paused = False
                            self.log("[RECONNECT] PowerManager resumed",
                                     _from_thread=True)
                    else:
                        self.log("[RECONNECT] Connect failed, retrying in 5s...",
                                 _from_thread=True)
                else:
                    self.log("[RECONNECT] No gateway found, retrying in 5s...",
                             _from_thread=True)
            except Exception as e:
                self.log(f"[RECONNECT] Error: {e}, retrying in 5s...",
                         _from_thread=True)
```

### 2.3 Add State Fields to `DCMonitorGateway.__init__()` in `dc_gateway.py`

In `gateway-pi5/gateway-code/dc_gateway.py`, add to `__init__()`:

```python
self._was_connected = False
self._reconnecting = False
self._last_connected_address = None
```

### 2.4 Set `_was_connected` on Successful Connection in `dc_gateway.py`

In `connect_to_node()` in `gateway-pi5/gateway-code/dc_gateway.py`, before `return True`, add:

```python
self._was_connected = True
self._last_connected_address = device.address
```

### 2.5 Add `_paused` Flag to `PowerManager` in `power_manager.py`

In `gateway-pi5/gateway-code/power_manager.py`, add to `PowerManager.__init__()`:

```python
self._paused = False
```

In `PowerManager.poll_loop()`, add at the top of the loop body:

```python
if self._paused:
    await asyncio.sleep(1.0)
    continue
```

### 2.6 Start the Reconnect Loop from `tui_app.py`

In `gateway-pi5/gateway-code/tui_app.py`, in `MeshGatewayApp.connect_ble()`, after successful connection, add:

```python
# Start auto-reconnect monitor
self._ble_thread.submit(self.gateway._auto_reconnect_loop())
```

### 2.7 Handle `send_command()` During Reconnect in `dc_gateway.py`

In `gateway-pi5/gateway-code/dc_gateway.py`, add a guard at the top of `DCMonitorGateway.send_command()`:

```python
if self._reconnecting:
    self.log("[WARN] Cannot send — reconnecting...", style="yellow")
    return
if self.client is None or not self.client.is_connected:
    self.log("[WARN] Not connected", style="yellow")
    return
```

---

## 3. Phase 2: Consolidated Universal Node Firmware

### 3.1 Overview of Changes to Sensor Node

The sensor node (`ESP-Mesh-Node-sensor-test/main/`) needs these additions from the GATT gateway.
**Create new module files** — do NOT add to existing `main.c` or `mesh_node.c`:

| Feature | Source in GATT Gateway | Target New Module |
|---------|----------------------|-------------------|
| GATT service definition | `gatt_service.c` lines ~1-22 | `gatt_service.c/h` (new) |
| GATT callbacks (read/write) | `gatt_service.c` lines ~23-66 | `gatt_service.c/h` (new) |
| GATT advertising | `gatt_service.c` lines ~67-103 | `gatt_service.c/h` (new) |
| GATT notify + chunking | `gatt_service.c` lines ~104-174 | `gatt_service.c/h` (new) |
| Vendor CLIENT model | `mesh_gateway.c` lines ~1-47 | Add to `mesh_node.c/h` |
| `send_vendor_command()` | `mesh_gateway.c` lines ~48-96 | Add to `mesh_node.c/h` |
| `process_gatt_command()` | `command_parser.c` | `command_parser.c/h` (new) |
| Known node tracking | `node_tracker.c` | `node_tracker.c/h` (new) |
| Monitor mode | `monitor.c` | `monitor.c/h` (new) |
| Send serialization vars | `mesh_gateway.c` | Add to `mesh_node.c/h` |

**Update `CMakeLists.txt`** to include all new source files.

### 3.2 New Mesh Composition in `mesh_node.c` (Both Models)

The current sensor node's `mesh_node.c` has:

```c
static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(&onoff_cli_pub, &onoff_client),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(&onoff_srv_pub, &onoff_server),
};

static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, VND_MODEL_ID_SERVER, vnd_op, NULL, NULL),
};
```

**Add** the vendor CLIENT model alongside the existing vendor SERVER:

```c
// Vendor client: sends commands TO other mesh nodes (like GATT gateway does)
static const esp_ble_mesh_client_op_pair_t vnd_op_pair[] = {
    {VND_OP_SEND, VND_OP_STATUS},
};

static esp_ble_mesh_client_t vendor_client = {
    .op_pair_size = ARRAY_SIZE(vnd_op_pair),
    .op_pair = vnd_op_pair,
};

static esp_ble_mesh_model_op_t vnd_cli_op[] = {
    ESP_BLE_MESH_MODEL_OP(VND_OP_STATUS, 1),
    ESP_BLE_MESH_MODEL_OP_END,
};

// Both vendor models on the same element
static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, VND_MODEL_ID_SERVER, vnd_op, NULL, NULL),
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, VND_MODEL_ID_CLIENT, vnd_cli_op, NULL, &vendor_client),
};
```

### 3.3 GATT Service Registration (Init Order)

The sensor node's `main.c` currently calls init functions in order.
Add GATT init calls while keeping `main.c` as a thin orchestrator:

```c
// In main.c app_main(), add these calls:
#include "gatt_service.h"  // NEW include

void app_main(void) {
    // ... existing init calls ...
    pwm_init();
    gatt_register_services();     // NEW (before mesh init!)
    ble_mesh_init();              // existing (also init vendor client model)
    gatt_start_advertising();     // NEW (after mesh init)
    xTaskCreate(console_task, ...); // existing
}
```

### 3.4 Self-Addressing for Local Commands

When the Pi 5 sends `0:READ` and this node IS node 0, bypass mesh and handle locally:

```c
// In process_gatt_command(), after parsing node_id and target_addr:
if (!is_all && target_addr == node_state.addr) {
    // This command is for US — process locally, no mesh needed
    char response[128];
    int resp_len = process_command(pico_cmd, response, sizeof(response));
    
    // Format as NODE<id>:DATA:<payload> and notify Pi 5
    char buf[SENSOR_DATA_MAX_LEN];
    int node_num = (node_state.addr >= NODE_BASE_ADDR) ? 
                   (node_state.addr - NODE_BASE_ADDR) : 0;
    int hdr_len = snprintf(buf, sizeof(buf), "NODE%d:DATA:", node_num);
    memcpy(buf + hdr_len, response, resp_len);
    buf[hdr_len + resp_len] = '\0';
    gatt_notify_sensor_data(buf, hdr_len + resp_len);
    return;
}
```

### 3.5 Advertising Name Change

Instead of `"Mesh-Gateway"`, universal nodes should advertise as e.g. `"Mesh-Node"` or `"DC-Monitor"`. The `gateway.py` scan already matches by service UUID or name prefix.

Update in `gatt_advertise()`:

```c
const char *name = "DC-Monitor";
```

And ensure `gateway-pi5/gateway-code/constants.py` has `"DC-Monitor"` in `DEVICE_NAME_PREFIXES`:

```python
DEVICE_NAME_PREFIXES = ["Mesh-Gateway", "DC-Monitor", "ESP-BLE-MESH"]
```

### 3.6 `custom_model_cb()` in `mesh_node.c` Now Handles Both Roles

The current sensor node's `mesh_node.c` `custom_model_cb()` only handles `VND_OP_SEND` (server role). The GATT gateway's `mesh_gateway.c` handles `VND_OP_STATUS` (client role, forwarding to Pi 5).

The consolidated node needs BOTH:

```c
static void custom_model_cb(esp_ble_mesh_model_cb_event_t event,
                             esp_ble_mesh_model_cb_param_t *param) {
  switch (event) {
  case ESP_BLE_MESH_MODEL_OPERATION_EVT:
    if (param->model_operation.opcode == VND_OP_SEND) {
        // ---- SERVER role: received a command, process locally ----
        // (existing sensor node code — unchanged)
        ...
    } else if (param->model_operation.opcode == VND_OP_STATUS) {
        // ---- CLIENT role: received response from another node ----
        // Forward to Pi 5 via GATT notify (copied from GATT gateway)
        if (gatt_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            // format as NODE<id>:DATA:<payload>, call gatt_notify_sensor_data()
            ...
        }
    }
    break;
    
  // ...rest of cases...
  }
}
```

---

## 4. Provisioner Changes (Phase 2)

### 4.1 Handle Dual Vendor Models

The provisioner's `composition.c` / `parse_composition_data()` already detects both `VND_MODEL_ID_SERVER` and `VND_MODEL_ID_CLIENT` by scanning vendor model IDs. A consolidated node will report both, so `has_vnd_srv` AND `has_vnd_cli` will both be true.

`model_binding.c` / `bind_next_model()` already handles this — it binds server first, then client. The group subscription step (v0.6.0) also works, since it only applies to `has_vnd_srv`.

### 4.2 No Provisioner Code Changes Expected

The current provisioner should handle universal nodes out of the box. The bind chain:

1. OnOff Server → bind
2. OnOff Client → bind
3. Vendor Server → bind
4. Vendor Client → bind  ← will now fire for sensor nodes too
5. Vendor Server → subscribe to group
6. → FULLY CONFIGURED

Just verify via serial monitor that all 6 steps complete.

---

## 5. Pi 5 `gateway.py` Changes (Phase 2)

### 5.1 Failover Logic in `_auto_reconnect_loop()` in `dc_gateway.py`

Extend the Phase 1 reconnect loop in `gateway-pi5/gateway-code/dc_gateway.py` to try ALL available nodes, not just the previously connected one:

```python
# In _auto_reconnect_loop(), on disconnect:
devices = await self.scan_for_nodes(timeout=5.0)
if devices:
    # Try each device until one connects
    for device in devices:
        success = await self.connect_to_node(device)
        if success:
            self.log(f"[FAILOVER] Connected to {device.name or device.address}")
            break
```

### 5.2 Update Scan Matching in `constants.py`

Ensure `gateway-pi5/gateway-code/constants.py` `DEVICE_NAME_PREFIXES` and service UUID matching work for universal nodes.

### 5.3 Sensing Node Count Adjustment

When the connected node is itself a sensor (universal node), the mesh scan will see N total nodes. But the connected node is one of them, so `sensing_node_count` might need adjustment since that node's data comes directly through GATT, not through mesh relay.

---

## 6. Deployment Steps (Phase 2)

> [!CAUTION]
> Phase 2 requires erasing flash and reprovisioning ALL ESP32-C6 devices.

### 6.1 Flash Order

1. **Build and flash Provisioner** (no code changes, but erase for clean state):

   ```bash
   cd ESP/ESP-Provisioner
   idf.py erase-flash
   idf.py build flash monitor
   ```

2. **Build and flash consolidated sensor nodes**:

   ```bash
   cd ESP/ESP-Mesh-Node-sensor-test
   idf.py erase-flash
   idf.py build flash monitor
   ```

3. **Verify provisioning** — should show BOTH vendor models bound:

   ```
   Vendor Server bound on 0x0005
   Vendor Client bound on 0x0005    ← NEW
   Subscribing Vnd Server on 0x0005 to group 0xC000
   Group subscription added on 0x0005
   ========== NODE 0x0005 FULLY CONFIGURED ==========
   ```

4. **Copy updated `gateway.py` to Pi 5**:

   ```bash
   scp gateway-pi5/gateway.py pi@<pi-ip>:~/gateway-pi5/
   ```

### 6.2 Verification

1. **Basic connection:** Pi 5 connects to any universal node
2. **Local commands:** `0:READ` works (self-addressed)
3. **Remote commands:** `1:READ`, `ALL:READ` work (forwarded via mesh)
4. **Failover test:** Power-cycle connected node → Pi 5 reconnects to another
5. **PM test:** `threshold 5000` → balancing works across nodes

---

## 7. File Structure After v0.7.0

```text
ESP/
  ESP-Mesh-Node-sensor-test/   ← Becomes "Universal Node"
    main/
      main.c                    ← Thin orchestrator (add 2 new init calls)
      mesh_node.c/h             ← Add vendor CLIENT model
      gatt_service.c/h          ← NEW (ported from GATT gateway)
      command_parser.c/h        ← NEW (ported from GATT gateway)
      node_tracker.c/h          ← NEW (ported from GATT gateway)
      monitor.c/h               ← NEW (ported from GATT gateway)
      sensor.c/h                ← Unchanged
      load_control.c/h          ← Unchanged
      command.c/h               ← Unchanged
      nvs_store.c/h             ← Unchanged
      CMakeLists.txt            ← Updated with new source files
  ESP-Mesh-Relay-Node/          ← Unchanged
  ESP-Provisioner/              ← Unchanged (or minimal)
  ESP_GATT_BLE_Gateway/         ← DEPRECATED (kept for rollback)
gateway-pi5/
  gateway-code/
    gateway.py                  ← Entry point (unchanged)
    constants.py                ← Add "DC-Monitor" prefix
    ble_thread.py               ← Unchanged
    node_state.py               ← Unchanged
    power_manager.py            ← Add _paused flag (~5 lines)
    dc_gateway.py               ← Add reconnect + failover (~100 lines)
    tui_app.py                  ← Start reconnect loop (~3 lines)
```
