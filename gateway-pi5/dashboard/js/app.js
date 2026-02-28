// WebSocket Manager and App Bootstrap
import * as topology from './topology.js';
import * as nodes from './nodes.js';
import * as charts from './charts.js';
import * as consoleLog from './console.js';

let ws = null;
let reconnectDelay = 1000;
const MAX_DELAY = 30000;

document.addEventListener('DOMContentLoaded', () => {
    // Initialize modules
    topology.init();
    nodes.init(sendCommand);
    charts.init();
    consoleLog.init(sendCommand);

    // Initial data fetch
    fetchInitialState();

    // Connect WebSocket
    connectWebSocket();
});

async function fetchInitialState() {
    try {
        const res = await fetch('/api/state');
        if (!res.ok) throw new Error('Failed to fetch state');
        const state = await res.json();
        handleState(state);
    } catch (e) {
        console.error('Initial state fetch error:', e);
    }
}

function connectWebSocket() {
    const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(`${protocol}//${location.host}/ws`);

    ws.onopen = () => {
        reconnectDelay = 1000;
        updateConnectionBadge('connected', 'WebSocket: Connected');
        document.getElementById('ws-status').textContent = 'WebSocket: Connected';
    };

    ws.onclose = () => {
        updateConnectionBadge('disconnected', 'WebSocket: Disconnected');
        document.getElementById('ws-status').textContent = 'WebSocket: Reconnecting...';
        setTimeout(connectWebSocket, reconnectDelay);
        reconnectDelay = Math.min(reconnectDelay * 2, MAX_DELAY);
    };

    ws.onerror = (err) => {
        console.error('WebSocket Error:', err);
    };

    ws.onmessage = (event) => {
        try {
            const msg = JSON.parse(event.data);
            handleMessage(msg);
        } catch (e) {
            console.error('Failed to parse WS msg:', e, event.data);
        }
    };
}

function handleMessage(msg) {
    if (msg.type === 'state') {
        handleState(msg.data);
    } else if (msg.type === 'sensor_data') {
        nodes.updateNode(msg.node_id, msg.data);
        charts.addPoint(msg.node_id, msg.timestamp, msg.data.power);
        const allNodes = nodes.getAllNodes();
        topology.updateGraph(allNodes);
    } else if (msg.type === 'log') {
        consoleLog.appendLog(msg.text, msg.timestamp);
    } else if (msg.type === 'event') {
        if (msg.event === 'pm_update') {
            updatePowerManager(msg.data);
        } else if (['connected', 'disconnected', 'reconnecting'].includes(msg.event)) {
            fetchInitialState(); // Refresh full state on connection events
        }
    }
}

function handleState(state) {
    if (!state || state.error) return;

    // Gateway connection status
    if (state.gateway) {
        const gw = state.gateway;
        let badgeStatus = 'disconnected';
        let text = '● Disconnected';
        if (gw.reconnecting) {
            badgeStatus = 'reconnecting';
            text = '● Reconnecting';
        } else if (gw.connected) {
            badgeStatus = 'connected';
            text = `● Connected to ${gw.device_name || gw.device_address}`;
        }
        updateConnectionBadge(badgeStatus, text);
    }

    // Nodes
    if (state.nodes) {
        Object.entries(state.nodes).forEach(([id, data]) => {
            nodes.updateNode(id, data);
        });
        topology.updateGraph(nodes.getAllNodes(), state.gateway?.device_address);
    }

    // Power Manager
    if (state.power_manager) {
        updatePowerManager(state.power_manager);
    }

    // Header count
    if (state.sensing_node_count !== undefined) {
        document.getElementById('node-summary').textContent = `${state.sensing_node_count} sensing, 0 relays`;
    }
}

function updatePowerManager(pm) {
    const act = document.getElementById('pm-active');
    if (pm.active || pm.budget) {
        act.textContent = 'Active';
        act.className = 'badge online';
    } else {
        act.textContent = 'Inactive';
        act.className = 'badge offline';
    }

    document.getElementById('pm-threshold').textContent = pm.threshold_mw ? `${pm.threshold_mw.toFixed(1)} mW` : '--';
    document.getElementById('pm-budget').textContent = pm.budget_mw ? `${pm.budget_mw.toFixed(1)} mW` : (pm.budget ? `${pm.budget.toFixed(1)} mW` : '--');

    // total_power vs total_power_mw based on event vs state payload
    const total = pm.total_power_mw !== undefined ? pm.total_power_mw : pm.total_power;
    document.getElementById('pm-total').textContent = total !== undefined ? `${total.toFixed(1)} mW` : '--';

    document.getElementById('pm-priority').textContent = pm.priority_node !== undefined && pm.priority_node !== null ? pm.priority_node : 'None';

    // Pass PM target info to nodes module
    if (pm.changes) {
        Object.entries(pm.changes).forEach(([nid, chg]) => {
            nodes.updateNode(nid, { target_duty: chg.new_target });
        });
    }
}

function updateConnectionBadge(status, text = null) {
    const badge = document.getElementById('connection-badge');
    badge.className = `badge ${status}`;
    if (text) badge.textContent = text;
}

export function sendCommand(cmdStr) {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
        consoleLog.appendLog(`[ERROR] WebSocket disconnected. Cannot send: ${cmdStr}`, Date.now() / 1000);
        return;
    }

    // Send via WebSocket (backend expects {"type": "command", "command": "..."})
    ws.send(JSON.stringify({
        type: 'command',
        command: cmdStr
    }));
}
