# v0.6.0 Group Addressing Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace sequential unicast ALL commands with a single BLE Mesh group broadcast, reducing poll time from O(N) to O(1).

**Architecture:** Provisioner subscribes each mesh node's vendor server model to group address `0xC000`. GATT Gateway sends one message to `0xC000` instead of looping through `known_nodes[]`. Each node responds individually via `VND_OP_STATUS`. Pi 5 gateway collects all responses.

**Tech Stack:** ESP-IDF BLE Mesh (C), Python 3 + bleak + textual

---

## System Context

Read `MESH_IMPLEMENTATION.md` in the project root for full architecture. Key points:

### File Map

| Component | Path | Role |
|---|---|---|
| Provisioner | `ESP/ESP-Provisioner/main/main.c` (~829 lines) | Auto-provisions mesh nodes, distributes keys, binds models |
| GATT Gateway | `ESP/ESP_GATT_BLE_Gateway/main/main.c` (~995 lines) | Pi 5 ↔ Mesh bridge via GATT service 0xDC01 |
| Mesh Node | `ESP/ESP-Mesh-Node-sensor-test/main/main.c` (~775 lines) | Direct I2C sensor + PWM load control |
| Pi 5 Gateway | `gateway-pi5/gateway.py` (~1700 lines) | Python TUI/CLI with PowerManager |

### Shared Constants (all ESP devices)

```c
#define CID_ESP              0x02E5
#define VND_MODEL_ID_CLIENT  0x0000   // On GATT Gateway
#define VND_MODEL_ID_SERVER  0x0001   // On Mesh Node(s)
#define VND_OP_SEND          ESP_BLE_MESH_MODEL_OP_3(0x00, CID_ESP)
#define VND_OP_STATUS        ESP_BLE_MESH_MODEL_OP_3(0x01, CID_ESP)
```

### Current ALL Command Flow (what we're changing)

1. Pi 5 `send_to_node("ALL", "READ")` → expands into loop: `1:READ`, wait, `2:READ`, wait...
2. GATT Gateway `process_gatt_command()` → loops `known_nodes[]` with **2500ms stagger**
3. Each node responds individually

### What Does NOT Change

- `PowerManager._evaluate_and_adjust()`, `_nudge_node()`, `_balance_proportional()`, `_balance_with_priority()` — all unchanged
- PM nudge commands — still unicast (different duty per node)
- `notification_handler()` — already handles individual responses
- TUI code — unchanged
- Mesh Node firmware — **zero code changes** (ESP-IDF handles group delivery automatically)

---

## Batch 1: Provisioner Changes (Tasks 1-2)

### Task 1: Add Group Subscription Helper to Provisioner

**Files:**

- Modify: `ESP/ESP-Provisioner/main/main.c:55-66` (struct), `:282-306` (after `bind_vendor_model`), `:308-333` (`bind_next_model`)

**Step 1: Add group address constant and struct field**

Near the top of the file, after the existing `#define VND_MODEL_ID_SERVER 0x0001` (around line 25), add:

```c
#define MESH_GROUP_ADDR  0xC000  // Group address for ALL commands
```

In the `mesh_node_info_t` struct (around line 55-66), add one field after `vnd_cli_bound`:

```c
  bool vnd_cli_bound;
  bool vnd_srv_subscribed;  // NEW: Vendor Server subscribed to group 0xC000
```

**Step 2: Add `subscribe_vendor_model_to_group()` function**

Add this function immediately after `bind_vendor_model()` (after line 306). Use the same pattern as `bind_vendor_model()` but with `MODEL_SUB_ADD` opcode:

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

**Step 3: Extend `bind_next_model()` with step 5**

In `bind_next_model()` (lines 308-333), add one new `else if` before the final `else` ("FULLY CONFIGURED"):

```c
  } else if (node->has_vnd_cli && !node->vnd_cli_bound) {
    err = bind_vendor_model(node, VND_MODEL_ID_CLIENT);
    if (err) ESP_LOGE(TAG, "Bind Vendor Client failed: %d", err);
  // === NEW: Step 5 — subscribe vendor server to group address ===
  } else if (node->has_vnd_srv && !node->vnd_srv_subscribed) {
    err = subscribe_vendor_model_to_group(node);
    if (err) ESP_LOGE(TAG, "Subscribe Vnd Server to group failed: %d", err);
  } else {
    ESP_LOGI(TAG, "========== NODE 0x%04x FULLY CONFIGURED ==========",
```

**Step 4: Build to verify compilation**

```bash
cd ESP/ESP-Provisioner
idf.py build
```

Expected: `Project build complete. To flash, run: idf.py flash`

---

### Task 2: Handle Subscription Response in Provisioner

**Files:**

- Modify: `ESP/ESP-Provisioner/main/main.c:614-716` (`config_client_cb`)

**Step 1: Handle `MODEL_SUB_ADD` success in `config_client_cb()`**

In `config_client_cb()`, inside the `ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT` case, after the `MODEL_APP_BIND` handler (around line 695), add:

```c
    } else if (opcode == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND) {
      // ... existing bind handling code stays exactly as-is ...
      bind_next_model(node);

    // === NEW: Handle group subscription response ===
    } else if (opcode == ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD) {
      ESP_LOGI(TAG, "Group subscription added on 0x%04x", addr);
      if (node) {
        node->vnd_srv_subscribed = true;
        bind_next_model(node);  // Chains to "FULLY CONFIGURED"
      }
    }
    break;
```

**Step 2: Handle `MODEL_SUB_ADD` errors**

In the error handling block at the top of `config_client_cb()` (around line 628-640), extend the condition to also catch `MODEL_SUB_ADD` failures:

Find this line:

```c
    if (opcode == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND) {
```

Replace with:

```c
    if (opcode == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND ||
        opcode == ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD) {
```

The existing error recovery (`bind_next_model(node)`) will skip the failed step and continue the chain.

**Step 3: Build to verify compilation**

```bash
cd ESP/ESP-Provisioner
idf.py build
```

Expected: `Project build complete. To flash, run: idf.py flash`

**Step 4: Commit**

```bash
cd ESP/ESP-Provisioner
git add main/main.c
git commit -m "feat(provisioner): subscribe vendor server to group 0xC000 during provisioning"
```

---

## Batch 2: GATT Gateway Changes (Tasks 3-4)

### Task 3: Replace ALL Loop with Group Send in Gateway

**Files:**

- Modify: `ESP/ESP_GATT_BLE_Gateway/main/main.c:~50` (constant), `:506-652` (`process_gatt_command`)

**Step 1: Add group address constant**

Near the top, after the existing vendor model defines (around line 50), add:

```c
#define MESH_GROUP_ADDR  0xC000  // Group address for ALL commands
```

**Step 2: Replace the ALL send loop in `process_gatt_command()`**

Find the `is_all` block inside the `if (vnd_bound)` section (around lines 621-640). It currently looks like:

```c
    if (is_all) {
      // Send to all known (previously responded) nodes
      for (int i = 0; i < known_node_count; i++) {
        send_vendor_command(known_nodes[i], pico_cmd, strlen(pico_cmd));
        vTaskDelay(pdMS_TO_TICKS(2500)); // Must exceed relay round-trip time
      }
      // Auto-discover: probe the next address beyond known nodes, but
      // stop once a probe times out (no more nodes to find).
      // discovery_complete is reset if a new node is manually discovered.
      if (!discovery_complete) {
        uint16_t probe_addr = NODE_BASE_ADDR + known_node_count + 1;
        if (probe_addr <= NODE_BASE_ADDR + MAX_NODES) {
          ESP_LOGI(TAG, "Probing for new node at 0x%04x", probe_addr);
          send_vendor_command(probe_addr, pico_cmd, strlen(pico_cmd));
        }
      }
    }
```

Replace the entire `if (is_all) { ... }` block with:

```c
    if (is_all) {
      // Group send: one message reaches all subscribed nodes simultaneously
      ESP_LOGI(TAG, "Group send to 0x%04x: %s", MESH_GROUP_ADDR, pico_cmd);
      send_vendor_command(MESH_GROUP_ADDR, pico_cmd, strlen(pico_cmd));
      // Each node responds individually via VND_OP_STATUS — existing
      // response handling (register_known_node + gatt_notify) is unchanged.
    }
```

**Step 3: Build to verify compilation**

```bash
cd ESP/ESP_GATT_BLE_Gateway
idf.py build
```

Expected: `Project build complete. To flash, run: idf.py flash`

---

### Task 4: Fix Response Tracking for Group Sends

**Files:**

- Modify: `ESP/ESP_GATT_BLE_Gateway/main/main.c:351-390` (`send_vendor_command`)

**Step 1: Skip busy-wait for group addresses**

In `send_vendor_command()` (lines 351-390), the function sets `vnd_send_busy = true` and `vnd_send_target_addr = target_addr` to serialize sends and match responses. For group sends, we can't match on a single target. Add this block right after `vnd_send_start_tick = xTaskGetTickCount();` (around line 383):

```c
  vnd_send_busy = true;
  vnd_send_target_addr = target_addr;
  vnd_send_start_tick = xTaskGetTickCount();

  // For group sends: don't block waiting for one specific response.
  // Multiple nodes respond async — let them flow through without blocking.
  if (target_addr >= 0xC000 && target_addr <= 0xFEFF) {
    vnd_send_busy = false;
    vnd_send_target_addr = 0x0000;
  }
```

**Step 2: Build to verify compilation**

```bash
cd ESP/ESP_GATT_BLE_Gateway
idf.py build
```

Expected: `Project build complete. To flash, run: idf.py flash`

**Step 3: Commit**

```bash
cd ESP/ESP_GATT_BLE_Gateway
git add main/main.c
git commit -m "feat(gateway): replace ALL unicast loop with group send to 0xC000"
```

---

## Batch 3: Pi 5 Gateway Changes (Tasks 5-6)

### Task 5: Simplify `send_to_node()` ALL Path

**Files:**

- Modify: `gateway-pi5/gateway.py` — `DCMonitorGateway.send_to_node()` method

**Step 1: Replace ALL expansion loop with single group send**

Find the `send_to_node()` method. Locate the `if str(node).upper() == "ALL":` block. It currently expands ALL into a loop of individual sends. Replace the entire ALL block with:

```python
        if str(node).upper() == "ALL":
            # Group send: GATT Gateway handles 0xC000 broadcast
            if value is not None:
                cmd = f"ALL:{command}:{value}"
            else:
                cmd = f"ALL:{command}"
            await self.send_command(cmd, _silent=_silent)
            return True
```

This sends a single `ALL:READ` or `ALL:DUTY:50` string to the GATT Gateway, which broadcasts to group `0xC000`.

**Step 2: Verify syntax**

```bash
python -c "import py_compile; py_compile.compile('gateway-pi5/gateway.py', doraise=True); print('OK')"
```

Expected: `OK`

---

### Task 6: Simplify PM `_poll_all_nodes()` to Use Group Send

**Files:**

- Modify: `gateway-pi5/gateway.py` — `PowerManager._poll_all_nodes()` method

**Step 1: Replace sequential polling with single group send**

Find `_poll_all_nodes()` in the `PowerManager` class. It currently loops through each node sending individual READ commands. Replace the entire method body with:

```python
    async def _poll_all_nodes(self):
        """Poll all nodes with a single group READ command.

        Uses group addressing (0xC000) — one mesh broadcast reaches all nodes
        simultaneously. Each node responds individually via VND_OP_STATUS.
        """
        self._poll_generation += 1
        if not self.nodes:
            return
        if self.threshold_mw is None:
            return
        # Single group send replaces O(N) sequential unicast
        await self.gateway.send_to_node("ALL", "READ", _silent=True)
        # _wait_for_responses() is called by poll_loop() after this method
```

Note: `poll_loop()` already calls `_wait_for_responses(timeout=4.0)` after `_poll_all_nodes()`, so responses are collected there. No new helper needed.

**Step 2: Verify syntax**

```bash
python -c "import py_compile; py_compile.compile('gateway-pi5/gateway.py', doraise=True); print('OK')"
```

Expected: `OK`

**Step 3: Commit**

```bash
cd gateway-pi5
git add gateway.py
git commit -m "feat(gateway.py): use group addressing for ALL commands and PM polling"
```

---

## Batch 4: Deployment and Verification (Tasks 7-9)

### Task 7: Erase Flash and Reprovision All Devices

> [!CAUTION]
> Group subscriptions are added during provisioning. Existing provisioned nodes will NOT have the subscription. You MUST erase flash on ALL ESP32-C6 devices and reprovision from scratch.

**Step 1: Erase and flash Provisioner FIRST**

```bash
cd ESP/ESP-Provisioner
idf.py erase-flash
idf.py flash monitor
```

Expected output (monitor running, waiting for devices):

```
I MESH_PROV: BLE Mesh Provisioner initialized
I MESH_PROV: Provisioner enabled, scanning for unprovisioned devices...
```

Leave this running.

**Step 2: Erase and flash each Mesh Node**

```bash
cd ESP/ESP-Mesh-Node-sensor-test
idf.py erase-flash
idf.py flash
```

Expected in Provisioner monitor:

```
I MESH_PROV: Unprovisioned device: dd:dd:...
I MESH_PROV: Provisioning node...
I MESH_PROV: Got composition data from 0x0005
I MESH_PROV: AppKey added to node 0x0005
I MESH_PROV: OnOff Server bound on 0x0005
I MESH_PROV: OnOff Client bound on 0x0005
I MESH_PROV: Vendor Server bound on 0x0005
I MESH_PROV: Subscribing Vnd Server on 0x0005 to group 0xC000    ← NEW
I MESH_PROV: Group subscription added on 0x0005                   ← NEW
I MESH_PROV: ========== NODE 0x0005 FULLY CONFIGURED ==========
```

**Verify:** The two NEW lines must appear. If "Group subscription added" is missing, there is a bug in Task 2.

Repeat for each mesh node.

**Step 3: Erase and flash GATT Gateway**

```bash
cd ESP/ESP_GATT_BLE_Gateway
idf.py erase-flash
idf.py flash
```

Expected in Provisioner monitor:

```
I MESH_PROV: Vendor Client bound on 0x0006
I MESH_PROV: ========== NODE 0x0006 FULLY CONFIGURED ==========
```

Note: The GATT Gateway has vendor CLIENT (not server), so it does NOT get a group subscription. This is correct — only nodes with vendor SERVER need group subscriptions.

---

### Task 8: Verify Group Send Works

**Step 1: Start Pi 5 gateway and connect**

```bash
cd gateway-pi5
python gateway.py
```

Connect to the GATT Gateway. Verify connection message shows correct node count.

**Step 2: Test ALL:READ via group**

```
read
```

(With target set to ALL, or `node all` then `read`)

**Verify in GATT Gateway serial monitor:**

```
I MESH_GW: Pi5 command: ALL:READ
I MESH_GW: Group send to 0xc000: read      ← Must say "Group send to 0xc000"
```

**NOT this (old behavior):**

```
I MESH_GW: Vendor SEND to 0x0005: read
I MESH_GW: Vendor SEND to 0x0006: read
```

**Verify in Pi 5 TUI:** Both/all nodes show updated sensor data.

**Step 3: Test ALL:DUTY via group**

```
node all
50
```

**Verify in GATT Gateway serial monitor:**

```
I MESH_GW: Group send to 0xc000: duty:50
```

**Verify in Pi 5 TUI:** All nodes show duty=50%.

---

### Task 9: Verify PM Still Works Correctly

**Step 1: Test threshold balancing**

```
threshold 5000
```

**Verify:**

- PM poll messages appear — `[POWER]` logs show balancing
- GATT Gateway shows `Group send to 0xc000: read` (not sequential unicast)
- Nodes settle to ~2250mW each (budget = 5000 - 500 headroom = 4500, split by N)

**Step 2: Test threshold increase (ramp up)**

```
threshold 8000
```

**Verify:** Nodes ramp up **immediately**. `[POWER] ▲ UP` messages appear.

**Step 3: Test priority mode**

```
priority 1
```

**Verify:** Node 1 gets ~67% of budget, Node 2 gets ~33%.

```
priority off
```

**Verify:** Nodes equalize.

**Step 4: Verify PM nudge is still unicast**

Check GATT Gateway serial monitor during PM balancing. Nudge commands should show individual unicast addresses:

```
I MESH_GW: Vendor SEND to 0x0005: duty:87     ← unicast, NOT 0xC000
I MESH_GW: Vendor SEND to 0x0006: duty:92     ← unicast, NOT 0xC000
```

Group sends should ONLY appear for READ polling:

```
I MESH_GW: Group send to 0xc000: read         ← group, correct
```

**Step 5: Verify performance improvement**

Compare time between `[POWER]` log entries:

- **v0.5.0 (before):** ~4-5s between poll cycles for 2 nodes
- **v0.6.0 (after):** ~2-3s between poll cycles for 2 nodes

**Step 6: Commit and tag**

```bash
git add -A
git commit -m "v0.6.0: group addressing for ALL commands"
git tag v0.6.0
```

---

## Rollback Plan

If group addressing causes issues, revert the Pi 5 gateway's `send_to_node()` ALL path back to the sequential loop (see `MESH_IMPLEMENTATION.md` for the original code). The ESP firmware changes are backward-compatible — group subscriptions don't affect unicast behavior.

---

## Files Changed Summary

| File | Changes | Lines |
|---|---|---|
| `ESP/ESP-Provisioner/main/main.c` | Add `MESH_GROUP_ADDR`, `vnd_srv_subscribed`, `subscribe_vendor_model_to_group()`, extend `bind_next_model()`, handle `MODEL_SUB_ADD` in `config_client_cb()` | ~35 new |
| `ESP/ESP_GATT_BLE_Gateway/main/main.c` | Add `MESH_GROUP_ADDR`, replace ALL loop with group send, skip busy-wait for group addresses | ~15 modified |
| `ESP/ESP-Mesh-Node-sensor-test/main/main.c` | **No changes** | 0 |
| `gateway-pi5/gateway.py` | Simplify `send_to_node()` ALL path, simplify `_poll_all_nodes()` | ~20 modified |

**Total: ~70 lines across 3 files. No architecture rewrites. Mesh node unchanged.**
