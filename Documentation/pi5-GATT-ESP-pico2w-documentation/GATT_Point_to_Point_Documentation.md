# BLE Mesh Network: DC Power Monitoring System

**Project**: Wireless DC Load Monitor  
**Author**: Justin Kwarteng  
**Date**: February 2, 2026  

---

## 📋 Table of Contents

1. [System Overview](#system-overview)
2. [Hardware Setup](#hardware-setup)
3. [Pico 2W - Sensor Node](#pico-2w---sensor-node)
4. [ESP32-C6 - BLE Bridge](#esp32-c6---ble-bridge)
5. [Raspberry Pi 5 - Gateway](#raspberry-pi-5---gateway)
6. [Testing & Results](#testing--results)
7. [Future Work](#future-work)

---

## System Overview

This project implements a **3-tier IoT network** for remote DC power monitoring using BLE (Bluetooth Low Energy):

```
┌─────────────────┐         ┌─────────────────┐         ┌─────────────────┐
│   PICO 2W       │  UART   │   ESP32-C6      │   BLE   │   Pi 5          │
│   Sensor Node   │◄───────►│   Bridge Node   │◄───────►│   Gateway       │
│                 │ 115200  │                 │  NimBLE │                 │
│ • INA260 sensor │  baud   │ • UART↔BLE      │         │ • Python/bleak  │
│ • PWM control   │         │ • GATT server   │         │ • CLI interface │
│ • MicroPython   │         │ • ESP-IDF       │         │ • Data display  │
└─────────────────┘         └─────────────────┘         └─────────────────┘
```

### Data Flow

| Direction | Path | Data |
|-----------|------|------|
| **Upstream** | Pico → ESP32 → Pi 5 | Voltage, Current, Power readings |
| **Downstream** | Pi 5 → ESP32 → Pico | Commands (duty, ramp, monitor, stop) |

---

## Hardware Setup

### Wiring Diagram

```
Pico 2W                    ESP32-C6 DevKitM-1
────────                   ──────────────────
GP12 (TX)  ─────────────►  GPIO4 (UART1 RX)
GP13 (RX)  ◄─────────────  GPIO5 (UART1 TX)
GND        ◄─────────────► GND

Pico 2W                    INA260
────────                   ──────
GP4 (SDA)  ─────────────►  SDA
GP5 (SCL)  ─────────────►  SCL
3V3        ─────────────►  VCC
GND        ─────────────►  GND
```

### Components

| Device | Role | Framework |
|--------|------|-----------|
| Raspberry Pi Pico 2W | Sensor + PWM control | MicroPython |
| ESP32-C6 DevKitM-1 | UART↔BLE bridge | ESP-IDF + NimBLE |
| Raspberry Pi 5 | Central gateway | Python + bleak |
| INA260 | Voltage/current sensor | I2C @ 0x45 |

---

## Pico 2W - Sensor Node

**File**: `DC-Monitoring-pico/main.py`

The Pico reads power data from the INA260 sensor and controls a PWM load. It accepts commands via UART from the ESP32.

### Configuration

```python
# Hardware Configuration
INA260_ADDR = 0x45          # I2C address
PWM_PIN = 16                # PWM output pin
PWM_FREQ = 1000             # 1kHz PWM frequency

# UART to ESP32-C6 (GP12=TX, GP13=RX)
uart = UART(0, baudrate=115200, tx=Pin(12), rx=Pin(13))

# I2C for INA260
i2c = I2C(0, sda=Pin(4), scl=Pin(5), freq=400000)
```

### Reading Sensor Data

```python
def read_ina260():
    """Read voltage and current from INA260"""
    time.sleep(0.3)  # Wait for averaging
    
    # Read voltage (register 0x02)
    data = i2c.readfrom_mem(INA260_ADDR, 0x02, 2)
    voltage = int.from_bytes(data, 'big') * 1.25 / 1000  # Convert to volts
    
    # Read current (register 0x01)
    data = i2c.readfrom_mem(INA260_ADDR, 0x01, 2)
    raw = int.from_bytes(data, 'big')
    if raw > 32767:
        raw -= 65536  # Handle signed value
    current = abs(raw * 1.25)  # Convert to mA
    
    return voltage, current
```

### PWM Duty Control

```python
def set_duty(percent):
    """Set load power (0-100%), inverted for 2N2222->MOSFET"""
    global current_duty
    percent = max(0, min(100, percent))
    inverted = 100 - percent  # Invert for transistor logic
    pwm.duty_u16(int(inverted * 65535 / 100))
    current_duty = percent
```

### Sending Data to ESP32

```python
def uart_send(duty, voltage, current, power=None):
    """Send reading to ESP32-C6 via UART"""
    if power is None:
        power = abs(voltage * current)
    msg = f"D:{duty}%,V:{voltage:.3f}V,I:{current:.2f}mA,P:{power:.1f}mW\n"
    uart.write(msg)
    return msg
```

### Processing Remote Commands

```python
def process_command(cmd):
    """Process a command from gateway via ESP32"""
    global monitor_mode
    cmd = cmd.strip().lower()
    
    if cmd == 's' or cmd == 'stop':
        set_duty(0)
        v, i = read_ina260()
        uart_send(0, v, i)
        uart_send_status("STOPPED")
        monitor_mode = False
        
    elif cmd == 'r' or cmd == 'ramp':
        uart_send_status("RAMP_START")
        for d in [0, 25, 50, 75, 100]:
            set_duty(d)
            time.sleep(0.5)
            v, i = read_ina260()
            uart_send(d, v, i)
        set_duty(0)
        uart_send_status("RAMP_DONE")
        
    elif cmd == 'm' or cmd == 'monitor':
        monitor_mode = True
        uart_send_status("MONITOR_START")
        
    elif cmd.startswith('duty:') or cmd.isdigit():
        duty_val = int(cmd.split(':')[1]) if ':' in cmd else int(cmd)
        set_duty(duty_val)
        v, i = read_ina260()
        uart_send(current_duty, v, i)
```

---

## ESP32-C6 - BLE Bridge

**Files**: `node-1-ble-mesh/main/main.c`, `ble_service.c`, `ble_service.h`

The ESP32-C6 acts as a bridge between the Pico (UART) and Pi 5 (BLE).

### UART Configuration

```c
// UART1 Configuration (UART0 is USB console!)
#define UART_PORT_NUM      UART_NUM_1
#define UART_BAUD_RATE     115200
#define UART_RX_PIN        GPIO_NUM_4   // From Pico GP12 (TX)
#define UART_TX_PIN        GPIO_NUM_5   // To Pico GP13 (RX)
#define BUF_SIZE           256

void uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, 
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}
```

### BLE GATT Service Definition

```c
// Custom UUIDs
#define DC_MONITOR_SERVICE_UUID         0xDC01  // Primary service
#define SENSOR_DATA_CHAR_UUID           0xDC02  // Read/Notify
#define COMMAND_CHAR_UUID               0xDC03  // Write

// GATT service structure
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(DC_MONITOR_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                // Sensor Data - Read + Notify
                .uuid = BLE_UUID16_DECLARE(SENSOR_DATA_CHAR_UUID),
                .access_cb = sensor_data_access_cb,
                .val_handle = &sensor_char_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                // Command - Write
                .uuid = BLE_UUID16_DECLARE(COMMAND_CHAR_UUID),
                .access_cb = command_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            { 0 },
        },
    },
    { 0 },
};
```

### Forwarding Data & Commands

```c
// UART → BLE: Forward sensor data from Pico to gateway
void uart_receive_task(void *pvParameters)
{
    uint8_t data[BUF_SIZE];
    
    while (1) {
        int len = uart_read_bytes(UART_PORT_NUM, data, BUF_SIZE - 1, 100 / portTICK_PERIOD_MS);
        
        if (len > 0) {
            data[len] = '\0';
            printf("PICO > %s", (char *)data);
            
            // Forward to BLE (notify connected gateway)
            ble_update_sensor_data((const char *)data, len);
        }
    }
}

// BLE → UART: Forward commands from gateway to Pico
static void on_command_received(const char *cmd, uint16_t len)
{
    ESP_LOGI(TAG, "BLE command: %s", cmd);
    uart_send_to_pico(cmd, len);
}
```

### SDK Configuration

**File**: `sdkconfig.defaults`

```ini
# Enable Bluetooth and NimBLE stack
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BTDM_CTRL_MODE_BLE_ONLY=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y
CONFIG_BT_NIMBLE_ROLE_CENTRAL=n
CONFIG_BT_NIMBLE_ROLE_OBSERVER=n
CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_GAP_DEVICE_NAME_MAX_LEN=32
```

---

## Raspberry Pi 5 - Gateway

**File**: `gateway-pi5/gateway.py`

The Pi 5 runs a Python script using the `bleak` library to act as a BLE central.

### BLE UUIDs

```python
# Custom UUIDs matching ESP32-C6 ble_service.h
DC_MONITOR_SERVICE_UUID = "0000dc01-0000-1000-8000-00805f9b34fb"
SENSOR_DATA_CHAR_UUID = "0000dc02-0000-1000-8000-00805f9b34fb"
COMMAND_CHAR_UUID = "0000dc03-0000-1000-8000-00805f9b34fb"
```

### Scanning for Nodes

```python
async def scan_for_nodes(self, timeout=10.0):
    """Scan for DC Monitor nodes"""
    devices = await BleakScanner.discover(timeout=timeout, return_adv=True)
    
    nodes = []
    for address, (device, adv_data) in devices.items():
        # Match by name or service UUID
        if device.name and "DC-Monitor" in device.name:
            nodes.append(device)
        elif adv_data.service_uuids:
            if DC_MONITOR_SERVICE_UUID.lower() in [str(u).lower() for u in adv_data.service_uuids]:
                nodes.append(device)
    
    return nodes
```

### Receiving Sensor Data

```python
def notification_handler(self, characteristic, data: bytearray):
    """Handle incoming sensor data notifications"""
    timestamp = datetime.now().strftime("%H:%M:%S")
    decoded = data.decode('utf-8', errors='replace').strip()
    
    if decoded.startswith("STATUS:"):
        print(f"[{timestamp}] 📢 {decoded}")
    else:
        print(f"[{timestamp}] 📊 {decoded}")
```

### Sending Commands

```python
async def send_command(self, cmd: str):
    """Send command to node (forwards to Pico via UART)"""
    await self.client.write_gatt_char(COMMAND_CHAR_UUID, cmd.encode('utf-8'))
    print(f"✓ Sent: {cmd}")

async def set_duty(self, percent: int):
    """Set duty cycle (0-100%)"""
    return await self.send_command(f"DUTY:{percent}")

async def start_ramp(self):
    return await self.send_command("RAMP")

async def start_monitor(self):
    return await self.send_command("MONITOR")

async def stop(self):
    return await self.send_command("STOP")
```

### Interactive Mode

```python
async def interactive_mode(self):
    """Interactive command mode"""
    print("Commands:")
    print("  0-100    Set duty cycle")
    print("  r/ramp   Run ramp test")
    print("  m/mon    Start monitoring")
    print("  s/stop   Stop load")
    print("  q/quit   Exit")
    
    while self.running and self.client.is_connected:
        cmd = input("> ").strip().lower()
        
        if cmd in ['q', 'quit']:
            await self.stop()
            break
        elif cmd in ['s', 'stop']:
            await self.stop()
        elif cmd in ['r', 'ramp']:
            await self.start_ramp()
        elif cmd.isdigit():
            await self.set_duty(int(cmd))
```

---

## Testing & Results

### Ramp Test Output

Running `python gateway.py --ramp` on the Pi 5:

```
==================================================
  DC Monitor BLE Gateway (Pi 5)
==================================================

🔍 Scanning for BLE devices (10.0s)...
  ✓ Found: nimble [FC:01:2C:F9:0E:B6] (by service UUID)

🔗 Connecting to nimble...
  ✓ Connected!
  ✓ Subscribed to sensor notifications
✓ Sent: RAMP

[13:03:20] 📢 STATUS:RAMP_START
[13:03:21] 📊 D:0%,V:12.179V,I:5.00mA,P:60.9mW
[13:03:22] 📊 D:25%,V:11.955V,I:130.00mA,P:1554.2mW
[13:03:23] 📊 D:50%,V:11.833V,I:233.75mA,P:2765.8mW
[13:03:24] 📊 D:75%,V:11.715V,I:321.25mA,P:3763.4mW
[13:03:24] 📊 D:100%,V:11.626V,I:403.75mA,P:4694.1mW
[13:03:25] 📢 STATUS:RAMP_DONE
```

### Power Curve Analysis

| Duty % | Voltage (V) | Current (mA) | Power (mW) |
|--------|-------------|--------------|------------|
| 0% | 12.179 | 5.00 | 60.9 |
| 25% | 11.955 | 130.00 | 1,554.2 |
| 50% | 11.833 | 233.75 | 2,765.8 |
| 75% | 11.715 | 321.25 | 3,763.4 |
| 100% | 11.626 | 403.75 | 4,694.1 |

> **Observation**: Voltage drops slightly as current increases (expected due to internal resistance and wiring).

---

## Future Work

1. **Multi-Node Mesh**: Add additional ESP32-C6 nodes to create a true mesh network using ESP-BLE-MESH
2. **Data Logging**: Store readings to SQLite database on Pi 5
3. **Web Dashboard**: Real-time visualization using Flask + Chart.js
4. **Alert System**: Notify when power exceeds threshold
5. **OTA Updates**: Over-the-air firmware updates for ESP32 nodes

---

## Project Structure

```
BLE-mesh-networking/
├── DC-Monitoring-pico/
│   └── main.py              # Pico 2W sensor code
├── node-1-ble-mesh/
│   ├── main/
│   │   ├── main.c           # ESP32 main application
│   │   ├── ble_service.c    # BLE GATT server
│   │   ├── ble_service.h    # BLE header
│   │   └── CMakeLists.txt   # Component build file
│   ├── sdkconfig.defaults   # Bluetooth configuration
│   └── CMakeLists.txt       # Project build file
└── gateway-pi5/
    ├── gateway.py           # Python gateway script
    └── requirements.txt     # Dependencies (bleak)
```

---

*Generated on February 2, 2026*
