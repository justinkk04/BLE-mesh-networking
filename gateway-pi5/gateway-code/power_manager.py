"""Equilibrium-based power balancer for mesh nodes.

Maintains total power near (threshold - headroom) by nudging node
duty cycles up or down each poll cycle:
  - No priority: all nodes get equal power share (budget/N)
  - With priority: priority node gets PRIORITY_WEIGHT x normal share
  - Bidirectional: increases duty when under budget, decreases when over
  - Gradual: max STEP_SIZE% change per cycle prevents oscillation
"""

from __future__ import annotations

import asyncio
import time
from typing import Optional, TYPE_CHECKING

from node_state import NodeState

if TYPE_CHECKING:
    from dc_gateway import DCMonitorGateway


class PowerManager:
    """Equilibrium-based power balancer for mesh nodes.

    Maintains total power near (threshold - headroom) by nudging node
    duty cycles up or down each poll cycle:
      - No priority: all nodes get equal power share (budget/N)
      - With priority: priority node gets PRIORITY_WEIGHT x normal share
      - Bidirectional: increases duty when under budget, decreases when over
      - Gradual: max STEP_SIZE% change per cycle prevents oscillation
    """

    POLL_INTERVAL = 3.0    # Seconds between poll cycles
    READ_STAGGER = 2.5     # Seconds between READ commands (must exceed mesh SEND_COMP time)
    STALE_TIMEOUT = 45.0   # Seconds before marking node unresponsive (relay round trips are slow)
    COOLDOWN = 5.0         # Seconds between adjustments (give mesh time to settle)
    HEADROOM_MW = 500.0    # Target buffer below threshold (budget = threshold - headroom)
    PRIORITY_WEIGHT = 2.0  # Priority node gets this many "shares" vs 1 for normal nodes

    def __init__(self, gateway):
        self.gateway = gateway
        self.nodes: dict[str, NodeState] = {}
        self.threshold_mw: Optional[float] = None
        self.priority_node: Optional[str] = None
        self._adjusting = False
        self._last_adjustment: float = 0
        self._force_evaluate = False  # Set True to bypass cooldown on next eval
        self._poll_generation: int = 0
        self._polling = False  # True while a poll cycle is active
        self._needs_bootstrap = False

    # ---- Public API ----

    def set_threshold(self, mw: float):
        """Enable power management with the given threshold in mW."""
        first_enable = self.threshold_mw is None
        self.threshold_mw = mw
        self._needs_bootstrap = not self.nodes
        if first_enable:
            # Freeze target_duty from current sensor duty for ALL nodes.
            # This is the "initialization → PM" transition: whatever duty
            # the user set before enabling PM becomes the ceiling.
            # Only done on first enable — changing threshold while PM is
            # active must NOT re-snapshot (would capture PM-reduced values).
            for ns in self.nodes.values():
                if ns.duty > 0:
                    ns.target_duty = ns.duty
                    self.gateway.log(
                        f"[PM] N{ns.node_id} target frozen at {ns.duty}%")
        # Force immediate evaluation on next poll cycle
        # (uses a flag instead of _last_adjustment=0 to survive race with
        # a concurrently-finishing _evaluate_and_adjust on the BLE thread)
        self._force_evaluate = True
        self._adjusting = False  # Clear any in-progress flag
        budget = mw - self.HEADROOM_MW
        n = len([ns for ns in self.nodes.values() if ns.responsive]) or 1
        share = budget / n
        self.gateway.log(
            f"[POWER] Threshold: {mw:.0f}mW → budget {budget:.0f}mW "
            f"({share:.0f}mW × {n} nodes)")

    async def disable(self):
        """Disable power management and restore original duty cycles."""
        self.threshold_mw = None
        self._polling = False
        # Wait for any in-flight mesh commands to complete before restoring
        await asyncio.sleep(2.0)
        # Restore all nodes to their target duty
        for ns in self.nodes.values():
            if ns.commanded_duty != ns.target_duty and ns.target_duty > 0:
                self.gateway.log(
                    f"[POWER] Restoring node {ns.node_id}: {ns.commanded_duty}% → {ns.target_duty}%")
                await self.gateway.set_duty(
                    ns.node_id, ns.target_duty, _from_power_mgr=True, _silent=True)
                await self.gateway._wait_node_response(ns.node_id)
            ns.commanded_duty = 0  # Reset commanded state
        self.gateway.log("[POWER] Threshold disabled")

    def set_priority(self, node_id: str):
        """Set the priority node. Triggers immediate rebalance."""
        self.priority_node = node_id
        self._force_evaluate = True  # Force rebalance on next cycle
        if self.threshold_mw:
            budget = self.threshold_mw - self.HEADROOM_MW
            responsive = [ns for ns in self.nodes.values() if ns.responsive]
            n = len(responsive) or 1
            total_shares = self.PRIORITY_WEIGHT + (n - 1)
            pri_share = budget * (self.PRIORITY_WEIGHT / total_shares)
            other_share = budget * (1.0 / total_shares)
            self.gateway.log(
                f"[POWER] Priority: N{node_id} ({pri_share:.0f}mW), "
                f"others: {other_share:.0f}mW each")
        else:
            self.gateway.log(f"[POWER] Priority node: {node_id}")

    def clear_priority(self):
        """Remove priority designation. Triggers immediate rebalance to equal shares."""
        self.priority_node = None
        self._force_evaluate = True  # Force rebalance on next cycle
        if self.threshold_mw:
            budget = self.threshold_mw - self.HEADROOM_MW
            n = len([ns for ns in self.nodes.values() if ns.responsive]) or 1
            share = budget / n
            self.gateway.log(f"[POWER] Priority cleared → equalizing at {share:.0f}mW each")
        else:
            self.gateway.log("[POWER] Priority cleared")

    def set_target_duty(self, node_id: str, duty: int):
        """Record the user-requested duty for a node."""
        if node_id not in self.nodes:
            self.nodes[node_id] = NodeState(node_id=node_id)
        self.nodes[node_id].target_duty = duty
        # Also sync commanded_duty so PM's mw_per_pct estimate stays accurate
        # when user changes duty while PM is active
        self.nodes[node_id].commanded_duty = duty

    def status(self) -> str:
        """Return a human-readable status summary."""
        lines = []
        lines.append("--- Power Manager ---")
        if self.threshold_mw is not None:
            budget = self.threshold_mw - self.HEADROOM_MW
            lines.append(f"Threshold: {self.threshold_mw:.0f} mW")
            lines.append(f"Budget:    {budget:.0f} mW (headroom: {self.HEADROOM_MW:.0f} mW)")
        else:
            lines.append("Threshold: OFF")
        if self.priority_node is not None:
            lines.append(f"Priority:  node {self.priority_node}")
        else:
            lines.append("Priority:  none")

        total = 0.0
        responsive_count = sum(1 for ns in self.nodes.values() if ns.responsive)
        if self.nodes:
            # Calculate shares for display
            share_info = {}
            if self.threshold_mw is not None and responsive_count > 0:
                budget = self.threshold_mw - self.HEADROOM_MW
                if self.priority_node and self.priority_node in self.nodes:
                    total_shares = self.PRIORITY_WEIGHT + (responsive_count - 1)
                    for nid in self.nodes:
                        if nid == self.priority_node:
                            share_info[nid] = budget * (self.PRIORITY_WEIGHT / total_shares)
                        else:
                            share_info[nid] = budget * (1.0 / total_shares)
                else:
                    per_share = budget / responsive_count
                    for nid in self.nodes:
                        share_info[nid] = per_share

            lines.append("Nodes:")
            for nid in sorted(self.nodes.keys()):
                ns = self.nodes[nid]
                st = "ok" if ns.responsive else "stale"
                target = f" (target:{ns.target_duty}%)" if ns.target_duty != ns.duty else ""
                share = f" share:{share_info[nid]:.0f}mW" if nid in share_info else ""
                lines.append(
                    f"  Node {nid}: D:{ns.duty}%{target} "
                    f"V:{ns.voltage:.2f}V I:{ns.current:.1f}mA "
                    f"P:{ns.power:.0f}mW [{st}]{share}"
                )
                if ns.responsive:
                    total += ns.power
            lines.append(f"Total power: {total:.0f} mW")
            if self.threshold_mw is not None:
                headroom = self.threshold_mw - total
                lines.append(f"Headroom:    {headroom:.0f} mW")
        else:
            lines.append("No nodes discovered yet")
        lines.append("--------------------")
        return "\n".join(lines)

    # ---- Notification Hook ----

    def on_sensor_data(self, node_id: str, duty: int, voltage: float,
                       current: float, power: float):
        """Update node state from parsed sensor data."""
        if node_id not in self.nodes:
            self.nodes[node_id] = NodeState(node_id=node_id)

        ns = self.nodes[node_id]
        ns.duty = duty
        ns.voltage = voltage
        ns.current = current
        ns.power = power
        ns.last_seen = time.monotonic()
        ns.responsive = True
        ns.poll_gen = self._poll_generation

        # Only sync commanded_duty when PM is OFF — when PM is active,
        # only _nudge_node() updates commanded_duty (avoids stale sensor
        # data overwriting what PM just sent, which causes oscillation)
        if self.threshold_mw is None:
            ns.commanded_duty = duty

        # Don't auto-sync target_duty from sensor data — it must only be set
        # by explicit user commands (set_target_duty). Auto-sync caused PM to
        # "forget" the original target after disable() because sensor data
        # reported the reduced duty, overwriting target_duty.

    # ---- Internal Control Loop ----

    async def _bootstrap_discovery(self):
        """Discover sensing nodes by probing only as many addresses as the BLE scan found.

        The BLE scan already told us how many mesh devices exist:
        sensing_node_count = total_mesh_devices - 1 (GATT gateway).
        We probe addresses 1..sensing_node_count. Nodes that respond with
        sensor data are sensing nodes; relays/others are ignored.
        """
        count = self.gateway.sensing_node_count
        if count == 0:
            self.gateway.log("[POWER] No sensing nodes found in BLE scan")
            return

        # Skip if we already know all nodes (e.g. user already sent commands)
        if len(self.gateway.known_nodes) >= count:
            self.gateway.log(
                f"[POWER] {len(self.gateway.known_nodes)} node(s) already discovered")
            # Seed PM from known_nodes
            for nid in self.gateway.known_nodes:
                if nid not in self.nodes:
                    await self.gateway.send_to_node(nid, "READ", _silent=True)
                    await self.gateway._wait_node_response(nid)
            return

        self.gateway.log(f"[POWER] Probing {count} sensing node(s)...")
        for nid in range(1, count + 1):
            if self.threshold_mw is None:
                return
            nid_str = str(nid)
            if nid_str in self.nodes:
                self.gateway.log(f"[POWER] Node {nid} already known")
                continue
            await self.gateway.send_to_node(nid_str, "READ", _silent=True)
            responded = await self.gateway._wait_node_response(nid_str)
            if responded:
                self.gateway.log(f"[POWER] Found node {nid}")
            else:
                self.gateway.log(f"[POWER] Node {nid} no response")
        self.gateway.log(f"[POWER] Discovery complete: {len(self.nodes)} node(s)")

    async def poll_loop(self):
        """Periodic poll-and-adjust cycle. Called by TUI @work or asyncio task."""
        if self._polling:
            # Another poll_loop is running. In TUI mode with exclusive workers,
            # the old one may be mid-cancellation but hasn't cleared _polling yet.
            # Wait briefly for it to finish, then proceed.
            for _ in range(10):
                await asyncio.sleep(0.1)
                if not self._polling:
                    break
            else:
                # Old loop is genuinely still running — let it handle things
                return
        try:
            if getattr(self, '_needs_bootstrap', False):
                self._needs_bootstrap = False
                await self._bootstrap_discovery()
                await asyncio.sleep(2.0)
            self._polling = True
            while self.threshold_mw is not None:
                await self._poll_all_nodes()
                await self._wait_for_responses(timeout=4.0)
                self._mark_stale_nodes()
                await asyncio.sleep(1.0)  # Relay breathing gap — let radio catch up
                await self._evaluate_and_adjust()
                await asyncio.sleep(self.POLL_INTERVAL)
            self._polling = False
        except asyncio.CancelledError:
            self._polling = False

    async def _poll_all_nodes(self):
        """Poll all nodes with a single group READ.

        Sends ALL:READ which the GATT gateway translates to a BLE Mesh
        group send (0xC000).  All subscribed nodes respond individually.
        """
        self._poll_generation += 1
        if not self.nodes:
            return
        await self.gateway.send_to_node("ALL", "READ", _silent=True)
        await self._wait_for_responses(timeout=3.0)

    async def _wait_for_responses(self, timeout: float = 3.0):
        """Wait until all responsive nodes report for this poll cycle, or timeout."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.threshold_mw is None:
                return
            all_fresh = all(
                ns.poll_gen == self._poll_generation
                for ns in self.nodes.values()
                if ns.responsive
            )
            if all_fresh:
                return
            await asyncio.sleep(0.1)

    def _mark_stale_nodes(self):
        """Mark nodes that haven't responded recently as unresponsive."""
        now = time.monotonic()
        for ns in self.nodes.values():
            if not ns.node_id.isdigit():
                continue  # Skip phantom nodes like "ALL"
            age = now - ns.last_seen
            if age > self.STALE_TIMEOUT:
                if ns.responsive:
                    self.gateway.log(
                        f"[POWER] Node {ns.node_id} unresponsive ({age:.0f}s)")
                ns.responsive = False

    async def _evaluate_and_adjust(self):
        """Bidirectional equilibrium: nudge nodes toward their power budget share.

        Increases duty when under budget, decreases when over.
        Dead band prevents jitter when close to target.
        """
        if self.threshold_mw is None or self._adjusting:
            self.gateway.log(
                f"[PM] skip: threshold={self.threshold_mw}, adjusting={self._adjusting}",
                _debug=True)
            return

        since = time.monotonic() - self._last_adjustment
        forced = self._force_evaluate
        if not forced and since < self.COOLDOWN:
            self.gateway.log(f"[PM] skip: cooldown {since:.1f}/{self.COOLDOWN}s",
                             _debug=True)
            return
        self._force_evaluate = False  # Clear flag before evaluating

        responsive = {nid: ns for nid, ns in self.nodes.items() if ns.responsive}
        if not responsive:
            self.gateway.log("[PM] skip: no responsive nodes", _debug=True)
            return

        budget = self.threshold_mw - self.HEADROOM_MW
        if budget <= 0:
            self.gateway.log(f"[PM] skip: budget={budget:.0f} (threshold too low)",
                             _debug=True)
            return

        total_power = sum(ns.power for ns in responsive.values())

        # Log per-node state for debugging
        for nid, ns in responsive.items():
            self.gateway.log(
                f"[PM] N{nid}: pwr={ns.power:.0f}mW, "
                f"cmd_duty={ns.commanded_duty}%, tgt_duty={ns.target_duty}%, "
                f"sensor_duty={ns.duty}%", _debug=True)

        if not forced:
            # Dead band: skip if within 5% of budget (prevents constant jitter)
            # Bypassed on forced evals (threshold/priority changes) because
            # individual shares may need rebalancing even when total is fine
            deadband = budget * 0.05
            diff = abs(total_power - budget)
            if diff < deadband:
                self.gateway.log(
                    f"[PM] skip: deadband (total={total_power:.0f}, budget={budget:.0f}, "
                    f"diff={diff:.0f} < band={deadband:.0f})", _debug=True)
                return

            # Skip if all nodes are at their ceiling and under budget
            all_at_ceiling = all(
                (ns.target_duty > 0 and ns.commanded_duty >= ns.target_duty)
                for ns in responsive.values()
            )
            # Also verify commanded duty matches actual sensor duty —
            # a mismatch means the node didn't receive the last command
            all_in_sync = all(
                abs(ns.duty - ns.commanded_duty) <= 2  # 2% tolerance
                for ns in responsive.values()
                if ns.commanded_duty > 0
            )
            if all_at_ceiling and all_in_sync and total_power <= budget:
                self.gateway.log(
                    f"[PM] skip: all at ceiling, in sync & under budget "
                    f"(total={total_power:.0f} <= {budget:.0f})", _debug=True)
                return
            elif all_at_ceiling and not all_in_sync:
                # Nodes think they're at ceiling but actual duty disagrees
                for ns in responsive.values():
                    if ns.commanded_duty > 0 and abs(ns.duty - ns.commanded_duty) > 2:
                        self.gateway.log(
                            f"[PM] N{ns.node_id} out of sync: "
                            f"cmd={ns.commanded_duty}% vs actual={ns.duty}%",
                            _debug=True)
        else:
            self.gateway.log(f"[PM] forced re-evaluation (threshold/priority change)",
                             _debug=True)
            # Reset commanded_duty from actual sensor data to prevent
            # stale values from corrupting mw_per_pct estimates
            for ns in responsive.values():
                if ns.duty > 0:
                    old_cmd = ns.commanded_duty
                    ns.commanded_duty = ns.duty
                    if abs(old_cmd - ns.duty) > 2:
                        self.gateway.log(
                            f"[PM] N{ns.node_id} reset cmd: {old_cmd}% -> {ns.duty}% (from sensor)",
                            _debug=True)

        direction = "▲ UP" if total_power < budget else "▼ DOWN"
        self.gateway.log(
            f"[POWER] {direction}: {total_power:.0f}/{budget:.0f}mW, "
            f"nodes: {list(responsive.keys())}")

        self._adjusting = True
        try:
            if self.priority_node and self.priority_node in responsive:
                await self._balance_with_priority(responsive, budget)
            else:
                await self._balance_proportional(responsive, budget)

            self._last_adjustment = time.monotonic()
        finally:
            self._adjusting = False

    def _estimate_mw_per_pct(self, ns: NodeState, all_nodes: dict) -> float:
        """Estimate milliwatts per duty% for a node.

        Uses commanded_duty (what PM sent) instead of sensor-reported duty
        to avoid oscillation from stale sensor data lagging PM commands.
        """
        duty_value = ns.commanded_duty if ns.commanded_duty > 0 else ns.duty
        if duty_value > 0 and ns.power > 0:
            return ns.power / duty_value
        # Fallback: average from other nodes that have data
        estimates = []
        for n in all_nodes.values():
            d = n.commanded_duty if n.commanded_duty > 0 else n.duty
            if d > 0 and n.power > 0:
                estimates.append(n.power / d)
        if estimates:
            return sum(estimates) / len(estimates)
        return 50.0  # Last resort default

    async def _nudge_node(self, nid: str, ns: NodeState, target_share_mw: float,
                          all_nodes: dict) -> str | None:
        """Nudge a single node's duty toward its target power share.

        Returns a change description string, or None if no change needed.
        Sends the duty command once — retries happen on the next poll cycle
        instead of blocking here (which caused cascading delays).
        """
        mw_per_pct = self._estimate_mw_per_pct(ns, all_nodes)
        ideal_duty = target_share_mw / mw_per_pct

        # Clamp to [0, target_duty] — never exceed user's original setting
        ceiling = ns.target_duty if ns.target_duty > 0 else 100
        ideal_duty_clamped = max(0, min(ceiling, ideal_duty))

        current = ns.commanded_duty if ns.commanded_duty > 0 else ns.duty
        new_duty = round(ideal_duty_clamped)

        self.gateway.log(
            f"[PM] nudge N{nid}: share={target_share_mw:.0f}mW, "
            f"mw/pct={mw_per_pct:.1f}, ideal={ideal_duty:.1f}%, "
            f"ceiling={ceiling}%, clamped={new_duty}%, current={current}%",
            _debug=True)

        if new_duty == current:
            return None

        new_duty = max(0, min(100, new_duty))
        if new_duty == current:
            return None

        change = f"N{nid}:{current}->{new_duty}%"
        await self.gateway.set_duty(nid, new_duty, _from_power_mgr=True, _silent=True)
        confirmed = await self.gateway._wait_node_response(nid)
        if confirmed:
            ns.commanded_duty = new_duty
        else:
            self.gateway.log(
                f"[PM] N{nid} did not confirm duty:{new_duty}%, "
                f"keeping cmd={current}%", _debug=True)
            # Don't update commanded_duty — node may not have received it.
            # Next poll cycle will re-evaluate with accurate data.
        return change

    async def _balance_proportional(self, nodes: dict, budget: float):
        """Equal power shares: each node gets budget/N."""
        n = len(nodes)
        share_mw = budget / n

        changes = []
        for nid, ns in sorted(nodes.items(), key=lambda x: int(x[0]) if x[0].isdigit() else 999):
            change = await self._nudge_node(nid, ns, share_mw, nodes)
            if change:
                changes.append(change)

        total_power = sum(ns.power for ns in nodes.values())
        if changes:
            self.gateway.log(
                f"[POWER] Balancing {total_power:.0f}/{budget:.0f}mW "
                f"(share:{share_mw:.0f}mW each) — {', '.join(changes)}")

    async def _balance_with_priority(self, nodes: dict, budget: float):
        """Weighted power shares: priority node gets PRIORITY_WEIGHT x normal share."""
        priority_ns = nodes[self.priority_node]
        non_priority = {nid: ns for nid, ns in nodes.items()
                        if nid != self.priority_node}

        # Calculate weighted shares
        total_shares = self.PRIORITY_WEIGHT + len(non_priority)
        priority_budget = budget * (self.PRIORITY_WEIGHT / total_shares)

        # If priority can't use its full share (limited by target_duty), redistribute
        pri_mw_per_pct = self._estimate_mw_per_pct(priority_ns, nodes)
        pri_ceiling = priority_ns.target_duty if priority_ns.target_duty > 0 else 100
        pri_max_power = pri_ceiling * pri_mw_per_pct
        if pri_max_power < priority_budget and non_priority:
            # Priority can't fill its share — surplus goes to non-priority
            priority_budget = pri_max_power
            remaining = budget - priority_budget
        else:
            remaining = budget - priority_budget

        non_pri_share = remaining / len(non_priority) if non_priority else 0

        changes = []
        # Nudge priority node
        change = await self._nudge_node(self.priority_node, priority_ns,
                                        priority_budget, nodes)
        if change:
            changes.append(change + "(pri)")

        # Nudge non-priority nodes
        for nid, ns in sorted(non_priority.items(), key=lambda x: int(x[0]) if x[0].isdigit() else 999):
            change = await self._nudge_node(nid, ns, non_pri_share, nodes)
            if change:
                changes.append(change)

        total_power = sum(ns.power for ns in nodes.values())
        if changes:
            self.gateway.log(
                f"[POWER] Balancing {total_power:.0f}/{budget:.0f}mW "
                f"(pri:{priority_budget:.0f}mW, others:{non_pri_share:.0f}mW each) "
                f"— {', '.join(changes)}")
