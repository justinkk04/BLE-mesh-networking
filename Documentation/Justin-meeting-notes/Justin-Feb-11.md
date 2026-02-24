# Meeting Notes — Justin Kwarteng — Feb 11, 2026

| Field | Details |
|---|---|
| **Team Member** | Justin Kwarteng |
| **Progress** | Implemented custom vendor model transport (CID 0x02E5) across all three ESP32-C6 devices — Provisioner, GATT Gateway, and Mesh Nodes. Replaced the original 1-bit Generic OnOff model with arbitrary text command/response messaging (DUTY, READ, RAMP, STOP, MONITOR). Eliminated the Pico 2W UART bridge entirely — ESP32-C6 mesh nodes now read INA260 sensors directly via I2C and control load PWM via LEDC. Reduced message path from 7 hops (Pi→GATT→Mesh→Node→UART→Pico→sensor→back) to 4 hops (Pi→GATT→Mesh→Node→I2C→back). Added NVS persistence so mesh credentials survive power cycles. Fixed GATT notification chunking for payloads >20 bytes, command serialization to prevent radio contention, and gateway discovery issues. |
| **What's for tomorrow?** | Build the Pi 5 Python gateway TUI with Textual framework. Implement PowerManager for automated power balancing across mesh nodes. |
| **Hours worked since last meeting** | 20 |
| **Hurdles** | Vendor model binding required composition-data-aware provisioner — original code failed silently when gateway had different models than nodes. GATT Proxy MTU hard-limited to 23 bytes required chunked notification protocol. NVS persistence needed for both gateway state and vendor model binding flags. |
| **Notes** | Architecture simplified from 5 components (Pi + ESP + Pico per node) to 3 (Pi + ESP Gateway + ESP Nodes). Latency dropped from 500ms–2s (UART async) to ~50ms (synchronous I2C). 11 bugs resolved and documented. |
