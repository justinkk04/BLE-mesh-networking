#!/usr/bin/env python3
"""
BLE Gateway for DC Monitor Mesh Network
Connects to ESP32-C6 GATT gateway and sends commands to mesh nodes

Usage:
    python gateway.py                       # TUI interactive mode (default)
    python gateway.py --scan                # Just scan for gateways
    python gateway.py --node 0 --ramp       # Send RAMP to node 0
    python gateway.py --node 1 --duty 50    # Set duty 50% on node 1
    python gateway.py --node all --stop     # Stop all nodes
    python gateway.py --node 0 --read       # Single sensor reading
    python gateway.py --node 0 --monitor    # Continuous monitoring
    python gateway.py --no-tui              # Plain CLI mode (legacy)

Commands are sent as NODE_ID:COMMAND[:VALUE] to the ESP32-C6 mesh gateway,
which forwards them to the targeted mesh node via BLE Mesh.
"""

import argparse
import asyncio
import sys

from dc_gateway import DCMonitorGateway

# Check for textual
_HAS_TEXTUAL = False
try:
    from tui_app import MeshGatewayApp
    _HAS_TEXTUAL = True
except ImportError:
    print("Note: textual not available. Install with: pip install textual")
    print("      Falling back to plain CLI mode.\n")


def main():
    """Entry point — decides between TUI and CLI mode."""
    parser = argparse.ArgumentParser(description="BLE Gateway for DC Monitor Mesh")
    parser.add_argument("--scan", action="store_true", help="Scan for gateways only")
    parser.add_argument("--address", type=str, help="Connect to specific MAC address")
    parser.add_argument("--node", type=str, default="0",
                        help="Target mesh node ID (0-9 or ALL, default: 0)")
    parser.add_argument("--duty", type=int, help="Set duty cycle (0-100%%)")
    parser.add_argument("--ramp", action="store_true", help="Run ramp test")
    parser.add_argument("--stop", action="store_true", help="Stop load")
    parser.add_argument("--status", action="store_true", help="Get node status")
    parser.add_argument("--read", action="store_true", help="Single sensor reading")
    parser.add_argument("--monitor", action="store_true", help="Start continuous monitoring")
    parser.add_argument("--timeout", type=float, default=10.0, help="Scan timeout")
    parser.add_argument("--no-tui", action="store_true",
                        help="Use plain CLI mode instead of TUI")
    parser.add_argument("--web", action="store_true",
                        help="Enable web dashboard alongside TUI")
    parser.add_argument("--web-only", action="store_true",
                        help="Web dashboard only, no TUI")
    parser.add_argument("--web-port", type=int, default=8000,
                        help="Web dashboard port (default 8000)")
    args = parser.parse_args()

    # Validate --node argument
    if args.node.upper() != "ALL" and not (args.node.isdigit() and 0 <= int(args.node) <= 9):
        parser.error(f"Invalid node ID '{args.node}': use 0-9 or ALL")

    node = args.node.upper() if args.node.upper() == "ALL" else args.node
    is_oneshot = args.scan or args.stop or args.ramp or args.status or args.read \
        or args.monitor or args.duty is not None

    # Web-only mode: no TUI, just web dashboard + BLE
    if args.web_only:
        _run_web_only(args, node)
        return

    # If TUI available and not one-shot and not --no-tui, launch TUI
    # Textual's app.run() manages its own event loop, so call it directly (not from asyncio.run)
    if _HAS_TEXTUAL and not is_oneshot and not args.no_tui:
        gateway = DCMonitorGateway()

        # Initialize web dashboard if --web flag is set
        if args.web:
            import db
            import web_server
            db.init_db()
            web_server.set_gateway(gateway)
            gateway._web_enabled = True
            gateway._web_port = args.web_port

        app = MeshGatewayApp(
            gateway,
            target_address=args.address,
            default_node=node,
            scan_timeout=args.timeout,
        )
        app.run()
        return

    # Otherwise: one-shot or legacy CLI mode (needs asyncio.run)
    asyncio.run(_run_cli(args, node))


async def _run_cli(args, node: str):
    """Run one-shot CLI commands or legacy interactive mode."""
    gateway = DCMonitorGateway()

    print("\n" + "=" * 50)
    print("  DC Monitor Mesh Gateway (Pi 5)")
    print("=" * 50)

    devices = await gateway.scan_for_nodes(
        timeout=args.timeout, target_address=args.address
    )

    if args.scan:
        print(f"\nFound {len(devices)} gateway(s)")
        return

    if not devices:
        return

    # Select device
    if args.address:
        device = next(
            (d for d in devices if d.address.upper() == args.address.upper()),
            devices[0],
        )
    else:
        device = devices[0]

    # Connect
    if not await gateway.connect_to_node(device):
        return

    # Handle one-shot CLI commands
    if args.stop:
        await gateway.stop_node(node)
        await asyncio.sleep(1)
    elif args.duty is not None:
        await gateway.set_duty(node, args.duty)
        await asyncio.sleep(2)
    elif args.ramp:
        await gateway.start_ramp(node)
        await asyncio.sleep(2)
    elif args.status:
        await gateway.read_status(node)
        await asyncio.sleep(2)
    elif args.read:
        await gateway.read_sensor(node)
        await asyncio.sleep(2)
    elif args.monitor:
        await gateway.start_monitor(node)
        print("Monitoring... press Ctrl+C to stop")
        try:
            while gateway.client and gateway.client.is_connected:
                await asyncio.sleep(1)
        except KeyboardInterrupt:
            pass
    else:
        # Legacy interactive mode (--no-tui)
        await gateway.interactive_mode(default_node=node)

    await gateway.disconnect()


def _run_web_only(args, node: str):
    """Run gateway with web dashboard only (no TUI)."""
    import db
    import web_server
    import uvicorn
    from ble_thread import BleThread

    gateway = DCMonitorGateway()
    gateway._web_enabled = True

    bt = BleThread()
    bt.start()
    gateway.ble_thread = bt

    db.init_db()
    web_server.set_gateway(gateway)

    async def startup_and_serve():
        # Scan and connect
        devices = await gateway.scan_for_nodes(
            timeout=args.timeout, target_address=args.address)
        if devices:
            if args.address:
                device = next(
                    (d for d in devices if d.address.upper() == args.address.upper()),
                    devices[0])
            else:
                device = devices[0]

            # Try each device until one connects
            for dev in ([device] + [d for d in devices if d != device]):
                if await gateway.connect_to_node(dev):
                    gateway.sensing_node_count = len(devices)
                    print(f"  Connected to {dev.name or dev.address} "
                          f"({len(devices)} node(s))")
                    asyncio.ensure_future(gateway._auto_reconnect_loop())
                    break
        else:
            print("  No gateways found. Web server starting anyway...")

        # Start uvicorn
        config = uvicorn.Config(
            web_server.app, host="0.0.0.0", port=args.web_port,
            log_level="info")
        server = uvicorn.Server(config)
        await server.serve()

    print("\n" + "=" * 50)
    print("  DC Monitor Mesh Gateway — Web Only Mode")
    print("=" * 50)
    print(f"  Dashboard: http://0.0.0.0:{args.web_port}")
    print(f"  API:       http://0.0.0.0:{args.web_port}/api/state")
    print(f"  WebSocket: ws://0.0.0.0:{args.web_port}/ws")
    print()

    future = bt.submit(startup_and_serve())
    try:
        future.result()
    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        gateway.running = False
        bt.stop()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nGoodbye!")
