// WebSocket Manager, Tab Navigation, and App Bootstrap
import * as topology from './topology.js';
import * as nodes from './nodes.js';
import * as charts from './charts.js';
import * as consoleLog from './console.js';

let ws;
let reconnectDelay = 1000;
const MAX_DELAY = 10000;
let currentGatewayNode = null; // Store the active GATT node ID

// ── App Init ──
document.addEventListener('DOMContentLoaded', () => {
    // Initialize modules
    topology.init();
    nodes.init(sendCommand);
    charts.init();
    consoleLog.init(sendCommand);

    // Tab navigation
    initTabs();

    // Poll controls
    initPollControls();

    // Initial data fetch
    fetchInitialState();

    // Connect WebSocket
    connectWebSocket();
});

// ── Tab Navigation ──
function initTabs() {
    document.querySelectorAll('.tab-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            const tabId = btn.dataset.tab;
            switchTab(tabId);
        });
    });
}

function switchTab(tabId) {
    // Deactivate all tabs and content
    document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
    document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));

    // Activate selected
    const btn = document.querySelector(`.tab-btn[data-tab="${tabId}"]`);
    const content = document.getElementById(tabId);
    if (btn) btn.classList.add('active');
    if (content) content.classList.add('active');

    // Refresh charts when switching to analytics tab (ensures canvas sizes are correct)
    if (tabId === 'tab-analytics') {
        charts.refresh();
    } else if (tabId === 'tab-dashboard') {
        topology.refresh();
    }
}

// ── Poll Controls ──
function initPollControls() {
    const pollBtn = document.getElementById('poll-toggle');
    const pollInput = document.getElementById('poll-interval');
    if (pollBtn) {
        pollBtn.addEventListener('click', () => {
            const isActive = pollBtn.classList.contains('active');
            if (isActive) {
                sendCommand('poll stop');
            } else {
                const interval = parseFloat(pollInput?.value) || 2.0;
                sendCommand(`poll ${interval}`);
            }
        });
    }
    if (pollInput) {
        pollInput.addEventListener('change', () => {
            if (document.getElementById('poll-toggle')?.classList.contains('active')) {
                const interval = parseFloat(pollInput.value) || 2.0;
                sendCommand(`poll ${interval}`);
            }
        });
    }
}

// ── WebSocket ──
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
        updateConnectionBadge('connected', '● Connected');
        document.getElementById('ws-status').textContent = 'WebSocket: Connected';
    };

    ws.onclose = () => {
        updateConnectionBadge('disconnected', '● Disconnected');
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

// ── Message Handlers ──
function handleMessage(msg) {
    if (msg.type === 'state') {
        handleState(msg.data);
    } else if (msg.type === 'sensor_data') {
        nodes.updateNode(msg.node_id, msg.data);
        charts.addPoint(msg.node_id, msg.timestamp, {
            power: msg.data.power,
            voltage: msg.data.voltage,
            current: msg.data.current,
        });
        const allNodes = nodes.getAllNodes();
        topology.updateGraph(allNodes, currentGatewayNode);

        // Show in console if this is a user-triggered response
        if (msg.user_triggered) {
            const d = msg.data;
            const line = `NODE${msg.node_id} >> D:${d.duty}%, V:${d.voltage.toFixed(3)}V, I:${d.current.toFixed(1)}mA, P:${d.power.toFixed(1)}mW`;
            consoleLog.appendLog(line, msg.timestamp);
        }
    } else if (msg.type === 'log') {
        consoleLog.appendLog(msg.text, msg.timestamp);
    } else if (msg.type === 'event') {
        if (msg.event === 'pm_update') {
            updatePowerManager(msg.data);
        } else if (msg.event === 'poll_update') {
            updatePollStatus(msg.data);
        } else if (['connected', 'disconnected', 'reconnecting'].includes(msg.event)) {
            fetchInitialState();
        }
    }
}

function handleState(state) {
    if (!state || state.error) return;

    // Gateway connection status
    if (state.gateway) {
        const gw = state.gateway;
        currentGatewayNode = gw.connected_node !== undefined ? gw.connected_node : null;
        let badgeStatus = 'disconnected';
        let text = '● Disconnected';
        if (gw.reconnecting) {
            badgeStatus = 'reconnecting';
            text = '● Reconnecting';
        } else if (gw.connected) {
            badgeStatus = 'connected';
            text = `● ${gw.device_name || gw.device_address || 'Connected'}`;
        }
        updateConnectionBadge(badgeStatus, text);
    }

    // Nodes
    if (state.nodes) {
        Object.entries(state.nodes).forEach(([id, data]) => {
            nodes.updateNode(id, data);
        });
        topology.updateGraph(nodes.getAllNodes(), currentGatewayNode);
    }

    // Power Manager
    if (state.power_manager) {
        updatePowerManager(state.power_manager);
    }

    // Poll state
    if (state.poll) {
        updatePollStatus(state.poll);
    }

    // Header count
    if (state.sensing_node_count !== undefined) {
        document.getElementById('node-summary').textContent = `${state.sensing_node_count} nodes active`;
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

function updatePollStatus(poll) {
    const btn = document.getElementById('poll-toggle');
    const input = document.getElementById('poll-interval');
    if (!btn) return;

    const isActive = poll.active || poll.requested;
    btn.textContent = isActive ? 'Stop' : 'Start';
    btn.className = isActive ? 'poll-btn active' : 'poll-btn';
    if (input && poll.interval !== undefined) {
        input.value = poll.interval;
    }
}

// ── Send Command ──
export function sendCommand(cmdStr) {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
        consoleLog.appendLog(`[ERROR] WebSocket disconnected. Cannot send: ${cmdStr}`, Date.now() / 1000);
        return;
    }

    ws.send(JSON.stringify({
        type: 'command',
        command: cmdStr
    }));
}
