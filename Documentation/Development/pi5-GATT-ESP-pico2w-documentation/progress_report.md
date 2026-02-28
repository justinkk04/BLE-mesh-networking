# Weekly Progress Report

| **Team Member** | **Progress** | **What's for tomorrow?** | **Hours worked since last Meeting** | **Hurdles** | **Notes** |
|-----------------|--------------|--------------------------|-------------------------------------|-------------|-----------|
| Justin Kwarteng | ✅ Completed bidirectional BLE mesh gateway communication between Pico 2W sensor node, ESP32-C6 bridge, and Pi 5 gateway | Expand to multi-node mesh topology; Add data logging/visualization on Pi 5 | ~8 hours | Minor: INA260 current polarity showing negative (fixed with `abs()`) | All three devices communicating wirelessly! |

---

## Summary of Work Completed

### BLE Mesh Network - DC Monitor System

Successfully implemented a **3-device IoT network** for remote DC power monitoring:

```
┌─────────────┐    UART     ┌─────────────┐    BLE     ┌─────────────┐
│  Pico 2W    │ ◄─────────► │  ESP32-C6   │ ◄────────► │   Pi 5      │
│  (Sensor)   │   115200    │  (Bridge)   │   NimBLE   │  (Gateway)  │
└─────────────┘             └─────────────┘            └─────────────┘
```

### Components Built

| Component | File | Description |
|-----------|------|-------------|
| **Pico 2W Sensor** | `DC-Monitoring-pico/main.py` | PWM load control + INA260 readings, accepts remote commands via UART |
| **ESP32-C6 Bridge** | `node-1-ble-mesh/main/main.c` | Bidirectional UART↔BLE bridge using NimBLE stack |
| **Pi 5 Gateway** | `gateway-pi5/gateway.py` | Python BLE central with interactive command mode |

### Features Implemented

- ✅ **Sensor Data Flow**: Pico → ESP32 → Pi 5 (voltage, current, power readings)
- ✅ **Remote Commands**: Pi 5 → ESP32 → Pico (duty cycle, ramp test, monitor mode)
- ✅ **BLE GATT Service**: Custom service (0xDC01) with sensor data + command characteristics
- ✅ **Interactive CLI**: Gateway supports `0-100`, `ramp`, `monitor`, `stop` commands

### Sample Output

```
[13:03:20] STATUS:RAMP_START
[13:03:21] D:0%,V:12.179V,I:5.00mA,P:60.9mW
[13:03:22] D:25%,V:11.955V,I:130.00mA,P:1554.2mW
[13:03:23] D:50%,V:11.833V,I:233.75mA,P:2765.8mW
[13:03:24] D:75%,V:11.715V,I:321.25mA,P:3763.4mW
[13:03:24] D:100%,V:11.626V,I:403.75mA,P:4694.1mW
```

---

## Next Steps

1. **Multi-Node Support**: Add more ESP32 nodes to create full mesh network
2. **Data Logging**: Store readings to file/database on Pi 5
3. **Web Dashboard**: Real-time visualization of sensor data
4. **ESP-BLE-MESH**: Migrate from point-to-point to full mesh topology
