# Prompt: Power Manager Algorithm Flowchart

> **Usage:** Copy everything below the `---` line and paste it as a prompt in a new conversation.

---

## Task

Create a `.drawio` XML file for the **Power Manager Algorithm Flow** — the equilibrium-based balancing algorithm. Save it to `Documentation/Flowcharts/04-power-manager-algorithm-flow.drawio`.

## Source File
`gateway-pi5/gateway-code/power_manager.py` — class `PowerManager`

## Constants (show as a reference box/table in the diagram)
| Constant | Value | Purpose |
|----------|-------|---------|
| `POLL_INTERVAL` | 3.0s | Seconds between poll cycles |
| `READ_STAGGER` | 2.5s | Gap between sequential READ commands |
| `STALE_TIMEOUT` | 45.0s | Mark node unresponsive after this |
| `COOLDOWN` | 5.0s | Min time between adjustments |
| `HEADROOM_MW` | 500.0 mW | Buffer below threshold (budget = threshold - headroom) |
| `PRIORITY_WEIGHT` | 2.0 | Priority node gets 2x power share |

## Entry Point: `set_threshold(mw)`

1. Store `threshold_mw = mw`
2. If first enable: **freeze target_duty** from current sensor duty for ALL nodes (this is the user's ceiling)
3. Set `_force_evaluate = True` (bypass cooldown on next cycle)
4. Calculate: `budget = mw - HEADROOM_MW`, `share = budget / N`
5. Log threshold and budget
6. Triggers `poll_loop()` to start (kicked off by TUI worker or asyncio task)

## Main Loop: `poll_loop()`

Show as a loop with clear entry/exit:

1. **Bootstrap check**: if `_needs_bootstrap` → `_bootstrap_discovery()`:
   - Probe addresses 1.._sensing_node_count_
   - Send READ to each, wait for response
   - Nodes that respond with sensor data are real sensing nodes
   - Sleep 2s after bootstrap

2. Set `_polling = True`
3. If PM is active on web dashboard: cancel `_web_poll_task` (PM does its own polling)

4. **Loop** (while `threshold_mw is not None`):
   - If `_paused` (reconnecting): sleep 1s, continue
   - `_poll_all_nodes()` → send `ALL:READ` (group broadcast 0xC000)
   - `_wait_for_responses(timeout=4s)` → wait until all responsive nodes report for this poll generation, or timeout
   - `_mark_stale_nodes()` → any node not seen in 45s marked unresponsive
   - Sleep 1.0s (relay breathing gap)
   - `_evaluate_and_adjust()` — **THE CORE ALGORITHM**
   - Sleep `POLL_INTERVAL` (3s)

5. On exit: `_polling = False`, resume web auto-poll if requested

## Core Algorithm: `_evaluate_and_adjust()`

Show this as a detailed decision tree:

### Pre-checks (top-down diamonds)
1. Diamond: **`threshold_mw is None OR _adjusting?`** → Yes: skip
2. Diamond: **`Forced evaluation?`** (`_force_evaluate`)
   - Yes: clear flag, proceed directly (bypass cooldown + deadband)
   - Also: reset `commanded_duty` from actual sensor data on forced evals
   - No: continue to cooldown check
3. Diamond: **`Cooldown elapsed?`** (`since < COOLDOWN`)
   - No: skip
   - Yes: continue
4. Get `responsive` nodes dict (only nodes with `responsive == True`)
5. Diamond: **`No responsive nodes?`** → skip
6. Calculate `budget = threshold_mw - HEADROOM_MW`
7. Diamond: **`budget <= 0?`** → skip (threshold too low)
8. Calculate `total_power = sum of responsive nodes' power`

### Deadband Check (only on non-forced evals)
9. Diamond: **`|total_power - budget| < 5% of budget?`** → skip (within deadband, no jitter)

### Ceiling Check (only on non-forced evals)
10. Diamond: **`All nodes at ceiling AND in sync AND under budget?`** → skip
    - "At ceiling" = `commanded_duty >= target_duty`
    - "In sync" = `|sensor duty - commanded_duty| <= 2%`
    - If at ceiling but NOT in sync: log desync warning

### Direction Decision
11. Box: Determine direction: `total_power < budget` → **▲ UP** | `total_power >= budget` → **▼ DOWN**

### Balancing (two paths)
12. Diamond: **`Priority node set?`**
    - **No** → `_balance_proportional()`: each node gets `budget / N` → `_nudge_node()` for each
    - **Yes** → `_balance_with_priority()`:
      - Priority node gets `budget × (PRIORITY_WEIGHT / total_shares)` where `total_shares = PRIORITY_WEIGHT + (N-1)`
      - Non-priority nodes each get `budget × (1 / total_shares)`
      - If priority can't use full share (limited by ceiling): surplus redistributed to others
      - `_nudge_node()` for each

13. Set `_last_adjustment = now`

## `_nudge_node()` Detail (sub-flowchart)

1. `_estimate_mw_per_pct(ns)`:
   - If `commanded_duty > 0 AND power > 0`: `mw_per_pct = power / commanded_duty`
   - Else: average from other nodes with data
   - Last resort fallback: 50.0 mW/pct
2. Calculate `ideal_duty = target_share_mw / mw_per_pct`
3. Clamp to `[0, target_duty]` — **never exceed user's original setting**
4. `new_duty = round(ideal_duty_clamped)`
5. Diamond: **`new_duty == current?`** → No change needed, return None
6. Send `set_duty(node_id, new_duty)` via BLE
7. `_wait_node_response()` (up to 5s)
8. Always update `commanded_duty = new_duty` (don't require confirmation — next poll self-corrects)
9. Return change description string (e.g., "N1:50->35%")

## `on_sensor_data()` Hook (side annotation)
- Called from `notification_handler()` whenever sensor data arrives
- Updates: `duty`, `voltage`, `current`, `power`, `last_seen`, `responsive = True`, `poll_gen`
- **Only** syncs `commanded_duty` from sensor when PM is OFF (prevents oscillation when PM is active)
- **Never** auto-syncs `target_duty` from sensor data (must be set by explicit user commands only)

## `disable()` Flow (separate small section)
1. Set `threshold_mw = None`
2. Set `_polling = False`
3. Sleep 2s (let in-flight mesh commands complete)
4. For each node: restore `commanded_duty → target_duty` (the user's original setting)
5. Log "Threshold disabled"

## Style
- Main flow: top-to-bottom with the loop clearly showing the cycle
- Decision diamonds at each check/skip point
- Color code: Green for "proceed", Red for "skip/return"
- Use orange for the nudge_node sub-flowchart
- Reference box for constants
- Annotation notes for key design decisions (why cooldown, why deadband, why no auto-sync)
