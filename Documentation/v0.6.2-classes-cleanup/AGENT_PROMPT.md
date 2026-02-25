# v0.6.2 Modular Code Cleanup — Agent Prompt

> **Instructions:** Copy the prompt below and pass it to a coding agent. The agent will split all ESP firmware from monolithic `main.c` files into clean, single-responsibility modules.

---

## Agent Prompt

```
You are refactoring ESP32-C6 BLE Mesh firmware into clean modules (v0.6.2).

REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Split four monolithic main.c files (781-1036 lines each) into focused .c/.h module pairs. Zero behavior changes — this is a pure cut-and-paste reorganization.

**Context:** Read these files FIRST — they contain the complete implementation plan:
- `Documentation/v0.6.2-classes-cleanup/CLASSES_CLEANUP_PLAN.md` — file structure targets per project
- `Documentation/v0.6.2-classes-cleanup/CLASSES_CLEANUP_IMPLEMENTATION.md` — exact function inventories, global variable routing, header code, and CMakeLists changes
- `MESH_IMPLEMENTATION.md` — system architecture reference (read for context only)

**Source files to refactor (read all before starting):**
- `ESP/ESP-Mesh-Node-sensor-test/main/main.c` (781 lines) — Batch 1
- `ESP/ESP_GATT_BLE_Gateway/main/main.c` (1036 lines) — Batch 2
- `ESP/ESP-Provisioner/main/main.c` (868 lines) — Batch 3
- `ESP/ESP-Mesh-Relay-Node/main/main.c` (311 lines) — Batch 4

**Architecture:** ESP-IDF 5.x C firmware. Each project uses CMake with explicit SRCS lists in `main/CMakeLists.txt`. New .c files MUST be added to this list.

**Tech Stack:** C, ESP-IDF, CMake, BLE Mesh

---

## CRITICAL RULES

1. **ZERO behavior changes** — every function does exactly what it did before
2. **Cut and paste only** — move code, don't rewrite or "improve" it
3. **Remove `static`** from functions that become cross-module, add declaration to header
4. **Use `extern`** in headers for globals accessed by multiple modules
5. **Every header** has include guards: `#ifndef X_H` / `#define X_H` / `#endif`
6. **Build after EACH batch** with `idf.py build` — must pass before next batch
7. **Delete legacy files** in sensor node: `ble_service.c` and `ble_service.h` (dead code from v0.3.0)

---

## Execution Order

### Batch 1 — Sensor Node (ESP/ESP-Mesh-Node-sensor-test/)
Split main.c into: sensor.c/.h, load_control.c/.h, command.c/.h, mesh_node.c/.h, nvs_store.c/.h
See IMPLEMENTATION.md Section 2 for exact code.
Build: `cd ESP/ESP-Mesh-Node-sensor-test && idf.py build`
Commit: `git commit -m "refactor(sensor-node): split main.c into modules"`

### Batch 2 — GATT Gateway (ESP/ESP_GATT_BLE_Gateway/)
Split main.c into: gatt_service.c/.h, mesh_gateway.c/.h, command_parser.c/.h, monitor.c/.h, node_tracker.c/.h, nvs_store.c/.h
See IMPLEMENTATION.md Section 3.
Build: `cd ESP/ESP_GATT_BLE_Gateway && idf.py build`
Commit: `git commit -m "refactor(gatt-gateway): split main.c into modules"`

### Batch 3 — Provisioner (ESP/ESP-Provisioner/)
Split main.c into: node_registry.c/.h, composition.c/.h, model_binding.c/.h, provisioning.c/.h, mesh_config.c/.h
See IMPLEMENTATION.md Section 4.
Build: `cd ESP/ESP-Provisioner && idf.py build`
Commit: `git commit -m "refactor(provisioner): split main.c into modules"`

### Batch 4 — Relay Node (ESP/ESP-Mesh-Relay-Node/)
Split main.c into: mesh_relay.c/.h, led.c/.h, nvs_store.c/.h
See IMPLEMENTATION.md Section 5.
Build: `cd ESP/ESP-Mesh-Relay-Node && idf.py build`
Commit: `git commit -m "refactor(relay-node): split main.c into modules"`

---

## Common Errors

See IMPLEMENTATION.md Section 6 for:
- `undefined reference` → function was static, needs header declaration
- `multiple definition` → variable defined in header, need extern
- `implicit declaration` → missing #include
- Circular dependencies → use forward declarations
- Log tag collisions → each module gets its own unique TAG

## Final Verification

After ALL batches:
```bash
cd ESP/ESP-Mesh-Node-sensor-test && idf.py build
cd ../ESP_GATT_BLE_Gateway && idf.py build
cd ../ESP-Provisioner && idf.py build
cd ../ESP-Mesh-Relay-Node && idf.py build
```

All four must compile successfully. Tag:

```bash
git tag v0.6.2
```

```
