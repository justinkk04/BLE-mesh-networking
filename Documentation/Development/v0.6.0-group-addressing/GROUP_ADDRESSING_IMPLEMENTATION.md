# v0.6.0 — Group Addressing Implementation Guide

**Date:** February 14, 2026
**Author:** Justin Kwarteng
**Purpose:** Feed this document to an agent to implement BLE Mesh group addressing.

---

## 1. System Summary (What We Have Now — v0.5.0)

### Architecture

```
Pi 5 (gateway.py — Python TUI/CLI)
  |  BLE GATT (Service 0xDC01)
  v
ESP32-C6 GATT Gateway (ESP_GATT_BLE_Gateway)
  |  BLE Mesh (Vendor Model, CID 0x02E5)
  v
ESP32-C6 Mesh Node(s) (ESP-Mesh-Node-sensor-test)
  |  I2C (INA260) + LEDC PWM (load control)
  v
INA260 sensor + 2N2222/MOSFET load circuit
```

A separate **ESP32-C6 Provisioner** auto-discovers and configures all mesh devices.

### File Locations

| Component | Path | Language | Lines |
|---|---|---|---|
| Provisioner | `ESP/ESP-Provisioner/main/main.c` | C | ~829 |
| GATT Gateway | `ESP/ESP_GATT_BLE_Gateway/main/main.c` | C | ~995 |
| Mesh Node | `ESP/ESP-Mesh-Node-sensor-test/main/main.c` | C | ~775 |
| Pi 5 Gateway | `gateway-pi5/gateway.py` | Python | ~1700 |
| Master Docs | `MESH_IMPLEMENTATION.md` | Markdown | ~829 |

### How Commands Flow Today

1. Pi 5 writes to GATT characteristic `0xDC03` (e.g., `1:READ` or `ALL:DUTY:50`)
2. GATT Gateway's `process_gatt_command()` parses and maps to node-native format
3. For unicast: calls `send_vendor_command(target_addr, cmd)` with `VND_OP_SEND`
4. For `ALL`: **loops through `known_nodes[]` with 2500ms stagger delay between each**
5. Each mesh node's `custom_model_cb()` receives `VND_OP_SEND`, processes command synchronously
6. Node sends response via `VND_OP_STATUS` back to gateway
7. Gateway formats as `NODE<id>:DATA:<payload>` and notifies Pi 5 via GATT `0xDC02`

### How PowerManager Polls Today

In `PowerManager._poll_all_nodes()` (gateway.py):

```python
# Current: sequential unicast — O(N) time
for node_id in sorted(self.nodes.keys()):
    await self.gateway.send_to_node(node_id, "READ", _silent=True)
    await self.gateway._wait_node_response(node_id)
```

This sends `1:READ`, waits for response, `2:READ`, waits, etc. With N nodes and ~1s per round-trip, poll time ≈ N seconds.

### Vendor Model Definitions (shared across all ESP devices)

```c
#define CID_ESP              0x02E5
#define VND_MODEL_ID_CLIENT  0x0000   // On GATT Gateway
#define VND_MODEL_ID_SERVER  0x0001   // On Mesh Node(s)
#define VND_OP_SEND          ESP_BLE_MESH_MODEL_OP_3(0x00, CID_ESP)  // Gateway -> Node
#define VND_OP_STATUS        ESP_BLE_MESH_MODEL_OP_3(0x01, CID_ESP)  // Node -> Gateway
```

### Provisioner's Current Bind Chain

`bind_next_model()` in `ESP-Provisioner/main/main.c` (line 308-333):

```
1. OnOff Server   → bind_model(node, GEN_ONOFF_SRV)
2. OnOff Client   → bind_model(node, GEN_ONOFF_CLI)
3. Vendor Server  → bind_vendor_model(node, VND_MODEL_ID_SERVER)
4. Vendor Client  → bind_vendor_model(node, VND_MODEL_ID_CLIENT)
5. → "FULLY CONFIGURED" ← currently ends here
```

No group subscriptions are added. Nodes only receive unicast messages addressed to their specific unicast address (e.g., `0x0005`, `0x0006`).

### Node State Tracking

The `mesh_node_info_t` struct in the provisioner tracks per-node state:

```c
typedef struct {
  uint8_t  uuid[16];
  uint16_t unicast;
  uint8_t  elem_num;
  char     name[16];       // "NODE-0", "NODE-1", etc.
  bool     has_onoff_srv;
  bool     has_onoff_cli;
  bool     has_vnd_srv;    // Vendor Server detected
  bool     has_vnd_cli;    // Vendor Client detected
  bool     srv_bound;
  bool     cli_bound;
  bool     vnd_srv_bound;  // Vendor Server bound to AppKey
  bool     vnd_cli_bound;  // Vendor Client bound to AppKey
} mesh_node_info_t;
```

---

## 2. What Group Addressing Does

### Problem

`ALL:READ` with 2 nodes takes ~5s. With 5 nodes it takes ~15s. This is because the GATT Gateway loops through each node sequentially with a 2500ms stagger between sends.

### Solution

BLE Mesh supports **group addresses** (range `0xC000`-`0xFEFF`). A single message sent to a group address is delivered to ALL nodes subscribed to that group — simultaneously. Each node then responds individually with its own unicast `VND_OP_STATUS`.

**Result:** `ALL:READ` poll drops from O(N) to O(1). With 5 nodes: ~15s → ~3s.

### What Uses Group vs Unicast

| Command | Current | With Groups | Why |
|---|---|---|---|
| `ALL:READ` (PM poll) | N sequential unicast | **1 group send** | Same command to all nodes |
| `ALL:DUTY:100` | N sequential unicast | **1 group send** | Same value to all nodes |
| `ALL:RAMP` | N sequential unicast | **1 group send** | Same command to all nodes |
| `ALL:STOP` | N sequential unicast | **1 group send** | Same command to all nodes |
| PM nudge (per-node) | Unicast | **Stays unicast** | Different duty per node |
| `priority` rebalance | Unicast | **Stays unicast** | Different shares per node |
| Single-node commands | Unicast | **Stays unicast** | Targeted to one node |
| `MONITOR` | Unicast | **Stays unicast** | Polls one specific node |

---

## 3. Implementation Plan — Exact Changes Per File

### 3.1 Provisioner (`ESP/ESP-Provisioner/main/main.c`)

#### 3.1.1 Add Group Address Constant

Near the top of the file where `CID_ESP` and other constants are defined (around line 20-30):

```c
#define MESH_GROUP_ADDR  0xC000  // Group address for ALL commands
```

#### 3.1.2 Add `vnd_srv_subscribed` Flag to `mesh_node_info_t`

In the `mesh_node_info_t` struct (around line 55-66):

```c
typedef struct {
  // ... existing fields ...
  bool vnd_srv_bound;
  bool vnd_cli_bound;
  bool vnd_srv_subscribed;  // NEW: Vendor Server subscribed to group 0xC000
} mesh_node_info_t;
```

#### 3.1.3 Add `subscribe_vendor_model_to_group()` Helper Function

Add this new function near the existing `bind_vendor_model()` function (after line 306):

```c
// Subscribe a node's vendor server model to the group address
static esp_err_t subscribe_vendor_model_to_group(mesh_node_info_t *node) {
  esp_ble_mesh_client_common_param_t common = {0};
  esp_ble_mesh_cfg_client_set_state_t set = {0};

  ESP_LOGI(TAG, "Subscribing Vnd Server on 0x%04x to group 0x%04x",
           node->unicast, MESH_GROUP_ADDR);

  common.opcode = ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD;
  common.model = &root_models[1];  // CFG_CLI
  common.ctx.net_idx = prov_key.net_idx;
  common.ctx.app_idx = prov_key.app_idx;
  common.ctx.addr = node->unicast;
  common.ctx.send_ttl = MSG_SEND_TTL;
  common.msg_timeout = MSG_TIMEOUT;
  common.msg_role = ROLE_PROVISIONER;

  set.model_sub_add.element_addr = node->unicast;
  set.model_sub_add.sub_addr = MESH_GROUP_ADDR;
  set.model_sub_add.model_id = VND_MODEL_ID_SERVER;
  set.model_sub_add.company_id = CID_ESP;

  return esp_ble_mesh_config_client_set_state(&common, &set);
}
```

#### 3.1.4 Extend `bind_next_model()` Chain

Modify `bind_next_model()` (lines 308-333) to add group subscription as step 5:

```c
static void bind_next_model(mesh_node_info_t *node) {
  esp_err_t err;

  if (node->has_onoff_srv && !node->srv_bound) {
    err = bind_model(node, ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV);
    if (err) ESP_LOGE(TAG, "Bind OnOff Server failed: %d", err);
  } else if (node->has_onoff_cli && !node->cli_bound) {
    err = bind_model(node, ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI);
    if (err) ESP_LOGE(TAG, "Bind OnOff Client failed: %d", err);
  } else if (node->has_vnd_srv && !node->vnd_srv_bound) {
    err = bind_vendor_model(node, VND_MODEL_ID_SERVER);
    if (err) ESP_LOGE(TAG, "Bind Vendor Server failed: %d", err);
  } else if (node->has_vnd_cli && !node->vnd_cli_bound) {
    err = bind_vendor_model(node, VND_MODEL_ID_CLIENT);
    if (err) ESP_LOGE(TAG, "Bind Vendor Client failed: %d", err);
  // NEW: Step 5 — subscribe vendor server to group address
  } else if (node->has_vnd_srv && !node->vnd_srv_subscribed) {
    err = subscribe_vendor_model_to_group(node);
    if (err) ESP_LOGE(TAG, "Subscribe Vnd Server to group failed: %d", err);
  } else {
    ESP_LOGI(TAG, "========== NODE 0x%04x FULLY CONFIGURED ==========",
             node->unicast);
    ESP_LOGI(TAG, "Provisioned nodes: %d", node_count);
  }
}
```

#### 3.1.5 Handle Subscription Response in `config_client_cb()`

In `config_client_cb()` (lines 614-716), add handling for `ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD` in the `ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT` case:

```c
case ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT:
    if (opcode == ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD) {
      // ... existing code ...
    } else if (opcode == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND) {
      // ... existing code ...
    // NEW: Handle subscription response
    } else if (opcode == ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD) {
      ESP_LOGI(TAG, "Group subscription added on 0x%04x", addr);
      if (node) {
        node->vnd_srv_subscribed = true;
        bind_next_model(node);  // Chains to "FULLY CONFIGURED"
      }
    }
    break;
```

Also handle errors for `MODEL_SUB_ADD` in the error block at the top of `config_client_cb()`:

```c
if (param->error_code) {
    // ... existing code ...
    if (opcode == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND ||
        opcode == ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD) {
      node = get_node_info(addr);
      if (node) {
        ESP_LOGW(TAG, "Config op failed, trying next step...");
        bind_next_model(node);
      }
    }
    return;
}
```

---

### 3.2 GATT Gateway (`ESP/ESP_GATT_BLE_Gateway/main/main.c`)

#### 3.2.1 Add Group Address Constant

Near the top (around line 50):

```c
#define MESH_GROUP_ADDR  0xC000  // Group address for ALL commands
```

#### 3.2.2 Modify ALL Handling in `process_gatt_command()`

In `process_gatt_command()` (around lines 621-640), replace the sequential ALL loop with a single group send:

**CURRENT CODE (replace this whole if-block):**

```c
if (is_all) {
    // Send to all known (previously responded) nodes
    for (int i = 0; i < known_node_count; i++) {
        send_vendor_command(known_nodes[i], pico_cmd, strlen(pico_cmd));
        vTaskDelay(pdMS_TO_TICKS(2500));
    }
    // Auto-discover: probe the next address beyond known nodes...
    if (!discovery_complete) {
        uint16_t probe_addr = NODE_BASE_ADDR + known_node_count + 1;
        if (probe_addr <= NODE_BASE_ADDR + MAX_NODES) {
            ESP_LOGI(TAG, "Probing for new node at 0x%04x", probe_addr);
            send_vendor_command(probe_addr, pico_cmd, strlen(pico_cmd));
        }
    }
}
```

**NEW CODE:**

```c
if (is_all) {
    // Group send: one message reaches all subscribed nodes simultaneously
    ESP_LOGI(TAG, "Group send to 0x%04x: %s", MESH_GROUP_ADDR, pico_cmd);
    send_vendor_command(MESH_GROUP_ADDR, pico_cmd, strlen(pico_cmd));
    // Each node responds individually via VND_OP_STATUS — existing
    // response handling (register_known_node + gatt_notify) stays the same.
}
```

#### 3.2.3 Modify `send_vendor_command()` Response Tracking

In `send_vendor_command()` (lines 351-390), the function currently sets `vnd_send_target_addr` to track which unicast address to expect a response from. For group sends, we can't match on a single target address. Modify the response matching logic:

**CURRENT:**

```c
vnd_send_busy = true;
vnd_send_target_addr = target_addr;
```

**NEW:**

```c
vnd_send_busy = true;
vnd_send_target_addr = target_addr;

// For group sends, don't wait for a specific address — clear busy after send
// completion so responses can flow in without blocking
if (target_addr >= 0xC000 && target_addr <= 0xFEFF) {
    // Group address: responses come async from multiple nodes
    vnd_send_busy = false;
    vnd_send_target_addr = 0x0000;
}
```

#### 3.2.4 Response Handling — No Changes

The `custom_model_cb()` `VND_OP_STATUS` handler already:

- Extracts the source address from `ctx.addr`
- Calls `register_known_node(ctx.addr)`
- Formats as `NODE<id>:DATA:<payload>`
- Calls `gatt_notify_sensor_data()`

This all works identically for group-originated responses. Each node responds individually, and the gateway handles each response as it arrives. **No changes needed**.

---

### 3.3 Mesh Node (`ESP/ESP-Mesh-Node-sensor-test/main/main.c`)

#### No Code Changes Required

The mesh node's vendor server model is already registered with `ESP_BLE_MESH_VENDOR_MODEL()` and handles `VND_OP_SEND` in `custom_model_cb()`. When the provisioner subscribes the model to group `0xC000`, the ESP-IDF mesh stack automatically delivers group-addressed messages to the model's callback. The node's response path (`VND_OP_STATUS` back to the sender's unicast address) works identically.

**However**, you must erase flash and reprovision (see Section 4).

---

### 3.4 Pi 5 Gateway (`gateway-pi5/gateway.py`)

#### 3.4.1 Modify `_poll_all_nodes()` in PowerManager

**CURRENT** (lines 412-428):

```python
async def _poll_all_nodes(self):
    self._poll_generation += 1
    if not self.nodes:
        return
    node_ids = sorted(self.nodes.keys(), key=lambda x: int(x) if x.isdigit() else 999)
    for node_id in node_ids:
        if self.threshold_mw is None:
            return
        if not node_id.isdigit():
            continue
        await self.gateway.send_to_node(node_id, "READ", _silent=True)
        await self.gateway._wait_node_response(node_id)
```

**NEW:**

```python
async def _poll_all_nodes(self):
    self._poll_generation += 1
    if not self.nodes:
        return
    if self.threshold_mw is None:
        return
    # Group send: one ALL:READ → all nodes respond simultaneously
    await self.gateway.send_to_node("ALL", "READ", _silent=True)
    await self._wait_for_responses(timeout=4.0)
```

This changes from O(N) sequential sends + waits to O(1) single send + collect.

#### 3.4.2 Modify `send_to_node()` — ALL Path

In `DCMonitorGateway.send_to_node()`, the ALL path currently expands into a loop of individual sends. Change it to send a single `ALL:command` string that the GATT Gateway handles as a group broadcast:

**CURRENT:**

```python
if str(node).upper() == "ALL":
    pm = self._power_manager
    if pm and pm.nodes:
        node_ids = sorted(pm.nodes.keys(), ...)
    elif self.known_nodes:
        node_ids = sorted(self.known_nodes, ...)
    elif self.sensing_node_count > 0:
        node_ids = [str(i) for i in range(1, self.sensing_node_count + 1)]
    else:
        self.log("No nodes discovered ...")
        return False
    for nid in node_ids:
        if not nid.isdigit():
            continue
        if value is not None:
            cmd = f"{nid}:{command}:{value}"
        else:
            cmd = f"{nid}:{command}"
        await self.send_command(cmd, _silent=_silent)
    return True
```

**NEW:**

```python
if str(node).upper() == "ALL":
    # Group send: gateway handles 0xC000 broadcast
    if value is not None:
        cmd = f"ALL:{command}:{value}"
    else:
        cmd = f"ALL:{command}"
    await self.send_command(cmd, _silent=_silent)
    return True
```

#### 3.4.3 Add `_wait_all_responses()` Helper (Optional)

If you want to wait for all known nodes to respond after a group send, add to `DCMonitorGateway`:

```python
async def _wait_all_responses(self, timeout: float = 4.0):
    """Wait until all known_nodes have responded, or timeout."""
    if not self.known_nodes:
        await asyncio.sleep(timeout)
        return
    responded = set()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        # Check which nodes have fresh data (poll_gen == current gen)
        if self._power_manager:
            all_fresh = all(
                ns.poll_gen == self._power_manager._poll_generation
                for ns in self._power_manager.nodes.values()
                if ns.responsive
            )
            if all_fresh:
                return
        await asyncio.sleep(0.1)
```

Note: `_wait_for_responses()` already exists in PowerManager (lines 430-443) and does exactly this. The PM already calls it after `_poll_all_nodes()`. So this helper may not be needed — just ensure `_wait_for_responses()` is called after the group send.

#### 3.4.4 What Stays Exactly The Same

- `PowerManager._evaluate_and_adjust()` — all logic unchanged
- `PowerManager._nudge_node()` — still unicast, unchanged
- `PowerManager._balance_proportional()` — unchanged
- `PowerManager._balance_with_priority()` — unchanged
- `PowerManager.set_threshold()` — unchanged
- `PowerManager.set_priority()` / `clear_priority()` — unchanged
- `notification_handler()` — already handles individual responses, unchanged
- All TUI code — unchanged
- `set_duty()` for single-node PM nudge — unchanged

---

## 4. Deployment Steps

### 4.1 Flash Order — MUST Erase and Reprovision

Group subscriptions are stored in mesh NVS. Existing provisioned nodes won't have the group subscription. You MUST:

1. **Erase all ESP32-C6 devices:**

   ```bash
   # For each device:
   idf.py erase-flash
   ```

2. **Build and flash Provisioner FIRST** (it has the new subscription code):

   ```bash
   cd ESP/ESP-Provisioner
   rm sdkconfig sdkconfig.old
   rm -rf build
   idf.py set-target esp32c6
   idf.py build flash monitor
   ```

3. **Build and flash Mesh Node(s)** (no code changes, but need fresh provisioning):

   ```bash
   cd ESP/ESP-Mesh-Node-sensor-test
   idf.py erase-flash
   idf.py build flash monitor
   ```

4. **Build and flash GATT Gateway** (has the new group send code):

   ```bash
   cd ESP/ESP_GATT_BLE_Gateway
   rm sdkconfig sdkconfig.old
   rm -rf build
   idf.py set-target esp32c6
   idf.py build flash monitor
   ```

5. **Verify provisioning output** — should now show:

   ```
   Binding OnOff Server to node 0x0005
   OnOff Server bound on 0x0005
   Binding Vnd Server to node 0x0005
   Vendor Server bound on 0x0005
   Subscribing Vnd Server on 0x0005 to group 0xC000      ← NEW
   Group subscription added on 0x0005                     ← NEW
   ========== NODE 0x0005 FULLY CONFIGURED ==========
   ```

6. **Copy updated gateway.py to Pi 5:**

   ```bash
   scp gateway-pi5/gateway.py pi@<pi-ip>:~/gateway-pi5/
   ```

### 4.2 Why Erase is Required

- **Provisioner** sends `CONFIG_MODEL_SUB_ADD` during its provisioning chain. Already-provisioned nodes won't receive this.
- Mesh node NVS stores its mesh state. Without erase, it restores the old state (without group subscription) on boot.
- There is NO way to add group subscriptions to already-provisioned nodes without the provisioner re-provisioning them, which requires erasing their NVS first.

### 4.3 Fallback Plan

If group addressing fails for any reason, the old unicast approach still works. The Pi 5 gateway's `send_to_node("ALL", ...)` can fall back to the sequential loop:

```python
if str(node).upper() == "ALL":
    if self._use_group_addressing:
        cmd = f"ALL:{command}:{value}" if value else f"ALL:{command}"
        await self.send_command(cmd, _silent=_silent)
    else:
        # Fallback: sequential unicast
        for nid in self.known_nodes:
            await self.send_command(f"{nid}:{command}", _silent=_silent)
            await self._wait_node_response(nid)
```

---

## 5. Verification Plan

### 5.1 Provisioner Logs

Verify each node shows "Group subscription added" in the provisioner's serial monitor output.

### 5.2 Basic Group Send

```
# In Pi 5 gateway:
threshold 5000     # Start PM — should poll with single ALL:READ
```

Watch GATT Gateway serial monitor for:

```
Vendor SEND to 0xc000: read    ← GROUP send (not sequential unicast)
```

Watch for responses from ALL nodes arriving back:

```
NODE1:DATA:D:100%,V:11.95V,...
NODE2:DATA:D:100%,V:11.73V,...
```

### 5.3 Performance Test

Compare PM poll cycle time:

- **Before (v0.5.0):** `[POWER]` log entries ~4-5s apart for 2 nodes
- **After (v0.6.0):** `[POWER]` log entries ~2-3s apart for 2 nodes

### 5.4 PM Behavior

Verify these still work identically:

1. `threshold 5000` → nodes balance to ~2250mW each
2. `threshold 8000` → nodes ramp up immediately
3. `priority 1` → node 1 gets 2x share
4. `priority off` → nodes equalize
5. Individual PM nudges still use unicast (check gateway serial monitor shows unicast addresses like `0x0005`, not `0xC000`)

### 5.5 Edge Cases

- Single node: group send should work fine (one response)
- Node goes offline: PM marks it stale after STALE_TIMEOUT (unchanged)
- New node added after initial provisioning: provisioner handles it with the updated chain

---

## 6. Summary of Changes

| File | Lines Changed | What |
|---|---|---|
| `ESP-Provisioner/main/main.c` | ~30 new/modified | Group constant, `vnd_srv_subscribed` flag, `subscribe_vendor_model_to_group()`, extend `bind_next_model()`, handle `MODEL_SUB_ADD` in `config_client_cb()` |
| `ESP_GATT_BLE_Gateway/main/main.c` | ~15 modified | Group constant, replace ALL loop with single `send_vendor_command(0xC000, ...)`, skip busy tracking for group sends |
| `ESP-Mesh-Node-sensor-test/main/main.c` | **0 (no changes)** | ESP-IDF handles group delivery automatically |
| `gateway-pi5/gateway.py` | ~20 modified | Simplify `send_to_node()` ALL path, simplify `_poll_all_nodes()` to single group send |

**Total: ~65 lines of code changes across 3 files. No architecture rewrites.**
