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
    """Textual TUI for the BLE Mesh Gateway."""

    TITLE = "DC Monitor Mesh Gateway"

    CSS = """
    #sidebar {
        width: 26;
        dock: left;
        border-right: solid $accent;
        padding: 1;
        background: $surface;
    }
    #log {
        height: 1fr;
        border: solid $primary;
    }
    #nodes-table {
        height: auto;
        max-height: 10;
        border: solid $primary;
    }
    #cmd-input {
        dock: bottom;
    }
    """

    BINDINGS = [
        ("f2", "toggle_debug", "Debug"),
        ("f3", "clear_log", "Clear"),
        ("escape", "focus_input", "Input"),
    ]

    # ---- Custom Messages ----

    class SensorDataMsg(Message):
        """Sensor data arrived from a mesh node."""
        def __init__(self, node_id: str, duty: int, voltage: float,
                     current: float, power: float, raw: str):
            super().__init__()
            self.node_id = node_id
            self.duty = duty
            self.voltage = voltage
            self.current = current
            self.power = power
            self.raw = raw

    class LogMsg(Message):
        """Generic log line for the RichLog panel."""
        def __init__(self, text: str, style: str = ""):
            super().__init__()
            self.text = text
            self.style = style

    class PowerAdjustMsg(Message):
        """PowerManager made an adjustment."""
        def __init__(self, summary: str):
            super().__init__()
            self.summary = summary

    # ---- Init ----

    def __init__(self, gateway: DCMonitorGateway, target_address: str = None,
                 default_node: str = "0", scan_timeout: float = 10.0):
        super().__init__()
        self.gateway = gateway
        self.gateway.app = self  # Back-reference for callbacks
        self.gateway.target_node = default_node
        self.target_address = target_address
        self.scan_timeout = scan_timeout
        self.debug_mode = False
        self._connected = False
        self._ble_thread = BleThread()
        self.gateway.ble_thread = self._ble_thread

    # ---- Layout ----

    def compose(self) -> ComposeResult:
        yield Header()
        with Horizontal():
            yield Static("Connecting...", id="sidebar")
            yield RichLog(id="log", wrap=True, highlight=True, markup=True)
        yield DataTable(id="nodes-table")
        yield Input(placeholder="Enter command (type 'help' for list)", id="cmd-input")
        yield Footer()

    def on_mount(self) -> None:
        """Initialize table and start BLE connection."""
        table = self.query_one("#nodes-table", DataTable)
        table.add_columns("ID", "Duty", "Target", "Voltage", "Current", "Power", "Status")
        table.cursor_type = "none"
        # Focus the input
        self.query_one("#cmd-input", Input).focus()
        # Start BLE I/O thread before any BLE operations
        self._ble_thread.start()
        # Start BLE connection
        self.connect_ble()

    # ---- BLE Connection Worker ----

    @work(exclusive=True, group="ble_connect")
    async def connect_ble(self) -> None:
        """Scan and connect to BLE gateway via BLE thread."""
        gw = self.gateway
        bt = self._ble_thread

        devices = await bt.submit_async(
            gw.scan_for_nodes(timeout=self.scan_timeout, target_address=self.target_address)
        )

        if not devices:
            self.log_message("No gateways found. Restart to try again.", style="bold red")
            return

        # Build ordered device list: target address first (if given), then the rest
        if self.target_address:
            target_dev = next(
                (d for d in devices if d.address.upper() == self.target_address.upper()),
                None,
            )
            ordered = ([target_dev] if target_dev else []) + [
                d for d in devices if d != target_dev
            ]
        else:
            ordered = list(devices)

        # Try each device until we find one with the GATT service (subscribe succeeds)
        connected_device = None
        for device in ordered:
            success = await bt.submit_async(gw.connect_to_node(device))
            if success:
                connected_device = device
                break

        if connected_device:
            # Derive sensing node count from the BLE scan:
            # total mesh devices found - 1 (the GATT gateway we just connected to)
            gw.sensing_node_count = max(0, len(devices) - 1)
            self.log_message(
                f"Connected to {connected_device.name or connected_device.address} "
                f"({gw.sensing_node_count} sensing node(s) in mesh)",
                style="bold green")
            self._connected = True
            self.update_status()
            # Start auto-reconnect monitor (v0.7.0 Phase 1)
            self._ble_thread.submit(self.gateway._auto_reconnect_loop())
        else:
            self.log_message(
                "No device with GATT service found. Use --address to specify.",
                style="bold red")

    # ---- Command Handling ----

    @on(Input.Submitted, "#cmd-input")
    def on_cmd_submitted(self, event: Input.Submitted) -> None:
        """Handle command input."""
        cmd = event.value.strip()
        event.input.value = ""
        if cmd:
            self.log_message(f"> {cmd}", style="bold cyan")
            self.dispatch_command(cmd.lower())

    @work(exclusive=True, group="cmd")
    async def dispatch_command(self, cmd: str) -> None:
        """Parse and execute a user command via BLE thread."""
        gw = self.gateway
        bt = self._ble_thread
        try:
            if cmd in ['q', 'quit', 'exit']:
                if gw._power_manager:
                    await bt.submit_async(gw._power_manager.disable())
                await bt.submit_async(gw.disconnect())
                bt.stop()
                self.exit()

            elif cmd.startswith('node'):
                parts = cmd.split(None, 1)
                if len(parts) < 2:
                    self.log_message("Usage: node <0-9 or ALL>")
                    return
                new_node = parts[1].strip().upper()
                if new_node == 'ALL' or (new_node.isdigit() and 0 <= int(new_node) <= 9):
                    gw.target_node = new_node.lower() if new_node != 'ALL' else 'ALL'
                    self.log_message(f"Target node: {gw.target_node}")
                else:
                    self.log_message("Invalid node ID (use 0-9 or ALL)")

            elif cmd in ['s', 'stop']:
                was_monitoring = gw._monitoring
                await bt.submit_async(gw.stop_node(gw.target_node))
                if was_monitoring:
                    self.log_message("Monitoring stopped")

            elif cmd in ['r', 'ramp']:
                await bt.submit_async(gw.start_ramp(gw.target_node))

            elif cmd == 'status':
                await bt.submit_async(gw.read_status(gw.target_node))

            elif cmd == 'read':
                await bt.submit_async(gw.read_sensor(gw.target_node))

            elif cmd in ['m', 'monitor']:
                await bt.submit_async(gw.start_monitor(gw.target_node))

            elif cmd.startswith('duty'):
                parts = cmd.split(None, 1)
                if len(parts) < 2:
                    self.log_message("Usage: duty <0-100>")
                    return
                val = int(parts[1])
                if val < 0 or val > 100:
                    self.log_message(f"Note: duty clamped to {max(0, min(100, val))}%")
                await bt.submit_async(gw.set_duty(gw.target_node, val))

            elif cmd.isdigit():
                await bt.submit_async(gw.set_duty(gw.target_node, int(cmd)))

            elif cmd.startswith('raw'):
                parts = cmd.split(None, 1)
                if len(parts) < 2:
                    self.log_message("Usage: raw <command>")
                    return
                await bt.submit_async(gw.send_command(parts[1].upper()))

            elif cmd.startswith('threshold'):
                parts = cmd.split(None, 1)
                if len(parts) < 2:
                    self.log_message("Usage: threshold <mW> or threshold off")
                    return
                arg = parts[1].strip()
                if arg == 'off':
                    if gw._power_manager:
                        await bt.submit_async(gw._power_manager.disable())
                        self.workers.cancel_group(self, "power_poll")
                        self.notify("Threshold disabled", severity="information")
                else:
                    mw = float(arg)
                    if not gw._power_manager:
                        gw._power_manager = PowerManager(gw)
                    gw._power_manager.set_threshold(mw)
                    self.start_power_poll()
                    self.notify(f"Threshold: {mw:.0f} mW", severity="information")

            elif cmd.startswith('priority'):
                parts = cmd.split(None, 1)
                if len(parts) < 2:
                    self.log_message("Usage: priority <node_id> or priority off")
                    return
                arg = parts[1].strip()
                if arg == 'off':
                    if gw._power_manager:
                        gw._power_manager.clear_priority()
                        self.notify("Priority cleared", severity="information")
                elif gw._power_manager:
                    if not (arg.isdigit() and 0 <= int(arg) <= 9):
                        self.log_message(f"Warning: '{arg}' may not be a valid node ID (expected 0-9)")
                    gw._power_manager.set_priority(arg)
                    self.notify(f"Priority: node {arg}", severity="information")
                else:
                    self.log_message("Set a threshold first")

            elif cmd == 'power':
                if gw._power_manager:
                    self.log_message(gw._power_manager.status())
                else:
                    self.log_message("Power management not active. Use: threshold <mW>")

            elif cmd in ['d', 'debug']:
                self.action_toggle_debug()

            elif cmd in ['clear', 'cls']:
                self.action_clear_log()

            elif cmd == 'help':
                self._show_help()

            else:
                self.log_message("Unknown command. Type 'help' for list.")

        except (ValueError, IndexError):
            self.log_message("Invalid value or missing argument")
        except Exception as e:
            self.log_message(f"Error: {e}", style="bold red")

        self.update_status()

    # ---- Power Poll Worker ----

    @work(exclusive=True, group="power_poll")
    async def start_power_poll(self) -> None:
        """Run PowerManager poll loop on the BLE thread."""
        pm = self.gateway._power_manager
        if pm:
            future = self._ble_thread.submit(pm.poll_loop())
            try:
                await asyncio.wrap_future(future)
            except asyncio.CancelledError:
                future.cancel()
                raise

    # ---- Message Handlers ----
    # Textual auto-discovers handlers named on_<namespace>_<message_name>
    # where namespace = snake_case of outermost widget class.

    def on_mesh_gateway_app_sensor_data_msg(self, msg: SensorDataMsg) -> None:
        """Handle incoming sensor data — update table and optionally log."""
        self._update_node_table(msg)
        self.update_status()

        # Show in log unless it's a background PM poll
        pm = self.gateway._power_manager
        is_bg_poll = pm and pm._polling and pm.threshold_mw is not None
        if not is_bg_poll or self.debug_mode:
            log = self.query_one("#log", RichLog)
            log.write(msg.raw)

    def on_mesh_gateway_app_log_msg(self, msg: LogMsg) -> None:
        """Handle generic log messages."""
        log = self.query_one("#log", RichLog)
        if msg.style:
            log.write(f"[{msg.style}]{msg.text}[/{msg.style}]")
        else:
            log.write(msg.text)

    def on_mesh_gateway_app_power_adjust_msg(self, msg: PowerAdjustMsg) -> None:
        """Handle power adjustment notification."""
        self.update_status()

    # ---- UI Updates ----

    def _update_node_table(self, msg: SensorDataMsg) -> None:
        """Update or insert a row in the nodes DataTable."""
        table = self.query_one("#nodes-table", DataTable)
        pm = self.gateway._power_manager
        row_key = f"node_{msg.node_id}"

        # Get target duty
        if pm and msg.node_id in pm.nodes:
            target = pm.nodes[msg.node_id].target_duty
        else:
            target = msg.duty

        # Get responsive status
        if pm and msg.node_id in pm.nodes:
            status_icon = "ok" if pm.nodes[msg.node_id].responsive else "STALE"
        else:
            status_icon = "ok"

        row_data = [
            msg.node_id,
            f"{msg.duty}%",
            f"{target}%",
            f"{msg.voltage:.2f}V",
            f"{msg.current:.1f}mA",
            f"{msg.power:.0f}mW",
            status_icon,
        ]

        # Try to update existing row, add if not found
        if row_key in table.rows:
            col_keys = list(table.columns.keys())
            for col_idx, val in enumerate(row_data):
                table.update_cell(row_key, col_keys[col_idx], val)
        else:
            table.add_row(*row_data, key=row_key)

    def update_status(self) -> None:
        """Refresh the sidebar with current state."""
        gw = self.gateway
        pm = gw._power_manager
        lines = ["[bold]Status[/bold]", ""]

        # Connection
        if self._connected:
            name = gw.connected_device.name if gw.connected_device else "?"
            lines.append(f"[green]Connected[/green]")
            lines.append(f"{name}")
        else:
            lines.append("[yellow]Connecting...[/yellow]")

        lines.append(f"\nTarget: [bold]{gw.target_node}[/bold]")

        # Monitoring
        if gw._monitoring:
            lines.append("[cyan]Monitoring ●[/cyan]")

        # Power management
        lines.append("")
        if pm and pm.threshold_mw is not None:
            budget = pm.threshold_mw - pm.HEADROOM_MW
            lines.append("[bold]Power Mgmt[/bold]")
            lines.append(f"Threshold: {pm.threshold_mw:.0f}mW")
            lines.append(f"Budget:    {budget:.0f}mW")
            if pm.priority_node:
                lines.append(f"Priority:  N{pm.priority_node}")
            else:
                lines.append("Priority:  none")

            total = sum(ns.power for ns in pm.nodes.values() if ns.responsive)
            headroom = pm.threshold_mw - total
            lines.append(f"\nTotal: {total:.0f}mW")
            if headroom >= pm.HEADROOM_MW:
                lines.append(f"Headroom: [green]{headroom:.0f}mW[/green]")
            elif headroom >= 0:
                lines.append(f"Headroom: [yellow]{headroom:.0f}mW[/yellow]")
            else:
                lines.append(f"Headroom: [red]{headroom:.0f}mW[/red]")

            # Node count
            responsive = sum(1 for ns in pm.nodes.values() if ns.responsive)
            total_nodes = len(pm.nodes)
            lines.append(f"Nodes: {responsive}/{total_nodes}")
        else:
            lines.append("[dim]Power: OFF[/dim]")

        # Debug mode
        if self.debug_mode:
            lines.append("\n[yellow]DEBUG ON[/yellow]")

        try:
            self.query_one("#sidebar", Static).update("\n".join(lines))
        except Exception:
            pass

    def _show_help(self):
        """Display help text in the log."""
        help_text = (
            "[bold]--- Commands ---[/bold]\n"
            "  node <id>      Switch target (0-9 or ALL)\n"
            "  ramp / r       Send RAMP to target node\n"
            "  stop / s       Send STOP to target node\n"
            "  duty <0-100>   Set duty cycle on target node\n"
            "  status         Get status from target node\n"
            "  read           Single sensor reading\n"
            "  monitor / m    Start continuous monitoring\n"
            "  raw <cmd>      Send raw command string\n"
            "\n"
            "[bold]--- Power Management ---[/bold]\n"
            "  threshold <mW> Set total power limit\n"
            "  priority <id>  Set priority node\n"
            "  threshold off  Disable power management\n"
            "  priority off   Clear priority node\n"
            "  power          Show power manager status\n"
            "\n"
            "[bold]--- Keys / Misc ---[/bold]\n"
            "  debug / d      Toggle debug mode (or F2)\n"
            "  clear / cls    Clear log (or F3)\n"
            "  Esc            Focus input\n"
            "  q / quit       Quit"
        )
        log = self.query_one("#log", RichLog)
        log.write(help_text)

    # ---- Actions ----

    def action_toggle_debug(self) -> None:
        """Toggle debug mode."""
        self.debug_mode = not self.debug_mode
        self.notify(f"Debug: {'ON' if self.debug_mode else 'OFF'}")
        self.update_status()

    def action_clear_log(self) -> None:
        """Clear the log panel."""
        self.query_one("#log", RichLog).clear()

    def action_focus_input(self) -> None:
        """Focus the command input."""
        self.query_one("#cmd-input", Input).focus()

    def log_message(self, text: str, style: str = ""):
        """Convenience: post a LogMsg."""
        self.post_message(self.LogMsg(text, style))

    def on_unmount(self) -> None:
        """Clean up BLE thread when app exits."""
        # Signal reconnect loop to stop
        self.gateway.running = False
        if self._ble_thread:
            try:
                if self.gateway.client and self.gateway.client.is_connected:
                    f = self._ble_thread.submit(self.gateway.disconnect())
                    f.result(timeout=3.0)
            except Exception:
                pass
            self._ble_thread.stop()
