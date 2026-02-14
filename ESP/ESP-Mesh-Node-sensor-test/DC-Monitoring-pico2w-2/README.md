# PWM Current Control System - Documentation

## Overview

This project uses a **Raspberry Pi Pico 2 W** to control power to a load (motor/bulb) via PWM, while measuring voltage and current with an **INA260** sensor.

---


### Signal Flow (Inverted Logic)

The 2N2222 transistor **inverts** the Pico's signal:

| Pico GP16 | 2N2222 | MOSFET Gate | MOSFET | Load |
|-----------|--------|-------------|--------|------|
| **HIGH** (3.3V) | ON | Pulled LOW | OFF | **OFF** |
| **LOW** (0V) | OFF | Pulled HIGH (2kΩ) | ON | **ON** |

### Why Inversion?

1. At **boot**, Pico pins are undefined (floating)
2. A **pull-up resistor** (10kΩ to 3.3V) keeps GP16 HIGH
3. HIGH → 2N2222 ON → MOSFET OFF → **Load safely OFF at startup**

---

## Component Connections

### Pico 2 W Pinout

| Function | Pin | GPIO |
|----------|-----|------|
| INA260 SDA | Pin 6 | GP4 |
| INA260 SCL | Pin 7 | GP5 |
| PWM Output | Pin 21 | GP16 |
| 3.3V Power | Pin 36 | 3V3_OUT |
| Ground | Pin 38 | GND |

### INA260 Connections

| INA260 Pin | Connects To |
|------------|-------------|
| VCC | Pico Pin 36 (3.3V) |
| GND | Pico Pin 38 (GND) |
| SDA | Pico Pin 6 (GP4) |
| SCL | Pico Pin 7 (GP5) |
| VIN+ | Power Supply (+) |
| VIN- | Load (+) terminal |

> **Important:** Load current must flow through VIN+ → VIN- for current measurement!



## Code Logic

### Configuration

```python
INA260_ADDR = 0x45   # I2C address (depends on A0/A1 jumpers)
PWM_PIN = 16         # GP16 for PWM output
PWM_FREQ = 1000      # 1kHz PWM frequency
```

### PWM Duty Cycle (Inverted)

Because of the 2N2222 inversion, the code inverts the duty cycle:

```python
def set_duty(percent):
    inverted = 100 - percent      # Invert for transistor
    pwm.duty_u16(int(inverted * 65535 / 100))
```

| User Input | Actual PWM | Load Power |
|------------|------------|------------|
| 0% | 100% HIGH | OFF |
| 50% | 50% HIGH | 50% |
| 100% | 0% HIGH (=LOW) | FULL ON |

### INA260 Reading

```python
def read_ina260():
    # Voltage: Register 0x02, 1.25mV per bit
    data = i2c.readfrom_mem(INA260_ADDR, 0x02, 2)
    voltage = int.from_bytes(data, 'big') * 1.25 / 1000  # Convert to V
    
    # Current: Register 0x01, 1.25mA per bit (signed)
    data = i2c.readfrom_mem(INA260_ADDR, 0x01, 2)
    raw = int.from_bytes(data, 'big')
    if raw > 32767:
        raw -= 65536  # Convert to signed
    current = raw * 1.25  # Convert to mA
    
    return voltage, current
```

### INA260 Averaging (Noise Reduction)

PWM causes noisy readings. The INA260 is configured for hardware averaging:

```python
config = 0x6727  # 1024 samples, 8.244ms conversion
i2c.writeto_mem(INA260_ADDR, 0x00, config.to_bytes(2, 'big'))
```

| Averaging | Samples | Time per Reading | Noise Level |
|-----------|---------|------------------|-------------|
| None | 1 | 1.1ms | High (PWM spikes) |
| 128 | 128 | ~140ms | Medium |
| **1024** | 1024 | **~8.4s** | **Very Low** |

---

## User Commands

| Command | Action |
|---------|--------|
| `0-100` | Set duty cycle percentage |
| `r` | Ramp test (0% → 25% → 50% → 75% → 100%) |
| `m` | Monitor mode (continuous readings) |
| `s` | Stop (set to 0%) |
| `q` | Quit program |

---

## Troubleshooting

### Negative Current Readings

**Cause:** INA260 VIN+ and VIN- are swapped  
**Fix:** Swap the wires on VIN+ and VIN-

### Erratic Current During PWM

**Cause:** INA260 sampling catches PWM on/off transitions  
**Fix:** Increase averaging (already done: 1024 samples)

### Load Doesn't Respond to PWM

**Possible Causes:**

1. Grounds not connected (Pico GND, Power GND, MOSFET Source must all connect)
2. 2N2222 collector not connected to MOSFET gate
3. PWM pin damaged (try GP17 instead of GP16)

### Zero Current Even When Load is ON

**Cause:** Load current bypassing INA260  
**Fix:** Wire load in series: `Power+ → VIN+ → VIN- → Load+ → Load- → MOSFET → GND`
