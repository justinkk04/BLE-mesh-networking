# Meeting Notes — Justin Kwarteng — Feb 12, 2026

| Field | Details |
|---|---|
| **Team Member** | Justin Kwarteng |
| **Progress** | Built Textual TUI for Pi 5 gateway with live node table, scrollable log, sidebar status, and debug toggle (F2). Implemented equilibrium-based PowerManager — automatically balances total power across N nodes under a user-set threshold. Features: priority mode (2x weight), bidirectional nudging (ramp up and down), dynamic node discovery from BLE scan count, event-driven command pacing. Fixed 7 bugs: thread race condition blocking threshold/priority re-evaluation, deadband logic preventing rebalancing, stale commanded_duty corrupting ramp-up calculations, ghost node sends, poll loop cancellation race, rounding precision, and target_duty re-snapshot on threshold change. |
| **What's for tomorrow?** | Implement group addressing (v0.6.0) — subscribe nodes to group 0xC000 so ALL commands broadcast once instead of N sequential unicasts. Then self-healing gateway failover (v0.7.0). Final stress testing and documentation for v1.0. |
| **Hours worked since last meeting** | 20 |
| **Hurdles** | PM thread race conditions between TUI thread and BLE thread — threshold changes silently failed because concurrent evaluation overwrote force-evaluation flags. Deadband check blocked priority rebalancing when total power was near budget despite unequal individual shares. |
| **Notes** | v0.5.0 feature-complete for single-gateway operation. Roadmap: group addressing (v0.6.0, drops poll time from O(N) to O(1)), self-healing failover (v0.7.0), stress testing + docs (v1.0.0). Full implementation documented in MESH_IMPLEMENTATION.md (830 lines, 16 resolved issues). |
