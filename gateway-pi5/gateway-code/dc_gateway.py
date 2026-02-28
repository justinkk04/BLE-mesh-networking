"""BLE GATT gateway client for DC Monitor Mesh Network.

Handles BLE scanning, connection, GATT read/write, notification parsing,
and command routing to mesh nodes via the ESP32-C6 GATT gateway.
"""

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
    def __init__(self):
        self.client = None
        self.connected_device = None
        self.running = True
        self.target_node = "0"
        self._chunk_buf = ""  # Buffer for reassembling chunked notifications
        self._power_manager = None  # PowerManager instance
        self._monitoring = False  # True when monitor mode is active
        self.app = None  # Reference to TUI app (set by MeshGatewayApp)
        self.ble_thread = None  # BleThread instance (set by TUI app)
        self._node_events: dict[str, threading.Event] = {}  # Signaled when node responds
        self.known_nodes: set[str] = set()  # Node IDs that have actually responded with sensor data
        self.sensing_node_count = 0  # Set from BLE scan: total_mesh_devices - 1 (GATT gateway)
        # Reconnection state (v0.7.0 Phase 1)
        self._was_connected = False
        self._reconnecting = False
        self._last_connected_address = None
        self._web_enabled = False  # Set True by gateway.py when --web is used
        self._last_readings = {}  # {node_id: {duty, voltage, current, power, last_seen}}

    def log(self, text: str, style: str = "", _from_thread: bool = False,
            _debug: bool = False):
        """Post a log message to the TUI, or print() if no TUI.

        Args:
            _from_thread: Set True when calling from a non-Textual thread
                         (e.g. bleak notification callback). Uses call_from_thread
                         for safe cross-thread posting.
            _debug: If True, only show when debug_mode is on (F2 / 'debug').
        """
        if _debug:
            if self.app and _HAS_TEXTUAL:
                if not getattr(self.app, 'debug_mode', False):
                    return
            else:
                return  # CLI: suppress debug logs
        if self.app and _HAS_TEXTUAL:
            try:
                msg = self.app.LogMsg(text, style)
                if _from_thread:
                    self.app.call_from_thread(self.app.post_message, msg)
                else:
                    self.app.post_message(msg)
            except Exception as e:
                print(f"  {text}  [log error: {e}]")
        else:
            print(f"  {text}")

        # Web console streaming (skip debug messages to reduce noise)
        if self._web_enabled and not _debug:
            try:
                import web_server
                loop = self.ble_thread._loop if self.ble_thread else None
                if loop:
                    asyncio.run_coroutine_threadsafe(
                        web_server.broadcast_log(text), loop)
            except Exception:
                pass

    async def scan_for_nodes(self, timeout=10.0, target_address=None):
        """Scan for DC Monitor gateway nodes.

        If target_address is given, skip name/UUID matching and just find that device.
        Otherwise, match by name prefix or service UUID.
        """
        self.log(f"Scanning for BLE devices ({timeout}s)...")

        devices = await BleakScanner.discover(timeout=timeout, return_adv=True)

        nodes = []
        for address, (device, adv_data) in devices.items():
            # If a specific address was requested, match it directly
            if target_address and device.address.upper() == target_address.upper():
                nodes.append(device)
                self.log(f"Found target: {device.name or '(no name)'} [{device.address}]")
                continue

            # Match by known name prefixes
            if device.name and any(p in device.name for p in DEVICE_NAME_PREFIXES):
                nodes.append(device)
                self.log(f"Found: {device.name} [{device.address}]")
            # Match by service UUID (pre-provisioning)
            elif adv_data.service_uuids:
                if DC_MONITOR_SERVICE_UUID.lower() in [str(u).lower() for u in adv_data.service_uuids]:
                    nodes.append(device)
                    self.log(f"Found: {device.name or 'Unknown'} [{device.address}] (by service UUID)")

        if not nodes:
            self.log("No DC Monitor gateways found")
            self.log("Tip: Make sure ESP32-C6 is powered and advertising")

        return nodes

    def notification_handler(self, characteristic: BleakGATTCharacteristic, data: bytearray):
        """Handle incoming notifications from GATT gateway.

        IMPORTANT: This runs on bleak's callback thread, NOT the Textual event loop.
        All UI updates must use call_from_thread() or log(_from_thread=True).

        Messages > 20 bytes are chunked by the gateway:
          - Continuation chunks start with '+' (data follows after the '+')
          - Final (or only) chunk has no '+' prefix
        We accumulate '+' chunks and process the full message on the final chunk.
        """
        decoded = data.decode('utf-8', errors='replace').strip()

        # Chunked reassembly: '+' prefix means more data follows
        if decoded.startswith('+'):
            self._chunk_buf += decoded[1:]  # Accumulate without the '+' prefix
            return  # Wait for final chunk

        # Final (or only) chunk - combine with any buffered data
        if self._chunk_buf:
            decoded = self._chunk_buf + decoded
            self._chunk_buf = ""

        timestamp = datetime.now().strftime("%H:%M:%S")

        # Parse vendor model responses: NODE<id>:DATA:<sensor payload>
        if ":DATA:" in decoded:
            parts = decoded.split(":DATA:", 1)
            node_tag = parts[0]  # e.g. "NODE0"
            payload = parts[1]   # e.g. "D:50%,V:12.345V,I:1234.5MA,P:15234.5MW"

            # Parse sensor values
            sensor_match = SENSOR_RE.match(payload)
            node_match = NODE_ID_RE.match(node_tag)

            if sensor_match and node_match:
                node_id = node_match.group(1)
                duty = int(sensor_match.group(1))
                voltage = float(sensor_match.group(2))
                current = float(sensor_match.group(3))
                power = float(sensor_match.group(4))

                # Track this node as known (it actually exists and responded)
                self.known_nodes.add(node_id)

                # Store latest reading for web API (independent of PM)
                self._last_readings[node_id] = {
                    "duty": duty, "voltage": voltage,
                    "current": current, "power": power,
                    "last_seen": time.time(),
                }

                # Feed PowerManager
                if self._power_manager:
                    self._power_manager.on_sensor_data(
                        node_id, duty, voltage, current, power)

                # Signal that this node responded (unblocks event-driven pacing)
                evt = self._node_events.get(node_id)
                if evt:
                    evt.set()

                # Web dashboard: broadcast sensor data + record to DB
                if self._web_enabled:
                    try:
                        import web_server
                        import db
                        loop = self.ble_thread._loop if self.ble_thread else None
                        if loop:
                            asyncio.run_coroutine_threadsafe(
                                web_server.broadcast_sensor_data(node_id, {
                                    "duty": duty, "voltage": voltage,
                                    "current": current, "power": power,
                                    "last_seen": time.time(),
                                }),
                                loop
                            )
                        db.insert_reading(node_id, duty, voltage, current, power)
                    except Exception:
                        pass

                # Post to TUI for UI update (always use call_from_thread — we're on bleak's thread)
                if self.app and _HAS_TEXTUAL:
                    try:
                        msg = self.app.SensorDataMsg(
                            node_id, duty, voltage, current, power,
                            f"[{timestamp}] {node_tag} >> {payload}"
                        )
                        self.app.call_from_thread(self.app.post_message, msg)
                    except Exception as e:
                        print(f"  [{timestamp}] {node_tag} >> {payload}  [post error: {e}]")
                else:
                    print(f"[{timestamp}] {node_tag} >> {payload}")
            else:
                self.log(f"[{timestamp}] {node_tag} >> {payload}", _from_thread=True)

        elif decoded.startswith("ERROR:"):
            # Suppress MESH_TIMEOUT during PM polling — it's just discovery probes
            pm = self._power_manager
            if pm and pm._polling:
                pass  # Swallow errors during background polling (reduces TUI noise)
            else:
                self.log(f"[{timestamp}] !! {decoded}", style="bold red", _from_thread=True)
        elif decoded.startswith("SENT:"):
            # Only show in debug mode
            if self.app and _HAS_TEXTUAL:
                if self.app.debug_mode:
                    self.log(f"[{timestamp}] -> {decoded}", style="dim", _from_thread=True)
            else:
                print(f"[{timestamp}] -> {decoded}")
        elif decoded.startswith("MESH_READY"):
            self.log(f"[{timestamp}] {decoded}", _from_thread=True)
        elif decoded.startswith("TIMEOUT:"):
            pm = self._power_manager
            if pm and pm._polling:
                pass  # Swallow timeouts during background polling
            else:
                self.log(f"[{timestamp}] !! {decoded}", style="yellow", _from_thread=True)
        else:
            self.log(f"[{timestamp}] {decoded}", _from_thread=True)

    async def connect_to_node(self, device):
        """Connect to a specific node and subscribe to notifications"""
        self.log(f"Connecting to {device.name or device.address}...")

        self.client = BleakClient(device.address, dangerous_use_bleak_cache=False)
        try:
            await self.client.connect()
        except Exception as e:
            self.log(f"Connection failed: {e}")
            self.client = None
            return False

        if not self.client.is_connected:
            self.log("Connection failed")
            self.client = None
            return False

        self.connected_device = device

        try:
            await self.client.start_notify(SENSOR_DATA_CHAR_UUID, self.notification_handler)
            self.log("Subscribed to sensor notifications")
        except Exception as e:
            self.log(f"Could not subscribe: {e} — skipping this device")
            # This device doesn't have the DC01 GATT service (e.g. relay/sensor node)
            try:
                await self.client.disconnect()
            except Exception:
                pass
            self.client = None
            self.connected_device = None
            return False

        # Report negotiated MTU
        mtu = self.client.mtu_size
        self.log(f"MTU: {mtu}")

        self._was_connected = True
        self._last_connected_address = device.address

        # Web broadcast: connection established
        if self._web_enabled:
            try:
                import web_server
                loop = self.ble_thread._loop if self.ble_thread else None
                if loop:
                    asyncio.run_coroutine_threadsafe(
                        web_server.broadcast_state_change("connected", {
                            "device_name": getattr(device, 'name', None),
                            "device_address": device.address,
                        }),
                        loop
                    )
            except Exception:
                pass

        return True

    async def disconnect(self):
        """Disconnect from current node"""
        if self.client and self.client.is_connected:
            try:
                await self.client.disconnect()
            except (EOFError, Exception) as e:
                # BlueZ/dbus can throw EOFError if connection already dropped
                pass
            self.log("Disconnected")
        self._chunk_buf = ""  # Clear stale partial data on disconnect

        # Web broadcast: disconnected
        if self._web_enabled:
            try:
                import web_server
                loop = self.ble_thread._loop if self.ble_thread else None
                if loop:
                    asyncio.run_coroutine_threadsafe(
                        web_server.broadcast_state_change("disconnected"),
                        loop
                    )
            except Exception:
                pass

    async def send_command(self, cmd: str, _silent: bool = False):
        """Send raw command string to GATT gateway"""
        if self._reconnecting:
            if not _silent:
                self.log("[WARN] Cannot send — reconnecting...", style="yellow")
            return False
        if not self.client or not self.client.is_connected:
            if not _silent:
                self.log("[WARN] Not connected", style="yellow")
            return False

        try:
            await self.client.write_gatt_char(COMMAND_CHAR_UUID, cmd.encode('utf-8'))
            if not _silent:
                self.log(f"Sent: {cmd}")
            return True
        except Exception as e:
            if not _silent:
                self.log(f"Failed to send command: {e}")
            return False

    async def _wait_node_response(self, node_id: str, timeout: float = 5.0):
        """Wait until a specific node responds, then return immediately.

        Uses a threading.Event signaled by notification_handler (bleak thread)
        and polled from the asyncio loop. Falls back to timeout.
        """
        evt = threading.Event()
        self._node_events[node_id] = evt
        try:
            deadline = time.monotonic() + timeout
            while not evt.is_set() and time.monotonic() < deadline:
                await asyncio.sleep(0.1)  # Check every 100ms
            return evt.is_set()
        finally:
            self._node_events.pop(node_id, None)

    async def send_to_node(self, node: str, command: str, value: str = None,
                           _silent: bool = False):
        """Send command to a specific mesh node.

        When node is 'ALL', sends a single ALL:COMMAND which the GATT
        gateway translates to a BLE Mesh group send (0xC000).  All
        subscribed nodes receive it simultaneously — O(1) instead of O(N).

        Args:
            node: Node ID (0-9) or "ALL"
            command: RAMP, STOP, ON, OFF, DUTY, STATUS, READ
            value: Optional value (e.g. duty percentage)
        """
        if str(node).upper() == "ALL":
            if value is not None:
                cmd = f"ALL:{command}:{value}"
            else:
                cmd = f"ALL:{command}"
            return await self.send_command(cmd, _silent=_silent)

        if value is not None:
            cmd = f"{node}:{command}:{value}"
        else:
            cmd = f"{node}:{command}"
        return await self.send_command(cmd, _silent=_silent)

    async def set_duty(self, node: str, percent: int, _from_power_mgr: bool = False,
                       _silent: bool = False):
        """Set duty cycle (0-100%) on a mesh node"""
        percent = max(0, min(100, percent))
        if self._power_manager and not _from_power_mgr:
            if str(node).upper() == "ALL":
                # Track target for ALL known nodes
                pm = self._power_manager
                if pm.nodes:
                    for nid in pm.nodes:
                        pm.set_target_duty(nid, percent)
                elif self.known_nodes:
                    for nid in self.known_nodes:
                        pm.set_target_duty(nid, percent)
                # else: no nodes known yet, target will be set when they respond
            else:
                self._power_manager.set_target_duty(str(node), percent)
        return await self.send_to_node(node, "DUTY", str(percent), _silent=_silent)

    async def start_ramp(self, node: str):
        """Start ramp test on a mesh node"""
        return await self.send_to_node(node, "RAMP")

    async def stop_node(self, node: str):
        """Stop load on a mesh node. Also stops monitoring if active."""
        self._monitoring = False
        return await self.send_to_node(node, "STOP")

    async def read_status(self, node: str):
        """Request current status from a mesh node"""
        return await self.send_to_node(node, "STATUS")

    async def read_sensor(self, node: str):
        """Request single sensor reading from a mesh node"""
        return await self.send_to_node(node, "READ")

    async def start_monitor(self, node: str):
        """Start continuous monitoring on a mesh node"""
        self._monitoring = True
        return await self.send_to_node(node, "MONITOR")

    # ---- Auto-Reconnect (v0.7.0 Phase 1) ----

    async def _auto_reconnect_loop(self):
        """Monitor BLE connection health and auto-reconnect on disconnect.

        Runs as a background task on the BLE thread's event loop.
        Checks connection every 2 seconds. On disconnect:
        1. Logs the event
        2. Pauses PM polling
        3. Rescans for the gateway
        4. Reconnects and resubscribes
        5. Resumes PM polling
        """
        while self.running:
            await asyncio.sleep(2.0)

            if self.client is None or not self.client.is_connected:
                if self._was_connected:
                    self.log("[RECONNECT] Connection lost! Attempting reconnect...",
                             style="bold red", _from_thread=True)
                    self._was_connected = False
                    self._reconnecting = True

                    # Web broadcast: connection lost, attempting reconnect
                    if self._web_enabled:
                        try:
                            import web_server
                            loop = self.ble_thread._loop if self.ble_thread else None
                            if loop:
                                asyncio.run_coroutine_threadsafe(
                                    web_server.broadcast_state_change("reconnecting"),
                                    loop
                                )
                        except Exception:
                            pass

                    # Pause PM if active
                    pm = self._power_manager
                    if pm and pm.threshold_mw is not None:
                        pm._paused = True
                        self.log("[RECONNECT] PowerManager paused", _from_thread=True)

                    # Clean up old client
                    self.client = None
                    self.connected_device = None

                if not self._reconnecting:
                    continue

                # Attempt failover: scan for ALL available nodes, try each
                try:
                    dead_address = self._last_connected_address
                    devices = await self.scan_for_nodes(timeout=5.0)

                    if devices:
                        # Try each device (skip the dead one first pass)
                        connected = False
                        for device in devices:
                            if device.address == dead_address:
                                continue  # Skip the node that just died
                            success = await self.connect_to_node(device)
                            if success:
                                self.log(
                                    f"[FAILOVER] Connected to {device.name or device.address}",
                                    style="bold green", _from_thread=True)
                                self._was_connected = True
                                self._reconnecting = False
                                self._last_connected_address = device.address

                                # Resume PM
                                pm = self._power_manager
                                if pm and pm._paused:
                                    pm._paused = False
                                    self.log("[FAILOVER] PowerManager resumed",
                                             _from_thread=True)
                                connected = True
                                break

                        # If all others failed, try the original address too
                        if not connected and dead_address:
                            for device in devices:
                                if device.address == dead_address:
                                    success = await self.connect_to_node(device)
                                    if success:
                                        self.log(
                                            "[RECONNECT] Reconnected to original node",
                                            style="bold green", _from_thread=True)
                                        self._was_connected = True
                                        self._reconnecting = False

                                        pm = self._power_manager
                                        if pm and pm._paused:
                                            pm._paused = False
                                            self.log("[RECONNECT] PowerManager resumed",
                                                     _from_thread=True)
                                        connected = True
                                        break

                        if not connected:
                            self.log("[FAILOVER] No node available, retrying in 5s...",
                                     _from_thread=True)
                    else:
                        self.log("[FAILOVER] No nodes found, retrying in 5s...",
                                 _from_thread=True)
                except Exception as e:
                    self.log(f"[FAILOVER] Error: {e}, retrying in 5s...",
                             _from_thread=True)

    # ---- Legacy plain CLI interactive mode (--no-tui) ----

    async def interactive_mode(self, default_node: str = "0"):
        """Interactive command mode with mesh node targeting (plain CLI)"""
        self.target_node = default_node

        print("\n" + "=" * 50)
        print("  Mesh Gateway - Interactive Mode (Plain CLI)")
        print("=" * 50)
        print(f"  Target node: {self.target_node}")
        print()
        print("Commands:")
        print("  node <id>    Switch target (0-9 or ALL)")
        print("  ramp / r     Send RAMP to target node")
        print("  stop / s     Send STOP to target node")
        print("  duty <0-100> Set duty cycle on target node")
        print("  status       Get status from target node")
        print("  read         Single sensor reading from node")
        print("  monitor / m  Start continuous monitoring")
        print("  raw <cmd>    Send raw command string")
        print("Power Management:")
        print("  threshold <mW>  Set total power limit (auto-manages duty)")
        print("  priority <id>   Set priority node (preserved during reduction)")
        print("  threshold off   Disable power management")
        print("  priority off    Clear priority node")
        print("  power           Show power manager status")
        print()
        print("  q/quit       Exit")
        print("=" * 50)
        print()

        while self.running and self.client.is_connected:
            try:
                prompt = f"[node {self.target_node}]> "
                cmd = await asyncio.get_event_loop().run_in_executor(
                    None, lambda: input(prompt).strip().lower()
                )

                if not cmd:
                    continue
                elif cmd in ['q', 'quit', 'exit']:
                    break
                elif cmd.startswith('node'):
                    parts = cmd.split(None, 1)
                    if len(parts) < 2:
                        print("  Usage: node <0-9 or ALL>")
                        continue
                    new_node = parts[1].strip().upper()
                    if new_node == 'ALL' or (new_node.isdigit() and 0 <= int(new_node) <= 9):
                        self.target_node = new_node.lower() if new_node != 'ALL' else 'ALL'
                        print(f"  Target node: {self.target_node}")
                    else:
                        print("  Invalid node ID (use 0-9 or ALL)")
                elif cmd in ['s', 'stop']:
                    await self.stop_node(self.target_node)
                elif cmd in ['r', 'ramp']:
                    await self.start_ramp(self.target_node)
                elif cmd == 'status':
                    await self.read_status(self.target_node)
                elif cmd == 'read':
                    await self.read_sensor(self.target_node)
                elif cmd in ['m', 'monitor']:
                    await self.start_monitor(self.target_node)
                elif cmd.startswith('duty'):
                    parts = cmd.split(None, 1)
                    if len(parts) < 2:
                        print("  Usage: duty <0-100>")
                        continue
                    val = int(parts[1])
                    if val < 0 or val > 100:
                        print(f"  Note: duty clamped to {max(0, min(100, val))}%")
                    await self.set_duty(self.target_node, val)
                elif cmd.isdigit():
                    await self.set_duty(self.target_node, int(cmd))
                elif cmd.startswith('raw'):
                    parts = cmd.split(None, 1)
                    if len(parts) < 2:
                        print("  Usage: raw <command>")
                        continue
                    await self.send_command(parts[1].upper())
                elif cmd.startswith('threshold'):
                    parts = cmd.split(None, 1)
                    if len(parts) < 2:
                        print("  Usage: threshold <mW> or threshold off")
                        continue
                    arg = parts[1].strip()
                    if arg == 'off':
                        if self._power_manager:
                            await self._power_manager.disable()
                    else:
                        mw = float(arg)
                        if not self._power_manager:
                            self._power_manager = PowerManager(self)
                        self._power_manager.set_threshold(mw)
                        # Start poll loop as asyncio task (legacy mode)
                        asyncio.ensure_future(self._power_manager.poll_loop())
                elif cmd.startswith('priority'):
                    parts = cmd.split(None, 1)
                    if len(parts) < 2:
                        print("  Usage: priority <node_id> or priority off")
                        continue
                    arg = parts[1].strip()
                    if arg == 'off':
                        if self._power_manager:
                            self._power_manager.clear_priority()
                    elif self._power_manager:
                        if not (arg.isdigit() and 0 <= int(arg) <= 9):
                            print(f"  Warning: '{arg}' may not be a valid node ID (expected 0-9)")
                        self._power_manager.set_priority(arg)
                    else:
                        print("  Set a threshold first")
                elif cmd == 'power':
                    if self._power_manager:
                        print(self._power_manager.status())
                    else:
                        print("  Power management not active. Use: threshold <mW>")
                else:
                    print("  Unknown command. Type 'q' to quit.")

            except KeyboardInterrupt:
                print("\nExiting...")
                break
            except (ValueError, IndexError):
                print("  Invalid value or missing argument")
            except Exception as e:
                print(f"  Error: {e}")

        if self._power_manager:
            await self._power_manager.disable()
        await self.disconnect()
