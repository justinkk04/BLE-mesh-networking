# BLE Mesh Network: Comprehensive Progress Report

**Project**: Self-Healing BLE Mesh DC Power Monitoring System  
**Author**: Justin Kwarteng  
**Status Date**: February 6, 2026

---

## Executive Summary

This project implements a **self-healing BLE mesh network** for remote DC power monitoring. The system consists of:

- **ESP32-C6 Mesh Nodes** - Sensor bridges with UART to Pico 2W
- **ESP32-C6 Provisioner** - Auto-provisions and configures mesh network
- **ESP32-C6 GATT Gateway** - Bridges Raspberry Pi 5 to mesh network
- **Raspberry Pi 5 Gateway** - Python BLE central with command interface
- **Pico 2W Sensor Nodes** - INA260-based DC power monitoring

```
┌──────────────┐           ┌──────────────────┐           ┌─────────────┐
│   Pi 5       │◄──GATT───►│  ESP32-C6        │◄──Mesh───►│   ESP32-C6  │
│   Gateway    │           │  GATT Gateway    │           │   Nodes     │
│   (Python)   │           │  (0xDC01)        │           │   (UART)    │
└──────────────┘           └──────────────────┘           └──────┬──────┘
                                                                  │
                                                           UART (115200)
                                                                  │
                                                          ┌───────▼──────┐
                                                          │   Pico 2W    │
                                                          │   INA260     │
                                                          └──────────────┘
```

---

## ✅ What's Working

| Component | Status | Description |
|-----------|--------|-------------|
| ESP-Provisioner | ✅ Working | Auto-provisions nodes with UUID prefix `0xdd` |
| ESP-Mesh-Node | ✅ Working | Server/Client/Relay with UART bridge |
| ESP_GATT_BLE_Gateway | ✅ Working | GATT ↔ Mesh bridge, connects to Pi 5 |
| Pi 5 Gateway | ✅ Working | Interactive CLI with mesh node targeting |
| Pico Sensor | ✅ Working | DC monitoring with INA260 |

---

## 🔧 Issues Fixed

### 1. NetKey Invalid Index Error (Provisioner)

**Full documentation**: `ESP/ESP-Provisioner/documentation/NETKEY_FIX.md`

**Symptom**: All config client messages failed with `Invalid NetKeyIndex 0x0000`

**Root Causes (2 bugs)**:

#### Bug 1: Manual Primary NetKey Addition

```c
// ❌ WRONG: Returns ESP_ERR_INVALID_ARG (258)
esp_ble_mesh_provisioner_add_local_net_key(..., ESP_BLE_MESH_KEY_PRIMARY);
```

**Fix**: Primary NetKey is auto-created by `esp_ble_mesh_init()`. Only add AppKey:

```c
case ESP_BLE_MESH_PROVISIONER_PROV_ENABLE_COMP_EVT:
    netkey_ready = true;  // NetKey already exists
    esp_ble_mesh_provisioner_add_local_app_key(...);
    break;
```

#### Bug 2: Missing `msg_role = ROLE_PROVISIONER`

```c
// ❌ WRONG: msg_role defaults to ROLE_NODE (0), uses wrong subnet table
esp_ble_mesh_client_common_param_t common = {0};

// ✅ FIX: Explicitly set provisioner role
common.msg_role = ROLE_PROVISIONER;
```

---

### 2. GATT Gateway Initialization Failures

**Full documentation**: `ESP/ESP_GATT_BLE_Gateway/documentation/GATT_GATEWAY_FIXES.md`

**Problem**: Multiple conflicts when initializing custom GATT alongside BLE Mesh.

**Fix**: Restructured initialization order:

```c
void app_main(void) {
    bluetooth_init();           // 1. Init NimBLE
    gatt_register_services();   // 2. Register GATT BEFORE mesh
    mesh_init();                // 3. Mesh locks GATT table
    gatt_start_advertising();   // 4. Start GATT advertising
}
```

---

### 3. Provisioner Bind Sequence Failed for Gateway

**Problem**: GATT Gateway only has OnOff Client (no Server), provisioner returned early on Server bind failure.

**Fix**: Composition-data-aware binding:

```c
typedef struct {
    // ... other fields
    bool has_onoff_srv;
    bool has_onoff_cli;
    bool srv_bound;
    bool cli_bound;
} mesh_node_info_t;

void parse_composition_data(mesh_node_info_t *node, struct net_buf_simple *comp);
```

Now provisioner:

1. Parses composition data to detect available models
2. Only binds models that exist on the node
3. Chains bind operations correctly

---

### 4. Gateway Not Discoverable by Pi 5

**Problem**: After provisioning, Gateway wasn't visible in BLE scans.

**Fix**: Enabled GATT Proxy:

```diff
-.gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
+.gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
```

---

### 5. Pi Gateway Name Matching

**Problem**: `gateway.py` looked for `"Mesh-Gateway"` but device advertises as `"ESP-BLE-MESH"` after provisioning.

**Fix**: Updated `gateway.py` to match multiple patterns or connect by MAC.

---

## 📁 Project Structure

```
BLE-mesh-networking/
├── ESP/
│   ├── ESP-Provisioner/          # Auto-provisions mesh nodes
│   │   ├── main/main.c           # 777 lines - composition-aware binding
│   │   └── documentation/
│   │       └── NETKEY_FIX.md
│   │
│   ├── ESP-Mesh-Node/            # Server/Client/Relay + UART bridge
│   │   └── main/main.c           # 460 lines - NVS state storage
│   │
│   └── ESP_GATT_BLE_Gateway/     # Pi 5 ↔ Mesh bridge
│       ├── main/main.c           # 600 lines - GATT + Mesh
│       └── documentation/
│           └── GATT_GATEWAY_FIXES.md
│
├── gateway-pi5/
│   └── gateway.py                # 288 lines - Python BLE central
│
├── Pico2w/
│   ├── DC-Monitoring-pico2w-1/   # Sensor node 1
│   └── DC-Monitoring-pico2w-2/   # Sensor node 2
│
└── BLE_Mesh_Documentation.md     # Original 3-tier system docs
```

---

## 🔌 Hardware Connections

### ESP32-C6 ↔ Pico 2W (UART)

| ESP32-C6 | Pico 2W | Function |
|----------|---------|----------|
| GPIO4 (RX) | GP12 (TX) | Data from Pico |
| GPIO5 (TX) | GP13 (RX) | Data to Pico |
| GND | GND | Ground |

### Pico 2W ↔ INA260 (I2C)

| Pico 2W | INA260 | Function |
|---------|--------|----------|
| GP4 (SDA) | SDA | I2C Data |
| GP5 (SCL) | SCL | I2C Clock |
| 3V3 | VCC | Power |
| GND | GND | Ground |

---

## 📡 Mesh Network Details

### Addressing Scheme

| Device | Unicast Address | Model |
|--------|----------------|-------|
| Provisioner | 0x0001 | Config Client + OnOff Client |
| GATT Gateway | 0x0005+ | **OnOff Client only** |
| Mesh Node 0 | 0x0005+ | OnOff Server + Client |
| Mesh Node 1 | 0x0006+ | OnOff Server + Client |

### UUID Auto-Provisioning

All mesh devices use UUID prefix `0xdd 0xdd` for automatic provisioning:

```c
static uint8_t dev_uuid[16] = {0xdd, 0xdd};
```

### GATT Service (Pi 5 Interface)

| UUID | Name | Properties |
|------|------|------------|
| 0xDC01 | DC Monitor Service | Primary Service |
| 0xDC02 | Sensor Data | Read, Notify |
| 0xDC03 | Command | Write |

### Command Format (Pi 5 → Gateway → Mesh)

```
NODE_ID:COMMAND[:VALUE]

Examples:
  0:RAMP       → Send RAMP to node 0
  1:STOP       → Send STOP to node 1
  0:DUTY:50    → Set 50% duty on node 0
  ALL:STOP     → Stop all nodes
```

---

## ⚠️ Remaining Work

### 1. Mesh Persistence (Not Implemented)

**Requirement**: Once provisioned, nodes should auto-rejoin mesh after power cycle without needing provisioner.

**Solution Needed**:

- Store provisioning data (NetKey, AppKey, unicast addr) in NVS
- On boot, check if provisioned and restore state
- ESP-IDF provides `ble_mesh_nvs_store()` / `ble_mesh_nvs_restore()`

**Current State**: ESP-Mesh-Node has basic NVS code but needs full persistence.

### 2. Full Command Forwarding (Partial)

**Current**: OnOff SET → RAMP or STOP forwarded to Pico

**Needed**:

- Forward DUTY commands with specific values
- Forward MONITOR command
- Return sensor data through mesh to gateway

### 3. Self-Healing Gateway Failover (Planned)

**Goal**: If GATT Gateway goes down, another mesh node should activate GATT and become the new gateway.

**Not Yet Implemented**.

---

## 🧪 Verification Results

### Provisioner Output

```
Node 0x0006 models: srv=0, cli=1
Binding OnOff Client to node 0x0006
OnOff Client bound on 0x0006
========== NODE 0x0006 FULLY CONFIGURED ==========
```

### Gateway Output

```
GATT services registered
Mesh initialized, waiting for provisioner...
GATT advertising started
Gateway fully configured!
```

### Pi 5 Connection

```
✓ Found: ESP-BLE-MESH [98:A3:16:B1:C9:8A]
✓ Connected!
✓ Subscribed to sensor notifications
```

---

## 📚 Reference Files

| File | Purpose |
|------|---------|
| `ESP/ESP-Provisioner/documentation/NETKEY_FIX.md` | NetKey/msg_role bug fixes |
| `ESP/ESP_GATT_BLE_Gateway/documentation/GATT_GATEWAY_FIXES.md` | GATT init & provisioner bind fixes |
| `ESP/ESP-Provisioner/main/main.c` | Provisioner implementation |
| `ESP/ESP-Mesh-Node/main/main.c` | Mesh node implementation |
| `ESP/ESP_GATT_BLE_Gateway/main/main.c` | Gateway implementation |
| `gateway-pi5/gateway.py` | Pi 5 Python gateway |
| `BLE_Mesh_Documentation.md` | Original system documentation |

---

*Report generated February 6, 2026*
