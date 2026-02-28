# ESP Mesh Node Refactor: Direct I2C + PWM (Eliminate Pico)

## Problem

The BLE Mesh network had **unreliable command delivery to Node 2**. The root cause was the UART bridge architecture — each ESP32-C6 mesh node forwarded commands to a Pico 2W via UART, waited for an async response, then relayed it back through mesh. This introduced 500ms–2s of variable latency per command, making mesh send serialization unpredictable.

### Bugs Traced to UART Bridge

| Issue | Symptom |
|---|---|
| Async UART response queue | Wrong responses paired with wrong commands |
| UART polling latency (100ms+) | Slow round-trips cause mesh timeout |
| `vnd_send_in_progress` stuck flag | Responses silently dropped |
| 7-hop command path | Too many failure points |

## Solution

Eliminated the Pico 2W. The ESP32-C6 reads INA260 and controls PWM **directly**.

**Before** (7 hops, async):

```
Pi5 → GATT → Gateway → Mesh → Node → UART → Pico → sensor → UART → Node → Mesh → Gateway → Pi5
```

**After** (4 hops, synchronous):

```
Pi5 → GATT → Gateway → Mesh → Node → I2C read (~2ms) → Mesh → Gateway → Pi5
```

## What Changed in `main.c`

### Removed

- All UART code: `uart_init()`, `uart_send_to_pico()`, `uart_receive_task()`
- Async response queue: `vnd_ctx_queue[]` ring buffer
- `vnd_send_in_progress` flag and timeout guard
- `sensor_data[]` buffer
- Pico 2W dependency

### Added

- **I2C driver** for INA260 sensor (GPIO6 SDA, GPIO7 SCL, 400kHz)
- **I2C bus scan** at startup — prints all detected device addresses
- **INA260 reads**: voltage (reg 0x02), current (reg 0x01), 1024-sample averaging
- **LEDC PWM** at 1kHz on GPIO5 with inverted duty (2N2222→MOSFET circuit)
- **Synchronous vendor responses** — command processed and answered in same callback
- **Serial console task** — type commands into `idf.py monitor` for local testing

### Unchanged

- All mesh models, provisioning, NVS, relay
- Vendor opcodes (`VND_OP_SEND` / `VND_OP_STATUS`)
- GATT Gateway, Pi 5 gateway.py, Provisioner — no changes needed
- Response format: `D:%d%%,V:%.3fV,I:%.2fmA,P:%.1fmW`

## Console Commands

Type into `idf.py monitor`:

| Command | Action |
|---|---|
| `read` | Read INA260 voltage/current/power |
| `duty:50` | Set PWM to 50% |
| `50` | Same as duty:50 |
| `r` | Ramp test (0→25→50→75→100%) |
| `s` | Stop (duty 0%) |
| `scan` | Full I2C bus scan |

## Wiring

| Signal | Before (Pico) | After (ESP32-C6) |
|---|---|---|
| INA260 SDA | Pico GP4 | ESP GPIO6 |
| INA260 SCL | Pico GP5 | ESP GPIO7 |
| PWM out | Pico GP16 | ESP GPIO5 |
| INA260 Addr | 0x45 | 0x45 |
| UART TX/RX | ESP ↔ Pico | *(removed)* |
