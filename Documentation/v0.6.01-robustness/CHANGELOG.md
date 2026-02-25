# CHANGELOG — v0.6.01 Robustness & Safety Upgrade

**Date:** February 17, 2026
**Author:** Justin Kwarteng
**Status:** ✅ Hardware mod complete — circuit updated and verified

---

## Summary

Implemented the hardware failsafe modification to the inverting low-side driver circuit. The key change moves the Q1 base pull-up from the ESP 3.3V rail to the 12V supply rail, ensuring the load stays **OFF** if the ESP32 loses power or crashes.

---

## Circuit Changes (Old → New)

### Component Changes

| Component | Old Value | New Value | Reason |
|---|---|---|---|
| R1 (Q1 base pull-up) | 10kΩ to **3.3V** | 100kΩ to **12V** | Failsafe: keeps Q1 ON (load OFF) when 3.3V is lost |
| R2 (Q1 base series) | 1kΩ | 4.7kΩ | Reduces unnecessary GPIO/base current; Q1 only needs to sink ~0.24mA from 51k gate pull-up |
| R3 (MOSFET gate pull-down) | 51kΩ | 51kΩ (unchanged) | Already correct for gate discharge |
| Q1 (BJT inverter) | VN2222L | VN2222L (unchanged) | — |
| Q2 (MOSFET load switch) | MTA30N06E | MTA30N06E (unchanged) | — |

### Behavior Change

| Scenario | Old Circuit | New Circuit |
|---|---|---|
| ESP GPIO HIGH (duty=0%) | Q1 ON → MOSFET OFF → **Load OFF** ✅ | Same ✅ |
| ESP GPIO LOW (duty>0%) | Q1 OFF → MOSFET ON → **Load ON** ✅ | Same ✅ |
| ESP 3.3V lost / unpowered | Q1 OFF (no pull-up) → MOSFET ON → **Load ON** ⚠️ DANGEROUS | Q1 ON (12V pull-up still active) → MOSFET OFF → **Load OFF** ✅ SAFE |

### Safety Analysis (12V Pull-Up Path)

- Path: `12V → R1 (100kΩ) → Q1 Base → R2 (4.7kΩ) → ESP GPIO`
- Total resistance: ~104.7kΩ
- Max current into GPIO when ESP is unpowered: `12V / 104.7kΩ ≈ 0.11mA`
- ESP32 internal protection diodes safely handle <1mA steady current
- **No risk to ESP32 GPIO**

---

## Diagrams

| File | Description |
|---|---|
| `Old-circuit-diagram.jpeg` | Hand-drawn original circuit (Feb 15) — 3.3V pull-up, 1kΩ/10kΩ resistors |
| `Circuit-Diagram.drawio` | Updated draw.io schematic — 12V failsafe pull-up, 4.7kΩ/100kΩ resistors |
| `Circuit-Diagram.png` | PNG export of the updated schematic |

---

## Verification Performed

1. ✅ Disconnected ESP 3.3V — confirmed MOSFET gate stays near 0V (load OFF)
2. ✅ Normal operation — PWM duty control works identically to before
3. ✅ Confirmed no direct 3.3V pull-up remains at Q1 base (old 10kΩ path removed)
4. ✅ All grounds verified common (12V supply, ESP, INA260, load return)

---

## What Did NOT Change

- INA260 sensor wiring (I2C on GPIO6/GPIO7, powered from ESP 3.3V)
- MOSFET (Q2 MTA30N06E) and gate pull-down (R3 51kΩ)
- Load wiring (high-side through INA260 Vin+/Vin−)
- ESP32-C6 pin assignments (GPIO5=PWM, GPIO6=SDA, GPIO7=SCL)
- Any firmware or software — this was purely a hardware change
