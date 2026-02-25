# ESP32-C6 Mesh Provisioner

**Firmware:** ESP-IDF 5.x (C) · **Target:** ESP32-C6

Auto-discovers and provisions all BLE Mesh nodes with UUID prefix `0xdd`. Distributes NetKey/AppKey, binds models, and subscribes vendor servers to the group address (`0xC000`).

## Architecture

```
[THIS NODE] ──provisions──> Sensor Node(s)
                            GATT Gateway
                            Relay Node(s)
```

## Module Map

| File | Responsibility | Key Functions |
|---|---|---|
| `main.c` | **Thin orchestrator** — init calls only | `app_main()` |
| `provisioning.c/h` | Provisioning callbacks, config client, mesh init, unprovisioned advert handler | `ble_mesh_init()`, `provisioning_cb()`, `config_client_cb()`, `recv_unprov_adv_pkt()` |
| `mesh_config.c/h` | Mesh composition, model arrays, element definition, config constants | Model/element definitions, opcodes |
| `node_registry.c/h` | Track provisioned devices by UUID and unicast address | `node_registry_add()`, `node_registry_get()` |
| `composition.c/h` | Parse device composition data after provisioning | `store_comp_data()`, `get_comp_data()` |
| `model_binding.c/h` | AppKey add + model bind sequence + group subscription | `bind_app_key()`, `bind_model()` |

## Init Order (in `app_main`)

```
nvs_flash_init() → bluetooth_init() →
ble_mesh_get_dev_uuid() → ble_mesh_init()
```

## Provisioning Sequence

For each discovered node (UUID prefix `0xdd`):

```
1. Discover unprovisioned advertisement
2. Provision (assign unicast address)
3. Get composition data (discover models)
4. Add AppKey
5. Bind OnOff Server → AppKey
6. Bind OnOff Client → AppKey
7. Bind Vendor Server → AppKey
8. Bind Vendor Client → AppKey (if present)
9. Subscribe Vendor Server to group 0xC000
10. ========== NODE FULLY CONFIGURED ==========
```

## Detected Node Types

| Node Type | Has Vendor Server | Has Vendor Client | Has OnOff |
|---|---|---|---|
| Sensor Node | ✅ | ❌ | ✅ |
| GATT Gateway | ❌ | ✅ | ✅ |
| Relay Node | ❌ | ❌ | ❌ |

## Build

```bash
idf.py set-target esp32c6
idf.py build flash monitor
```

## Notes

- The provisioner must be powered on first when setting up a new network
- `idf.py erase-flash` required if reprovisioning from scratch
- Provisioned node data persists in NVS — survives power cycles
