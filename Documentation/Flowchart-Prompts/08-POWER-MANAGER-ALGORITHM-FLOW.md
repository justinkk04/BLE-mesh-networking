# Flowchart Prompt 08 — Power Manager Equilibrium Algorithm

## Purpose

This diagram is for a slideshow presentation. It should be clean, visually
polished, and self-explanatory to someone unfamiliar with the codebase.
It explains the **full lifecycle of the Power Manager** — from threshold
activation, through the periodic poll cycle, the equilibrium decision logic,
priority-weighted balancing, and nudge commands — using the same dark-mode
colour style as the other project flowcharts.

## Source File

`gateway-pi5/gateway-code/power_manager.py`

## Key Constants (embed in diagram)

| Constant | Value | Meaning |
|---|---|---|
| `POLL_INTERVAL` | 3.0 s | Gap between poll cycles |
| `HEADROOM_MW` | 500 mW | Buffer subtracted from threshold to get budget |
| `STALE_TIMEOUT` | 45 s | Mark node unresponsive after this long with no reply |
| `COOLDOWN` | 5.0 s | Min time between adjustments (prevents thrashing) |
| `PRIORITY_WEIGHT` | 2.0× | Priority node gets this many shares vs 1× for others |
| Dead band | ±5 % of budget | Skip nudge if total power is within this range |

---

## Diagram Layout

Use the same dark-mode style as `08-universal-node-command-flow.drawio`:
- Background: `#1e1e2e`
- Blue (gateway / user actions): fill `#1a3a5c`, stroke `#4f9ede`
- Orange (decisions / thresholds): fill `#3a2200`, stroke `#f0a500`, diamond shape
- Green (poll / data received): fill `#0d3a0d`, stroke `#4caf50`
- Purple (balance outcomes / notes): fill `#2a1040`, stroke `#9c6fde`
- Red (node duties sent): fill `#3a1010`, stroke `#e05c5c`
- Warn/fallback box: fill `#2a1a00`, stroke `#f0a500`
- All label text white (`#ffffff`); section headers use their column colour.
- Use `labelBackgroundColor=#000000` on all edges.
- Page size: 1900 × 1200.

---

## Sections / Columns

### ① USER ENABLES POWER MANAGER  (left column, BLUE)

1. **User sets threshold** (TUI command or gateway API call)
   - e.g. `pm 6000` → threshold = 6 000 mW
   - `budget = threshold − HEADROOM_MW` (e.g. 6000 − 500 = 5500 mW)
   - On **first enable only**: all nodes' `target_duty` is frozen at their current
     sensor duty — this becomes the ceiling PM will never exceed.

2. **Bootstrap discovery** (if no nodes known yet)
   - Sends `READ` to each node address 1..N (from BLE scan count)
   - Waits for sensor response → registers node in state table
   - If nodes already known: just re-polls them with `READ`

3. Note box (purple): `target_duty` = user's original setting.
   PM can only lower duty from here, never exceed it.

---

### ② POLL CYCLE — fires every 3 s  (top-centre, GREEN)

Sequence within each cycle (left to right or top to bottom):

1. `poll_loop()` wakes up
2. **`_poll_all_nodes()`** — sends `ALL:READ` group broadcast
   - All mesh nodes respond individually with `D:X%,V:Xv,I:XmA,P:XmW`
3. **`_wait_for_responses(timeout=4 s)`** — waits until every known
   responsive node has replied for this generation, or timeout
4. **`_mark_stale_nodes()`** — any node not seen in 45 s → `responsive = False`
5. **1 s breathing gap** — let radio settle
6. **`_evaluate_and_adjust()`** — equilibrium decision (see next section)
7. **Sleep `POLL_INTERVAL` (3 s)**, then repeat

Note box: while PM is active, the web server's own auto-poll is **paused**
(PM does the polling instead).

---

### ③ EQUILIBRIUM DECISION  (centre, ORANGE diamonds)

`_evaluate_and_adjust()` — runs after every poll:

**Gate checks (skip if any true):**

1. Diamond: **PM disabled or already adjusting?** → skip
2. Diamond: **Forced evaluation?** (threshold change / priority change sets
   `_force_evaluate = True`) → bypass cooldown, continue
3. Diamond: **Cooldown elapsed?** (< 5 s since last adjustment) → skip
4. Diamond: **Any responsive nodes?** → skip if none
5. Diamond: **Budget > 0?** (threshold too low) → skip

**Dead band check (non-forced only):**

6. Diamond: **`|total_power − budget| < 5 % of budget`?** → skip (within
   dead band, no nudge needed)
7. Diamond: **All nodes at ceiling AND in sync AND under budget?** → skip
   (already optimal, nothing to do)

If none of the above gates fire → **proceed to balancing**.

Direction log: ▲ UP if `total_power < budget`, ▼ DOWN if over.

---

### ④ BALANCING — two paths  (right of decision, RED/PURPLE)

**Branch A — No priority node (equal shares)**

`_balance_proportional(nodes, budget)`:
- `share_mw = budget / N`  (N = responsive nodes)
- For each node: call `_nudge_node()`
- Log: `Balancing X/YmW (share: ZmW each) — N1:40→55%, N2:40→55%`
- Broadcast `pm_update` to WebSocket → Web Dashboard

**Branch B — Priority node set**

`_balance_with_priority(nodes, budget)`:
- `total_shares = PRIORITY_WEIGHT + (N − 1)` = e.g. 2 + 1 = 3
- `priority_budget = budget × (2 / total_shares)` → priority node gets ~67%
- `non_pri_share = remaining / (N − 1)` → other nodes split the rest
- **Ceiling check**: if priority node's `target_duty` limits it below its
  share, surplus is redistributed to non-priority nodes
- Nudge priority node first, then non-priority nodes
- Log: `Balancing X/YmW (pri:AmW, others:BmW each) — N1:50→80%(pri), N2:50→30%`
- Broadcast `pm_update` to WebSocket → Web Dashboard

---

### ⑤ NUDGE NODE  (shared by both branches, RED)

`_nudge_node(nid, ns, target_share_mw, all_nodes)`:

1. **Estimate mW/duty%**: `mw_per_pct = node.power / node.commanded_duty`
   If no data: average from other nodes; last resort = 50 mW/%
2. **Ideal duty** = `target_share_mw / mw_per_pct`
3. **Clamp to ceiling**: `min(ideal_duty, target_duty)` — never exceed user's
   original setting
4. Diamond: **New duty == current commanded duty?** → no-op, return
5. **Send `DUTY:XX` command** to node via mesh
6. Wait for node response (next poll self-corrects if lost)
7. Update `ns.commanded_duty = new_duty`

Note: `commanded_duty` (what PM sent) is used for mW/pct estimates — NOT
the sensor-reported duty — to avoid oscillation from mesh latency lag.

---

### ⑥ NODE STATE  (small reference box, inline with nudge, BLUE)

Each node tracks:
| Field | Meaning |
|---|---|
| `duty` | Last sensor-reported duty % |
| `commanded_duty` | What PM last sent |
| `target_duty` | User's original requested duty (the ceiling) |
| `power` | Last reported mW |
| `responsive` | True / False |
| `last_seen` | Time of last response |
| `poll_gen` | Generation of last poll that got a response |

---

### ⑦ RESPONSE PATH  (bottom, GREEN → BLUE)

Node sends `D:X%,V:Xv,I:XmA,P:XmW` back via BLE Mesh →
GATT node forwards via GATT notify → Pi 5 `on_sensor_data()` updates
`NodeState` → PM reads updated state in next `_evaluate_and_adjust()`

---

### NOTES / SIDE BOXES

- **Forced re-evaluation** (orange warn box): setting threshold or priority
  sets `_force_evaluate = True`, which bypasses cooldown on the next cycle.
  Also resets `commanded_duty` from actual sensor data to prevent stale
  values corrupting the mW/pct estimate.

- **Disable PM** (blue box): calls `disable()` → restores all nodes to their
  `target_duty` (original user setting) → resumes web auto-poll.

- **GATT Failover** (purple note): if the GATT node loses power, Pi 5
  reconnects. PM pauses during reconnect (`_paused = True`), then resumes.

---

## Colour Legend (bottom bar)

| Colour | Meaning |
|---|---|
| Blue `#1a3a5c` | User actions / gateway layer |
| Green `#0d3a0d` | Poll cycle / sensor data |
| Orange `#3a2200` | Decision diamonds / thresholds |
| Red `#3a1010` | Duty commands sent to nodes |
| Purple `#2a1040` | Balance outcomes / notes |
| Warn `#2a1a00` | Forced evals / special cases |

---

## Save Path

`Documentation/Flowcharts/09-power-manager-algorithm-flow.drawio`
