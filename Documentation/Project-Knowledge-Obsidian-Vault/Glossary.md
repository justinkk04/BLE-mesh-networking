# Glossary

- BLE Mesh: multi-hop Bluetooth mesh networking
- Provisioner: device that adds/configures nodes in mesh
- Unicast: one specific destination node address
- Group Address: one address shared by many nodes (e.g., `0xC000`)
- Vendor Model: custom mesh model (non-SIG) for app-specific messages
- SIG Model: standard Bluetooth Mesh model
- NetKey: network-level mesh key
- AppKey: application-level key used by models
- Model Bind: linking AppKey to a model so it can send/receive app messages
- GATT: BLE service/characteristic protocol used between Pi and ESP gateway
- INA260: current/voltage/power sensor read over I2C
- PWM: pulse-width modulation used to control load duty
- TTL: hop limit in mesh forwarding
- Relay Node: forwards mesh packets, usually no sensor role
- PowerManager: Python logic that keeps total power near threshold budget
