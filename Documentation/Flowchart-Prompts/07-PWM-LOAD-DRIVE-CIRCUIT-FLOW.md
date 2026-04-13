# Prompt: PWM Load Drive Circuit Flowchart

> **Usage:** Copy everything below the `---` line and paste it as a prompt in a new conversation.

---

## Task

Create a `.drawio` XML file for the **PWM Load Drive Circuit Flow** — how a software duty cycle command travels from the ESP32 firmware through two MOSFET stages to switch a 12V/1A load. Save it to `Documentation/Flowcharts/07-pwm-load-drive-circuit-flow.drawio`.

## Purpose

This diagram is for a slideshow presentation. It should be clean, visually polished, and self-explanatory to someone unfamiliar with the codebase. It explains both the **hardware signal chain** and the **software inversion logic** in one unified flow.

## Circuit Components

- **ESP32-C6** — GPIO5 outputs PWM signal (LEDC, 1kHz, 13-bit resolution)
- **R2 (4.7kΩ)** — Gate protection resistor, limits current into Q1's gate
- **Q1 VN2222LL** — Small-signal N-channel MOSFET; acts as logic inverter + level shifter (common-source configuration)
- **R1 (100kΩ)** — Pull-up resistor from 12V to Q1's drain (switch node); also acts as gate bleed for Q2 (failsafe)
- **R3 (51kΩ)** — Gate series resistor for Q2; limits gate current, damps switching oscillation
- **Q2 MTA30N06E** — Power N-channel MOSFET; handles the full 12V @ 1A load
- **LOAD** — 12V, 1A device (e.g. LED bulb); connected between 12V rail and Q2's Drain

## Two Sections to Show

### Section 1: Hardware Signal Chain (left-to-right or top-down)

Show the physical signal path as a chain of labeled component boxes with arrows:

```
[ESP32 GPIO5] → [R2 4.7kΩ] → [Q1 VN2222LL Gate]
                                      ↓
                              Q1 DRAIN ←── [R1 100kΩ] ←── [12V Rail]
                                      ↓
                              [R3 51kΩ] → [Q2 MTA30N06E Gate]
                                                ↓
                              [12V Rail] → [LOAD] → [Q2 Drain] → [GND]
                                                        ↓
                                                   [Q2 Source] → [GND]
```

**Annotate each component with its role:**
- R2: "Gate protection / current limiter"
- Q1: "Signal inverter + 3.3V→12V level shifter"
- R1: "12V pull-up + failsafe gate bleed"
- R3: "Gate snubber / slew rate control"
- Q2: "Power switch — handles 12V @ 1A"

### Section 2: Logic Truth Table (side panel or below)

Show a small table or decision flow for the two key states:

| Software Command | LEDC Duty | GPIO5 | Q1 State | Q1 Drain | Q2 Gate | Q2 State | LOAD |
|---|---|---|---|---|---|---|---|
| `set_duty(100)` | 0 (min) | LOW | OFF | 12V via R1 | 12V | ON | 💡 ON |
| `set_duty(0)` | 8191 (max) | HIGH | ON | ~0V (pulled to GND) | ~0V | OFF | ⚫ OFF |

Add a callout box explaining the inversion:
> **Why invert?** Q1 in common-source configuration naturally inverts: GPIO HIGH → Q1 ON → Q2 Gate LOW → Load OFF. The firmware inverts the duty calculation so that `set_duty(100)` = full brightness feels intuitive to the user.

### Section 3: Failsafe Flow (separate branch or bottom panel)

Show a small decision diamond sub-flow:

```
[ESP32 loses power / unplugged]
            ↓
[GPIO5 = no drive → ~0V]
            ↓
[Q1 Gate = 0V → Q1 turns OFF]
            ↓
[R1 alone too weak to hold Q2 gate charged]
[R3 bleeds Q2 gate capacitance to GND]
            ↓
[Q2 Gate = 0V → Q2 turns OFF]
            ↓
💡 LOAD TURNS OFF (Failsafe ✅)
```

Add annotation: "R1 (100kΩ) is intentionally high-value so Q1 can override it when driven, but too weak to sustain Q2 gate charge when Q1 is off"

## Style

- **Dark background** (`#1e1e2e` or similar dark navy) to match slideshow aesthetic
- **Color coding:**
  - ESP32 / firmware elements: **Blue** (`#4f9ede`)
  - Small-signal stage (R2, Q1, R1): **Orange** (`#f0a500`)
  - Power stage (R3, Q2, LOAD): **Red/coral** (`#e05c5c`)
  - Failsafe path: **Green** (`#4caf50`)
  - Annotations / callouts: **Purple** (`#9c6fde`)
- **Font:** Clean sans-serif (e.g. Helvetica or similar), white text on dark boxes
- **Arrows:** Rounded, with labels on key transitions (e.g. "inverted logic", "level shifted to 12V", "gate bleeds to 0V")
- Component boxes should be **rounded rectangles**
- Decision diamonds for the failsafe section
- Add a **title box** at the top: "PWM Load Drive Circuit — Signal Chain & Inversion Logic"
- Add a small **subtitle**: "ESP32 GPIO5 → Q1 (inverter) → Q2 (power switch) → 12V Load"

## Output

Save the completed `.drawio` XML to: `Documentation/Flowcharts/07-pwm-load-drive-circuit-flow.drawio`
