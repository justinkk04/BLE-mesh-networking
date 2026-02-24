# 00 - Start Here

If you feel overwhelmed by this repo, use this exact first run:

1. [[Beginner-Track]]
2. [[Concepts/Why-BLE-Mesh-Hybrid-Star-Mesh]]
3. [[Concepts/Roles-Server-Client-Gateway-Explained]]
4. [[Flows/End-to-End-READ-Flow]]
5. [[Labs/Hands-On-Lab-1-Trace-READ]]
6. [[Drawio/Drawio-Index]]
7. [[How-To-Actually-Learn-This-Repo]]

---

## What This Project Is

This project controls and monitors DC loads over BLE Mesh.

High-level path:

`Pi 5 Python app` -> `ESP32 GATT Gateway` -> `BLE Mesh` -> `ESP32 Sensing Nodes`

The relay node extends range. The provisioner auto-configures nodes.

---

## Your Learning Goal

Do not start with all code at once.

First learn:

1. What "vendor model" means here
2. What "unicast vs group address" means
3. Why this is hybrid star+mesh
4. When relay is actually used
5. How auto-rejoin works after power loss
6. How a single `READ` command travels through the system

Then learn:

1. How provisioning sets up keys/models
2. How `threshold` and PowerManager work

---

## Index

- Structured Folders (your requested layout):
  - [[Architecture/Architecture-Index]]
  - [[Protocol/Protocol-Index]]
  - [[ESP-IDF/ESP-IDF-Index]]
  - [[Python-Gateway/Python-Gateway-Index]]
  - [[Hardware/Hardware-Index]]
  - [[Debugging/Debugging-Index]]
- Cheat Sheets:
  - [[Cheat-Sheets/C-Basics-For-This-Repo]]
  - [[Cheat-Sheets/Python-Basics-For-This-Repo]]
- Concepts:
  - [[Concepts/Why-BLE-Mesh-Hybrid-Star-Mesh]]
  - [[Concepts/Roles-Server-Client-Gateway-Explained]]
  - [[Concepts/Vendor-Model-Explained]]
  - [[Concepts/Addressing-Unicast-vs-Group]]
  - [[Concepts/Relay-Behavior-and-When-It-Activates]]
  - [[Concepts/Self-Healing-and-Auto-Rejoin]]
  - [[Concepts/WireGuard-Concepts]]
  - [[Concepts/Provisioning-Explained]]
- Component Maps:
  - [[Components/ESP-Provisioner-Map]]
  - [[Components/ESP-GATT-Gateway-Map]]
  - [[Components/ESP-Sensing-Node-Map]]
  - [[Components/ESP-Relay-Node-Map]]
  - [[Components/Pi5-gateway.py-Map]]
- Flows:
  - [[Flows/End-to-End-READ-Flow]]
  - [[Flows/Scan-and-Connect-By-Address-Flow]]
  - [[Flows/Target-Node-and-ALL-Flow]]
  - [[Flows/Provisioning-Lifecycle-Flow]]
  - [[Flows/Relay-Out-of-Range-Flow]]
  - [[Flows/RAMP-One-vs-ALL-Flow]]
  - [[Flows/Power-Loss-Auto-Rejoin-Flow]]
  - [[Flows/PowerManager-Flow]]
  - [[Flows/Drawio-Blueprints]]
- Practice:
  - [[Labs/Hands-On-Lab-1-Trace-READ]]
  - [[Labs/Hands-On-Lab-2-Trace-Threshold]]
- Reference:
  - [[Glossary]]
  - [[Drawio/Drawio-Index]]
  - [[How-To-Actually-Learn-This-Repo]]
