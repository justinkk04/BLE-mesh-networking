# Meeting Notes — Justin Kwarteng — Feb 15, 2026

| Field | Details |
|---|---|
| **Team Member** | Justin Kwarteng |
| **Progress** | **v0.6.0 Group Addressing:** Replaced sequential unicast commands with a single BLE Mesh group broadcast (address 0xC000). Provisioner subscribes all sensor nodes to this group. `ALL:READ` time reduced from ~5s to ~0.5s — O(N) to O(1). **v0.6.1 Relay-Only Node:** Created `ESP/ESP-Mesh-Relay-Node` by stripping the 781-line sensing firmware down to a 270-line relay-only firmware. Relay node forwards mesh packets (TTL=7), persists credentials in NVS for auto-rejoin, and blinks an LED heartbeat (fast=unprovisioned, slow=active). No vendor model, I2C, or PWM. Verified on hardware: provisioner auto-discovers and provisions relay, Pi 5 gateway correctly identifies it as non-sensing (`"Node 3 no response"` during discovery), PowerManager operates normally with 2 sensing nodes while relay silently extends range. |
| **What's for tomorrow?** | Begin implementation of v0.7.0: Self-healing Gateway Failover. Nodes will detect gateway loss and hold last state or enter safe mode. |
| **Hours worked since last meeting** | 5 |
| **Hurdles** | v0.6.0: Encountered 3 critical SDK behaviors — (1) `need_ack=false` drops responses, (2) nodes reply from group address by default, (3) SDK delivers matched responses via `RECV_PUBLISH_MSG_EVT` instead of `OPERATION_EVT`. v0.6.1: BLE scan count inflated by 1 due to relay advertising as `ESP-BLE-MESH` — cosmetic only, gateway handles gracefully. |
| **Notes** | v0.6.0 and v0.6.1 both complete and verified. v0.6.1 relay node tested with sensing node in another room — commands forwarded correctly through relay. Documentation in `v0.6.0-group-addressing/` and `v0.6.1-relay-node/`. |
