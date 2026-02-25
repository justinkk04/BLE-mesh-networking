# v0.6.2 Python Gateway Cleanup — Agent Prompt

> **Instructions:** Copy the prompt below and pass it to a coding agent.

---

## Agent Prompt

```
You are splitting a monolithic Python gateway into clean modules (v0.6.2).

**Goal:** Split `gateway-pi5/test-13-tui.py` (1,722 lines, 4 classes + TUI + CLI) into 7 focused single-responsibility Python modules. Zero behavior changes.

**Context:** Read these files FIRST:
- `Documentation/v0.6.2-classes-cleanup/python-gateway/PYTHON_CLEANUP_PLAN.md` — target file structure
- `Documentation/v0.6.2-classes-cleanup/python-gateway/PYTHON_CLEANUP_IMPLEMENTATION.md` — exact task-by-task instructions
- `gateway-pi5/test-13-tui.py` — the monolithic source file to split

**Tech Stack:** Python 3, bleak (BLE), textual (TUI)

---

## CRITICAL RULES

1. **ZERO behavior changes** — every class/method does exactly what it did
2. **Cut and paste only** — move code, don't rewrite or "improve"
3. **One class per file** — each module has one primary class
4. **Avoid circular imports** — use `TYPE_CHECKING` guard for type hints that would create cycles (see Implementation guide Section 11.1)
5. **Keep inner Message classes** inside `MeshGatewayApp` — they are Textual Message subclasses
6. **Do NOT delete `test-13-tui.py`** — keep it as rollback reference

---

## Execution Order

1. **Create `constants.py`** — UUIDs, regex patterns, name prefixes (lines 53-64)
2. **Create `ble_thread.py`** — `BleThread` class (lines 66-123)
3. **Create `node_state.py`** — `NodeState` dataclass (lines 126-138)
4. **Create `power_manager.py`** — `PowerManager` class (lines 141-680)
   - Use `TYPE_CHECKING` for `DCMonitorGateway` type hint to avoid circular import
5. **Create `dc_gateway.py`** — `DCMonitorGateway` class (lines 682-1161)
   - Duplicate `_HAS_TEXTUAL` check at module level
   - Import constants from `constants.py`
6. **Create `tui_app.py`** — `MeshGatewayApp` class (lines 1168-1647)
   - Import textual unconditionally (this file only imported when textual available)
7. **Create `gateway.py`** — `main()` + `_run_cli()` (lines 1650-1722)
   - Entry point with argparse
   - Guard `MeshGatewayApp` import with try/except

---

## Verification

After all tasks, run:

```bash
cd gateway-pi5
python -c "
import py_compile
for f in ['constants.py', 'ble_thread.py', 'node_state.py',
          'power_manager.py', 'dc_gateway.py', 'tui_app.py', 'gateway.py']:
    py_compile.compile(f, doraise=True)
    print(f'{f}: OK')
"
```

Then import check:

```bash
python -c "
from constants import DC_MONITOR_SERVICE_UUID
from ble_thread import BleThread
from node_state import NodeState
from power_manager import PowerManager
from dc_gateway import DCMonitorGateway
print('All imports OK')
"
```

Commit:

```bash
git add gateway-pi5/*.py
git commit -m "refactor(gateway-pi5): split test-13-tui.py into modules"
```

```
