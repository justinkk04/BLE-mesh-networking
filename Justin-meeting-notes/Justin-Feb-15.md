# Meeting Notes — Justin Kwarteng — Feb 15, 2026

| Field | Details |
|---|---|
| **Team Member** | Justin Kwarteng |
| **Progress** | Implemented v0.6.0 Group Addressing. Replaced sequential unicast commands with a single BLE Mesh group broadcast (to address 0xC000). Provisioner now subscribes all sensor nodes to this group. Gateways send one `ALL:COMMAND` which reaches all nodes simultaneously. Responses are collected asynchronously by the Pi 5. Performance improved drastically: `ALL:READ` time reduced from ~5s (for 2 nodes) to ~0.5s. Poll time changed from O(N) to O(1). Verified correct behavior for both fire-and-forget (DUTY) and response-required (READ) commands. |
| **What's for tomorrow?** | Begin implementation of v0.7.0: Self-healing Gateway Failover. Nodes will detect gateway loss and hold last state or enter safe mode. |
| **Hours worked since last meeting** | 4 |
| **Hurdles** | Encountered 3 critical SDK behaviors: (1) `need_ack=false` causes the ESP-IDF client model to drop responses (fixed by using `need_ack=true`); (2) Nodes reply using the group address as source by default (fixed by manually overriding `recv_dst` to the node's unicast address); (3) SDK delivers matched responses via `RECV_PUBLISH_MSG_EVT` instead of `OPERATION_EVT`, requiring a new handler. |
| **Notes** | v0.6.0 is complete and verified. Massive performance gain for the PowerManager. Full system erase and re-provisioning was required to apply group subscriptions. Documentation updated in `v0.6.0-group-addressing/`. |
