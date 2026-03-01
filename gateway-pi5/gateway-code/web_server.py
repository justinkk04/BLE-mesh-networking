"""FastAPI web server with WebSocket support for real-time mesh data.

Embedded in the gateway process — no separate Flask/file IPC needed.
Broadcasts sensor data via WebSocket the instant it arrives from BLE.
"""

import asyncio
import json
import time
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
from pydantic import BaseModel

import db
from power_manager import PowerManager


# --- WebSocket Manager ---

class ConnectionManager:
    """Manages WebSocket connections and broadcasts."""

    def __init__(self):
        self.active_connections: list[WebSocket] = []

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)

    def disconnect(self, websocket: WebSocket):
        if websocket in self.active_connections:
            self.active_connections.remove(websocket)

    async def broadcast(self, message: dict):
        """Send JSON message to all connected WebSocket clients."""
        if not self.active_connections:
            return
        data = json.dumps(message)
        disconnected = []
        for conn in self.active_connections:
            try:
                await conn.send_text(data)
            except Exception:
                disconnected.append(conn)
        for conn in disconnected:
            if conn in self.active_connections:
                self.active_connections.remove(conn)


# --- FastAPI App ---

app = FastAPI(title="DC Monitor Mesh Dashboard")
manager = ConnectionManager()

# Reference to the gateway (set by gateway.py at startup)
_gateway = None


def set_gateway(gw):
    """Called by gateway.py to inject the DCMonitorGateway reference."""
    global _gateway
    _gateway = gw


# --- Static Files (Dashboard UI — Phase 2) ---

DASHBOARD_DIR = Path(__file__).parent.parent / "dashboard"


@app.get("/")
async def index():
    """Serve dashboard or API info."""
    index_file = DASHBOARD_DIR / "index.html"
    if index_file.exists():
        return FileResponse(index_file)
    return {
        "message": "DC Monitor Mesh Gateway API",
        "docs": "/docs",
        "endpoints": {
            "state": "GET /api/state",
            "history": "GET /api/history?minutes=30",
            "command": "POST /api/command",
            "websocket": "ws://<host>/ws",
        },
    }


# Mount static directories if they exist (Phase 2)
for _subdir in ["css", "js"]:
    _dir = DASHBOARD_DIR / _subdir
    if _dir.exists():
        app.mount(f"/{_subdir}", StaticFiles(directory=str(_dir)), name=_subdir)





# --- WebSocket Endpoint ---

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await manager.connect(websocket)
    try:
        # Send initial state on connect (always, even if gateway not ready)
        await websocket.send_text(json.dumps({
            "type": "state",
            "data": _build_state()
        }))
        # Listen for commands from the browser
        while True:
            data = await websocket.receive_text()
            msg = json.loads(data)
            if msg.get("type") == "command" and _gateway:
                cmd = msg.get("command", "")
                asyncio.create_task(_execute_command(cmd))
    except WebSocketDisconnect:
        manager.disconnect(websocket)
    except Exception:
        manager.disconnect(websocket)


async def _execute_command(cmd: str):
    """Parse and dispatch a user-friendly command (mirrors TUI dispatch logic)."""
    if not _gateway:
        await broadcast_log("[ERROR] Gateway not initialized")
        return

    cmd = cmd.strip()
    if not cmd:
        return

    parts = cmd.lower().split()
    verb = parts[0]

    try:
        # "node <id> <subcmd> [value]" — e.g. "node 0 r", "node 1 duty 50"
        if verb == 'node' and len(parts) >= 3:
            node_id = parts[1]
            subcmd = parts[2]
            if subcmd in ('r', 'read'):
                await _gateway.read_sensor(node_id)
            elif subcmd in ('s', 'stop'):
                await _gateway.stop_node(node_id)
            elif subcmd in ('ramp',):
                await _gateway.start_ramp(node_id)
            elif subcmd == 'duty' and len(parts) >= 4:
                await _gateway.set_duty(node_id, int(parts[3]))
            else:
                await broadcast_log(f"Unknown sub-command: {subcmd}")
        # Change target node: "node <id>"
        elif verb == 'node' and len(parts) == 2:
            _gateway.target_node = parts[1].upper() if parts[1].upper() == 'ALL' else parts[1]
            await broadcast_log(f"Target node set to: {_gateway.target_node}")
        # Short-hand commands using current target node
        elif verb in ('r', 'read'):
            await _gateway.read_sensor(_gateway.target_node)
        elif verb in ('s', 'stop'):
            await _gateway.stop_node(_gateway.target_node)
        elif verb in ('ramp',):
            await _gateway.start_ramp(_gateway.target_node)
        elif verb == 'duty' and len(parts) >= 2:
            await _gateway.set_duty(_gateway.target_node, int(parts[1]))
        elif verb.isdigit():
            # Bare number = set duty on target node
            await _gateway.set_duty(_gateway.target_node, int(verb))
        # Poll control: "poll <interval>" / "poll stop"
        elif verb == 'poll':
            if len(parts) >= 2 and parts[1] in ('stop', 'off'):
                await _gateway.stop_web_poll()
            elif len(parts) >= 2:
                try:
                    interval = float(parts[1])
                    await _gateway.start_web_poll(interval)
                except ValueError:
                    await broadcast_log(f"[ERROR] Invalid poll interval: {parts[1]}")
            else:
                # Toggle: start at default if stopped, else show status
                if _gateway._web_poll_requested:
                    await broadcast_log(
                        f"Polling active: every {_gateway._web_poll_interval}s "
                        f"(use 'poll stop' to disable)")
                else:
                    await _gateway.start_web_poll(_gateway._web_poll_interval)
        # Power Manager: "threshold <mW>" / "threshold off"
        elif verb == 'threshold':
            if len(parts) < 2:
                await broadcast_log("Usage: threshold <mW> or threshold off")
            elif parts[1] in ('off', 'disable'):
                if _gateway._power_manager:
                    await _gateway._power_manager.disable()
            else:
                try:
                    mw = float(parts[1])
                    if not _gateway._power_manager:
                        _gateway._power_manager = PowerManager(_gateway)
                    _gateway._power_manager.set_threshold(mw)
                    asyncio.ensure_future(_gateway._power_manager.poll_loop())
                except ValueError:
                    await broadcast_log(f"[ERROR] Invalid threshold: {parts[1]}")
        # Priority: "priority <id>" / "priority off"
        elif verb == 'priority':
            if len(parts) < 2:
                await broadcast_log("Usage: priority <node_id> or priority off")
            elif parts[1] in ('off', 'none', 'clear'):
                if _gateway._power_manager:
                    _gateway._power_manager.clear_priority()
                    await broadcast_log("Priority node cleared")
            else:
                if _gateway._power_manager:
                    _gateway._power_manager.set_priority(parts[1])
                    await broadcast_log(f"Priority node set to: {parts[1]}")
                else:
                    await broadcast_log("[ERROR] Set a threshold first")
        elif verb == 'help':
            await broadcast_log(
                "Commands: read | r, stop | s, ramp, duty <0-100>, "
                "node <id> <r|s|ramp|duty> [val], poll <sec> | poll stop, "
                "threshold <mW> | threshold off, priority <id> | priority off"
            )
        else:
            # Fall through: try sending raw to BLE  
            await _gateway.send_command(cmd)
    except Exception as e:
        await broadcast_log(f"[ERROR] {e}")


# --- REST API ---

@app.get("/api/state")
async def get_state():
    """Return current mesh state."""
    return _build_state()


@app.get("/api/history")
async def get_history(node_id: str = None, minutes: int = 30, limit: int = 500):
    """Return historical sensor readings."""
    return db.get_history(node_id=node_id, minutes=minutes, limit=limit)


class CommandRequest(BaseModel):
    command: str


@app.post("/api/command")
async def post_command(req: CommandRequest):
    """Send a command to the mesh."""
    if not _gateway:
        return {"error": "Gateway not connected"}
    await _gateway.send_command(req.command)
    return {"status": "sent", "command": req.command}


# --- Settings REST API ---

@app.get("/api/settings")
async def get_settings():
    """Return current poll and PM settings."""
    if not _gateway:
        return {"error": "Gateway not initialized"}
    pm = _gateway._power_manager
    return {
        "poll": {
            "active": _gateway._web_poll_requested,
            "interval": _gateway._web_poll_interval,
        },
        "threshold_mw": pm.threshold_mw if pm else None,
        "priority_node": pm.priority_node if pm else None,
    }


class PollSettings(BaseModel):
    interval: float

@app.put("/api/settings/poll")
async def set_poll(settings: PollSettings):
    """Start or update auto-poll interval."""
    if not _gateway:
        return {"error": "Gateway not initialized"}
    await _gateway.start_web_poll(settings.interval)
    return {"status": "ok", "interval": _gateway._web_poll_interval}

@app.delete("/api/settings/poll")
async def stop_poll():
    """Stop auto-polling."""
    if not _gateway:
        return {"error": "Gateway not initialized"}
    await _gateway.stop_web_poll()
    return {"status": "ok"}


class ThresholdSettings(BaseModel):
    threshold_mw: float

@app.put("/api/settings/threshold")
async def set_threshold(settings: ThresholdSettings):
    """Set PM power threshold."""
    if not _gateway:
        return {"error": "Gateway not initialized"}
    if not _gateway._power_manager:
        _gateway._power_manager = PowerManager(_gateway)
    _gateway._power_manager.set_threshold(settings.threshold_mw)
    asyncio.ensure_future(_gateway._power_manager.poll_loop())
    return {"status": "ok", "threshold_mw": settings.threshold_mw}

@app.delete("/api/settings/threshold")
async def clear_threshold():
    """Disable PM."""
    if not _gateway:
        return {"error": "Gateway not initialized"}
    if _gateway._power_manager:
        await _gateway._power_manager.disable()
    return {"status": "ok"}


class PrioritySettings(BaseModel):
    node_id: str

@app.put("/api/settings/priority")
async def set_priority(settings: PrioritySettings):
    """Set PM priority node."""
    if not _gateway:
        return {"error": "Gateway not initialized"}
    if not _gateway._power_manager:
        return {"error": "Set a threshold first"}
    _gateway._power_manager.set_priority(settings.node_id)
    return {"status": "ok", "priority_node": settings.node_id}

@app.delete("/api/settings/priority")
async def clear_priority():
    """Clear PM priority node."""
    if not _gateway:
        return {"error": "Gateway not initialized"}
    if _gateway._power_manager:
        _gateway._power_manager.clear_priority()
    return {"status": "ok"}


# --- DB Maintenance ---

@app.on_event("startup")
async def _start_db_maintenance():
    """Hourly DB purge of readings older than 7 days."""
    async def _maintenance_loop():
        while True:
            await asyncio.sleep(3600)  # 1 hour
            try:
                db.purge_old_readings(days=7)
            except Exception:
                pass
    asyncio.create_task(_maintenance_loop())


# --- State Builder ---

def _build_state() -> dict:
    """Build current mesh state dict from gateway + PM objects."""
    if not _gateway:
        return {"error": "Gateway not initialized"}

    state = {
        "timestamp": time.time(),
        "gateway": {
            "connected": _gateway.client is not None and _gateway.client.is_connected,
            "device_name": getattr(_gateway.connected_device, 'name', None),
            "device_address": getattr(_gateway.connected_device, 'address', None),
            "reconnecting": _gateway._reconnecting,
        },
        "power_manager": None,
        "poll": {
            "active": _gateway._web_poll_requested and (
                _gateway._web_poll_task is not None
                and not _gateway._web_poll_task.done()
            ) if _gateway._web_poll_task else False,
            "requested": _gateway._web_poll_requested,
            "interval": _gateway._web_poll_interval,
        },
        "nodes": {},
        "sensing_node_count": _gateway.sensing_node_count,
    }

    # Always populate nodes from the gateway's last seen readings
    for nid, r in getattr(_gateway, '_last_readings', {}).items():
        state["nodes"][nid] = {
            "duty": r["duty"],
            "voltage": r["voltage"],
            "current": r["current"],
            "power": r["power"],
            "last_seen": r["last_seen"],
            "responsive": time.time() - r["last_seen"] < 30,
            "commanded_duty": 0,
            "target_duty": 0,
        }

    pm = _gateway._power_manager
    if pm:
        state["power_manager"] = {
            "active": pm.threshold_mw is not None,
            "threshold_mw": pm.threshold_mw,
            "budget_mw": (pm.threshold_mw - pm.HEADROOM_MW) if pm.threshold_mw else None,
            "priority_node": pm.priority_node,
            "total_power_mw": sum(ns.power for ns in pm.nodes.values()),
        }
        # Overlay PM-specific info (targets, responsiveness) onto the known nodes
        for nid, ns in pm.nodes.items():
            if nid not in state["nodes"]:
                state["nodes"][nid] = {}
            state["nodes"][nid].update({
                "duty": ns.duty,
                "voltage": ns.voltage,
                "current": ns.current,
                "power": ns.power,
                "responsive": ns.responsive,
                "last_seen": ns.last_seen,
                "commanded_duty": ns.commanded_duty,
                "target_duty": ns.target_duty,
            })

    return state


# --- Broadcast Helpers (called by gateway event hooks) ---

async def broadcast_sensor_data(node_id: str, data: dict):
    """Called by dc_gateway.notification_handler on sensor data."""
    await manager.broadcast({
        "type": "sensor_data",
        "node_id": node_id,
        "data": data,
        "timestamp": time.time(),
    })


async def broadcast_state_change(event: str, details: dict = None):
    """Called on connect, disconnect, failover, PM changes."""
    await manager.broadcast({
        "type": "event",
        "event": event,
        "data": details or {},
        "timestamp": time.time(),
    })


async def broadcast_log(text: str):
    """Called on every gateway log message for console streaming."""
    await manager.broadcast({
        "type": "log",
        "text": text,
        "timestamp": time.time(),
    })
