# Fix: "Invalid NetKeyIndex 0x0000" in ESP-IDF BLE Mesh Provisioner

## Platform
- ESP-IDF v5.1.6
- ESP32-C6
- ESP BLE Mesh Provisioner role

## Symptom
After provisioning a node (node receives unicast address successfully), all subsequent config client messages fail:
```
E (99476) BLE_MESH: Invalid NetKeyIndex 0x0000
```
Affected opcodes: Composition Data Get, AppKey Add (0x8008), Model App Bind.

## Root Cause — Two Separate Bugs

Both bugs produce the same misleading error about NetKeyIndex 0x0000 but are completely different problems.

---

### Bug 1: Manually adding the Primary NetKey returns ESP_ERR_INVALID_ARG (258)

**What we did wrong:**
Called `esp_ble_mesh_provisioner_add_local_net_key()` with `net_idx = ESP_BLE_MESH_KEY_PRIMARY` (0x0000), thinking the NetKey needed to be explicitly created before the AppKey.

**What happened:**
The call returned error code 258 (`ESP_ERR_INVALID_ARG`) every time, regardless of timing.

**Why:**
Inside the ESP-IDF source (`esp_ble_mesh_main.c`), there is an explicit guard:
```c
if (net_idx == ESP_BLE_MESH_KEY_PRIMARY) {
    return ESP_ERR_INVALID_ARG;
}
```
The primary NetKey (index 0x0000) is **automatically created** during `esp_ble_mesh_init()` for provisioner roles. The API rejects any attempt to add it manually.

We first tried moving the call into the `PROV_ENABLE_COMP_EVT` callback thinking it was a timing issue. Same error — it's not timing, it's by design.

**Fix:**
Remove `esp_ble_mesh_provisioner_add_local_net_key()` entirely. Set `netkey_ready = true` directly, then only add the AppKey:

```c
case ESP_BLE_MESH_PROVISIONER_PROV_ENABLE_COMP_EVT:
    if (param->provisioner_prov_enable_comp.err_code == ESP_OK) {
        // Primary NetKey (0x0000) is auto-created by esp_ble_mesh_init(),
        // so we only need to add our AppKey to it.
        netkey_ready = true;
        esp_ble_mesh_provisioner_add_local_app_key(
            prov_key.app_key, prov_key.net_idx, prov_key.app_idx);
    }
    break;
```

---

### Bug 2: Missing `msg_role = ROLE_PROVISIONER` on config client messages

**What we did wrong:**
The `set_config_common()` and `set_msg_common()` helper functions did not set the `msg_role` field on `esp_ble_mesh_client_common_param_t`. The field defaulted to 0.

**What happened:**
Provisioning succeeded (node got address 0x0005), but every config message after that — Composition Data Get, AppKey Add, Model App Bind — failed with `"Invalid NetKeyIndex 0x0000"`.

**Why:**
The ESP-IDF BLE Mesh stack maintains **separate subnet tables** for the node role and the provisioner role:
- `msg_role = ROLE_NODE` (0, the default) → stack looks up NetKey in the **node** subnet table
- `msg_role = ROLE_PROVISIONER` (1) → stack looks up NetKey in the **provisioner** subnet table

The provisioner's NetKey lives in the provisioner subnet table. When `msg_role` defaults to `ROLE_NODE`, the stack searches the node subnet table, which is empty — hence "Invalid NetKeyIndex 0x0000".

This was confirmed by reading the official ESP-IDF provisioner example, which sets `MSG_ROLE` to `ROLE_PROVISIONER` on every config/generic client message.

**Fix:**
Add `common->msg_role = ROLE_PROVISIONER` to both helper functions:

```c
static esp_err_t set_config_common(esp_ble_mesh_client_common_param_t *common,
                                   uint16_t unicast_addr,
                                   esp_ble_mesh_model_t *model,
                                   uint32_t opcode) {
    common->opcode = opcode;
    common->model = model;
    common->ctx.net_idx = prov_key.net_idx;
    common->ctx.app_idx = prov_key.app_idx;
    common->ctx.addr = unicast_addr;
    common->ctx.send_ttl = MSG_SEND_TTL;
    common->msg_timeout = MSG_TIMEOUT;
    common->msg_role = ROLE_PROVISIONER;  // <-- THIS WAS MISSING
    return ESP_OK;
}

static esp_err_t set_msg_common(esp_ble_mesh_client_common_param_t *common,
                                mesh_node_info_t *node,
                                esp_ble_mesh_model_t *model, uint32_t opcode) {
    common->opcode = opcode;
    common->model = model;
    common->ctx.net_idx = prov_key.net_idx;
    common->ctx.app_idx = prov_key.app_idx;
    common->ctx.addr = node->unicast;
    common->ctx.send_ttl = MSG_SEND_TTL;
    common->msg_timeout = MSG_TIMEOUT;
    common->msg_role = ROLE_PROVISIONER;  // <-- THIS WAS MISSING
    return ESP_OK;
}
```

Additionally, `ctx.app_idx` was changed from `ESP_BLE_MESH_KEY_UNUSED` to `prov_key.app_idx` to match the official example.

---

## Summary

| Bug | Error Seen | Actual Error Code | Root Cause | Fix |
|-----|-----------|-------------------|------------|-----|
| 1 | `add_local_net_key` fails | 258 (`ESP_ERR_INVALID_ARG`) | Primary NetKey is auto-created by `esp_ble_mesh_init()` — cannot add manually | Don't call `add_local_net_key`. Set `netkey_ready = true` directly, only add AppKey. |
| 2 | Config messages fail with "Invalid NetKeyIndex 0x0000" | logged by mesh stack internally | `msg_role` defaults to `ROLE_NODE` (0), stack looks up NetKey in wrong subnet table | Set `common->msg_role = ROLE_PROVISIONER` on all provisioner client messages |

## Key Takeaways

1. **Do not call `esp_ble_mesh_provisioner_add_local_net_key()` with index 0x0000.** The primary NetKey exists automatically after `esp_ble_mesh_init()` for provisioners.
2. **Always set `msg_role = ROLE_PROVISIONER`** on `esp_ble_mesh_client_common_param_t` when sending messages from a provisioner. The default value of 0 means `ROLE_NODE`, which uses the wrong subnet table.
3. **The official ESP-IDF examples are the source of truth.** Both fixes were confirmed by reading the example code at `$IDF_PATH/examples/bluetooth/esp_ble_mesh/`.
