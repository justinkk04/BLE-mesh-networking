# v1.0 Robustness & Safety Upgrade — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Harden the system for unattended operation ("set-and-forget"). Ensure the system fails safely if components disconnect or crash, and recovers automatically after power outages.

**Critical Safety Context:**
The hardware uses an **inverting low-side driver** (BJT inverter $\to$ MOSFET).

- **Logic High (3.3V) = Load OFF**
- **Logic Low (0V) / Floating = Load ON** (The Danger Zone)
- **Problem:** If the 3.3V rail fails (ESP removed/regulator dies), the BJT turns OFF, allowing the MOSFET to turn ON (Load ON).
- **Solution:** Hardware modification to bias Q1 ON by default (Load OFF).

**Branch name:** `feat/v1.0-robustness`

---

## 0. Hardware Safety Mod (The True Failsafe)

**Action:** Move the Q1 Base pull-up resistor from 3.3V to **12V**.
**Component:** Change R (10k) $\to$ **47kΩ - 100kΩ**.
**Critical Wiring Rule:** **Remove/cut the old 10k path to 3.3V entirely** (do not leave both pull-ups connected).
**Why:**

- If 3.3V is lost, the 12V rail still provides current to Q1 Base.
- Q1 turns ON (saturates).
- Q1 Collector goes LOW.
- MOSFET Gate goes LOW.
- **Load stays OFF.**

**Safety Note:** Using a high resistance (47k-100k) from 12V is safe for the ESP32 GPIO.

- The path is `12V -> 100k -> Base -> 4.7k -> GPIO`.
- Total resistance ~104.7kΩ.
- Max current into GPIO if ESP is dead/unpowered: `12V / 104.7kΩ ≈ 0.11mA`.
- ESP32 protection diodes can easily handle <1mA steady current.
- *Optional:* Using a dedicated 3.3V regulator from 12V is even safer but adds complexity/cost; the resistor method is standard practice.

**Detailed schematic (new):** `v1.0-robustness/01-Q1-Failsafe-12V-Pullup.drawio`
**SPICE test netlist:** `v1.0-robustness/failsafe-driver-ltspice.cir`
**CircuitikZ version:** `v1.0-robustness/failsafe-driver-circuitikz.tex`
**Tool guide:** `v1.0-robustness/CIRCUIT_TOOLS_AND_VALIDATION.md`

### 0.1 Base resistor update (1k vs 4.7k)

- `4.7k` for `Rbase_series` is a good choice and reduces unnecessary GPIO/base current.
- With gate pull-up `51k` to 12V, Q1 only needs to sink about `12/51k ~= 0.24 mA`.
- Even with `4.7k`, base drive is still comfortably above what Q1 needs.
- Recommendation: move `Rbase_series` from `1k` to `4.7k` (or `10k`) unless fast edge speed testing shows a need for lower value.

### 0.2 INA260 behavior when ESP 3.3V is lost

- If INA260 `VCC` is powered from ESP `3.3V`, then when ESP power is lost, INA260 will also power down.
- This is usually electrically safe for the monitor path, but telemetry is unavailable while ESP is off.
- Key protection rule: do not drive INA260 `SDA/SCL` above INA `VCC` when INA is unpowered.
- If you want sensor visibility independent of ESP state, power INA260 from an always-on 3.3V regulator derived from 12V, then connect I2C with proper pull-ups on that rail.

---

## 1. ESP32 Firmware Safety (The "Dead Man's Switch")

**Files:** `ESP/ESP-Mesh-Node/main/main.c` (and Relay node if applicable)
**Concept:** If the node hasn't received a valid mesh message (from Gateway/PowerManager) in `FAILSAFE_TIMEOUT` seconds, it assumes the brain is dead and must safe the system.

### Task 1.1: Implement Communications Watchdog

- Add `uint32_t last_valid_msg_time` timestamp.
- Update timestamp whenever a verified message (READ, DUTY, RAMP) is received.
- Add a periodic check (e.g., every 10s) in the main loop:

  ```c
  if (millis() - last_valid_msg_time > 300000) { // 5 minutes
      // Failsafe trigger
      pwm_set_duty(0); // Physically writes HIGH due to inverting logic
      blink_error_pattern();
  }
  ```

### Task 1.2: Enable Hardware Watchdogs (WDT)

- Ensure ESP-IDF Task Watchdog Timer (TWDT) is enabled.
- If the main loop freezes (crash/infinite loop), the WDT reboots the ESP.
- *(On reboot, GPIOs reset. We configure bootloader to pull this pin HIGH if possible, or initialize it ASAP).*

---

## 2. Gateway Persistence (Surviving Restarts)

**Files:** `gateway-pi5/gateway.py`
**Concept:** The Gateway is the system brain. If the Python script restarts (crash or service restart), it must remember its previous instructions (Threshold, Priority).

### Task 2.1: Save/Load State

- **Save:** When `set_threshold()`, `set_priority()`, or `disable()` is called, write the values to `gateway_config.json`.
- **Load:** On `__init__`, check for `gateway_config.json`.
- **Apply:** If config exists, automatically re-apply the threshold and priority after ensuring mesh connection.

---

## 3. Headless Operation (System Service)

**Files:** `gateway-pi5/ble-gateway.service`
**Concept:** The Gateway should run automatically when the Pi boots, without user login.

### Task 3.1: Create Systemd Service File

- Create `ble-gateway.service`:

  ```ini
  [Unit]
  Description=BLE Mesh Gateway Power Manager
  After=bluetooth.target network.target

  [Service]
  Type=simple
  User=justin
  WorkingDirectory=/home/justin/ble-gateway
  ExecStart=/home/justin/ble-gateway/.venv/bin/python gateway.py --no-tui
  Restart=always
  RestartSec=5

  [Install]
  WantedBy=multi-user.target
  ```

- Add a `--headless` or `--no-tui` mode to `gateway.py` (if not fully supported yet) to run cleanly without `textual` UI when run as a service.

---

## 4. Node Replacement Workflow ("The Alias")

**Files:** `gateway-pi5/gateway.py` (CLI commands)
**Concept:** When Node 1 dies, you replace it with a new node (Node 4). You want the system to treat "Node 4" as the new "Node 1".

### Task 4.1: Add "Alias" Command

- Usage: `python gateway.py --alias 4:1` ("Node 4 is now Node 1")
- Implementation:
  - Update `gateway_config.json` with an `aliases` map: `{"NODE4": "NODE1"}`.
  - In `notification_handler` and command sender, check this map.
  - If detailed reporting says "Node 4", Gateway logs/Json output says "Node 1".
  - Allows seamless swap without rewriting all your logs/dashboards.

---

## Execution Order

0. **Task 0 (Hardware Mod):** Critical physical verification.
1. **Task 1 (ESP Safety):** Critical firmware protection.
2. **Task 2 (Persistence):** Essential for reliable uptime.
3. **Task 3 (Service):** Deployment step.
4. **Task 4 (Alias):** Operational convenience.

## Verification

1. **Hardware Test:** Disconnect ESP 3.3V pin (or hold ESP in RESET). Verify Load remains OFF (0V at Gate).
2. **Failsafe Test:** Provision a node, turn on load. Unplug the Pi Gateway. Verify node turns off load after 5 mins.
3. **Reboot Test:** Set threshold 5000mW. Reboot Pi. Verify Gateway starts up and auto-enables threshold.
4. **Service Test:** `sudo reboot`. Check `systemctl status ble-gateway`. Verify Dashboard is accessible.
