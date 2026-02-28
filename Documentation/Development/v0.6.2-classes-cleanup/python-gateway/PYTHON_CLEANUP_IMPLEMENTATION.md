# v0.6.2 — Python Gateway Modular Cleanup Implementation Guide

**Date:** February 25, 2026
**Author:** Justin Kwarteng
**Purpose:** Feed this document to an agent to split `test-13-tui.py` into clean Python modules.

---

## 1. Source File

**File:** `gateway-pi5/test-13-tui.py` (1,722 lines)

**Class inventory:**

| Class / Section | Lines | Methods | Target Module |
|---|---|---|---|
| Imports + constants | 1-65 | — | `constants.py` |
| `BleThread` | 66-123 | `__init__`, `start`, `submit`, `submit_async`, `stop`, `_exception_handler` | `ble_thread.py` |
| `NodeState` | 126-138 | dataclass fields only | `node_state.py` |
| `PowerManager` | 141-680 | 18 methods | `power_manager.py` |
| `DCMonitorGateway` | 682-1161 | 18 methods | `dc_gateway.py` |
| `MeshGatewayApp` | 1168-1647 | 18 methods + 3 inner `Message` classes | `tui_app.py` |
| `main()` + `_run_cli()` | 1650-1722 | 2 functions | `gateway.py` |

---

## 2. Rules

1. **Zero behavior changes** — every method does exactly what it did before
2. **Cut and paste** — move code between files, don't rewrite
3. **One class per file** — each module has one primary class
4. **Imports follow dependency order** — no circular imports
5. **Test after creation** — `python -c "from module import Class"` must work for each

---

## 3. Task 1: Create `constants.py`

**Create:** `gateway-pi5/constants.py`

Extract from `test-13-tui.py` lines 53-64:

```python
"""Shared constants for the DC Monitor Gateway."""

import re

# Custom UUIDs matching ESP32-C6 ble_service.h
DC_MONITOR_SERVICE_UUID = "0000dc01-0000-1000-8000-00805f9b34fb"
SENSOR_DATA_CHAR_UUID = "0000dc02-0000-1000-8000-00805f9b34fb"
COMMAND_CHAR_UUID = "0000dc03-0000-1000-8000-00805f9b34fb"

# Device name prefixes to look for
DEVICE_NAME_PREFIXES = ["Mesh-Gateway", "ESP-BLE-MESH"]

# Sensor data parsing regex (case-insensitive for mA/mW/MA/MW)
SENSOR_RE = re.compile(r'D:(\d+)%,V:([\d.]+)V,I:([\d.]+)mA,P:([\d.]+)mW', re.IGNORECASE)
NODE_ID_RE = re.compile(r'NODE(\d+)', re.IGNORECASE)
```

---

## 4. Task 2: Create `ble_thread.py`

**Create:** `gateway-pi5/ble_thread.py`

Move the entire `BleThread` class (lines 66-123) with its imports:

```python
"""Dedicated BLE I/O thread with persistent asyncio event loop."""

import asyncio
import threading
import traceback
from typing import Optional


class BleThread:
    # ... entire class from lines 66-123, unchanged ...
```

---

## 5. Task 3: Create `node_state.py`

**Create:** `gateway-pi5/node_state.py`

Move the `NodeState` dataclass (lines 126-138):

```python
"""Data class tracking the state of a single mesh node."""

import time
from dataclasses import dataclass, field


@dataclass
class NodeState:
    # ... entire dataclass from lines 126-138, unchanged ...
```

---

## 6. Task 4: Create `power_manager.py`

**Create:** `gateway-pi5/power_manager.py`

Move the `PowerManager` class (lines 141-680). This is the largest class.

```python
"""Equilibrium-based power balancer for mesh nodes."""

import asyncio
import time
from typing import Optional

from node_state import NodeState


class PowerManager:
    # ... entire class from lines 141-680, unchanged ...
```

**Import notes:**

- `NodeState` comes from `node_state.py`
- `PowerManager.__init__` takes a `gateway` parameter (typed as `DCMonitorGateway`), but we don't need to import it — just use duck typing or `TYPE_CHECKING`:

```python
from __future__ import annotations
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from dc_gateway import DCMonitorGateway
```

This avoids a circular import since `DCMonitorGateway` also references `PowerManager`.

---

## 7. Task 5: Create `dc_gateway.py`

**Create:** `gateway-pi5/dc_gateway.py`

Move the `DCMonitorGateway` class (lines 682-1161).

```python
"""BLE GATT gateway client for DC Monitor Mesh Network."""

import asyncio
import threading
import time
from datetime import datetime
from typing import Optional

from bleak import BleakClient, BleakScanner
from bleak.backends.characteristic import BleakGATTCharacteristic

from constants import (
    DC_MONITOR_SERVICE_UUID,
    SENSOR_DATA_CHAR_UUID,
    COMMAND_CHAR_UUID,
    DEVICE_NAME_PREFIXES,
    SENSOR_RE,
    NODE_ID_RE,
)
from power_manager import PowerManager

# Check for textual availability (needed for log routing)
_HAS_TEXTUAL = False
try:
    from textual.app import App
    _HAS_TEXTUAL = True
except ImportError:
    pass


class DCMonitorGateway:
    # ... entire class from lines 682-1161, unchanged ...
```

**Import notes:**

- Constants from `constants.py`
- `PowerManager` from `power_manager.py` (used in `interactive_mode()` and as `_power_manager` attribute)
- `_HAS_TEXTUAL` check — duplicate the try/except here since `log()` method uses it

---

## 8. Task 6: Create `tui_app.py`

**Create:** `gateway-pi5/tui_app.py`

Move `MeshGatewayApp` (lines 1168-1647). Guard the entire file with `_HAS_TEXTUAL`:

```python
"""Textual TUI application for the DC Monitor Mesh Gateway."""

import asyncio

from textual.app import App, ComposeResult
from textual.containers import Horizontal, Vertical
from textual.message import Message
from textual.widgets import Header, Footer, Input, RichLog, DataTable, Static
from textual import work, on

from ble_thread import BleThread
from dc_gateway import DCMonitorGateway
from power_manager import PowerManager


class MeshGatewayApp(App):
    # ... entire class from lines 1168-1647, unchanged ...
    # Inner Message classes (SensorDataMsg, LogMsg, PowerAdjustMsg) stay inside
```

**Import notes:**

- All textual imports at module level (no try/except needed — this module is only imported when textual is available)
- `BleThread` from `ble_thread.py`
- `DCMonitorGateway` from `dc_gateway.py`
- `PowerManager` from `power_manager.py`

---

## 9. Task 7: Create `gateway.py` (Entry Point)

**Create:** `gateway-pi5/gateway.py`

Move `main()` and `_run_cli()` (lines 1650-1722):

```python
#!/usr/bin/env python3
"""
BLE Gateway for DC Monitor Mesh Network
Connects to ESP32-C6 GATT gateway and sends commands to mesh nodes

Usage:
    python gateway.py                       # TUI interactive mode (default)
    python gateway.py --scan                # Just scan for gateways
    python gateway.py --node 0 --ramp       # Send RAMP to node 0
    python gateway.py --node 1 --duty 50    # Set duty 50% on node 1
    python gateway.py --node all --stop     # Stop all nodes
    python gateway.py --node 0 --read       # Single sensor reading
    python gateway.py --node 0 --monitor    # Continuous monitoring
    python gateway.py --no-tui              # Plain CLI mode (legacy)
"""

import argparse
import asyncio
import sys

from dc_gateway import DCMonitorGateway

# Check for textual
_HAS_TEXTUAL = False
try:
    from tui_app import MeshGatewayApp
    _HAS_TEXTUAL = True
except ImportError:
    print("Note: textual not available. Install with: pip install textual")
    print("      Falling back to plain CLI mode.\n")


def main():
    # ... from lines 1650-1694, unchanged ...
    # Replace `MeshGatewayApp` reference — it's imported above


async def _run_cli(args, node: str):
    # ... from lines 1697-1722, unchanged ...


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nGoodbye!")
```

---

## 10. Task 8: Verify

### Step 1: Syntax check all files

```bash
cd gateway-pi5
python -c "
import py_compile
for f in ['constants.py', 'ble_thread.py', 'node_state.py', 
          'power_manager.py', 'dc_gateway.py', 'tui_app.py', 'gateway.py']:
    py_compile.compile(f, doraise=True)
    print(f'{f}: OK')
print('All files compile OK')
"
```

### Step 2: Import check

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

### Step 3: TUI import (only if textual installed)

```bash
python -c "from tui_app import MeshGatewayApp; print('TUI import OK')"
```

### Step 4: Commit

```bash
git add gateway-pi5/constants.py gateway-pi5/ble_thread.py \
        gateway-pi5/node_state.py gateway-pi5/power_manager.py \
        gateway-pi5/dc_gateway.py gateway-pi5/tui_app.py \
        gateway-pi5/gateway.py
git commit -m "refactor(gateway-pi5): split test-13-tui.py into modules"
```

---

## 11. Common Pitfalls

### 11.1 Circular Imports

**Symptom:** `ImportError: cannot import name 'X' from partially initialized module 'Y'`

**Fix:** Use `TYPE_CHECKING` guard for type hints that would create cycles:

```python
from __future__ import annotations
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from dc_gateway import DCMonitorGateway
```

The main circular risk is `PowerManager` ↔ `DCMonitorGateway`. Use the pattern above in `power_manager.py`.

### 11.2 `_HAS_TEXTUAL` Duplication

The `_HAS_TEXTUAL` flag is currently checked in the global scope. After the split:

- `dc_gateway.py` needs its own check (for `log()` method routing)
- `gateway.py` needs its own check (to decide TUI vs CLI)
- `tui_app.py` assumes textual is available (imported unconditionally)

### 11.3 Inner Message Classes

`MeshGatewayApp` has inner classes (`SensorDataMsg`, `LogMsg`, `PowerAdjustMsg`). These **stay inside** `MeshGatewayApp` — they are Textual Message subclasses that need the parent class context.

`DCMonitorGateway.log()` and `notification_handler()` reference `self.app.LogMsg` and `self.app.SensorDataMsg`. These work because `self.app` is a `MeshGatewayApp` instance at runtime.

### 11.4 Module-Level `_HAS_TEXTUAL` in `dc_gateway.py`

The `DCMonitorGateway` class checks `_HAS_TEXTUAL` in its `log()` method. After splitting, define this at the top of `dc_gateway.py`:

```python
_HAS_TEXTUAL = False
try:
    from textual.app import App
    _HAS_TEXTUAL = True
except ImportError:
    pass
```

---

## 12. Summary

| Before | After |
|---|---|
| 1 × 1,722-line file | 7 focused modules |
| 4 classes + TUI + CLI in one file | 1 class per file |
| Hard to navigate | Each file < 540 lines |
| Hard to test individually | Can import/test any class alone |

**Zero behavior changes. Zero protocol changes.**
