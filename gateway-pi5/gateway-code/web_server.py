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
    """Execute a gateway command via the normal BLE path."""
    if not _gateway:
        return
    await _gateway.send_command(cmd)


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
            "responsive": time.monotonic() - r["last_seen"] < 30,
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
