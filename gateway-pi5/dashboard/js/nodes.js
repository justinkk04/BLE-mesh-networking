// Node Card Grid Logic
let sendCmdFunc;
const nodesData = {};

export function init(sendCommandCallback) {
    sendCmdFunc = sendCommandCallback;

    // Periodic timer to update status badges (stale -> offline transitions)
    setInterval(() => {
        Object.entries(nodesData).forEach(([id, data]) => {
            renderCard(id, data);
        });
    }, 2000);
}

export function updateNode(id, data) {
    // Always stamp last_seen to NOW when we receive fresh data via WebSocket
    if (data.duty !== undefined || data.voltage !== undefined || data.power !== undefined) {
        data.last_seen = Date.now() / 1000;
    }
    // Merge new data into existing
    nodesData[id] = { ...nodesData[id], ...data };
    renderCard(id, nodesData[id]);
}

export function getAllNodes() {
    return nodesData;
}

function getStatus(data) {
    // If we never received last_seen, treat as online (just got data)
    if (!data.last_seen) return { cls: 'online', text: 'Online' };

    const ageSec = Date.now() / 1000 - data.last_seen;

    if (ageSec > 30) return { cls: 'offline', text: `Offline (${Math.round(ageSec)}s)` };
    if (ageSec > 10) return { cls: 'stale', text: `Stale (${Math.round(ageSec)}s)` };
    return { cls: 'online', text: 'Online' };
}

function renderCard(id, data) {
    let card = document.getElementById(`node-card-${id}`);
    const container = document.getElementById('node-cards');
    const { cls: statusClass, text: statusText } = getStatus(data);

    if (!card) {
        card = document.createElement('div');
        card.id = `node-card-${id}`;
        card.className = `node-card ${statusClass}`;

        card.innerHTML = `
            <div class="node-header">
                <div class="node-title">
                    Node ${id} <span class="badge ${statusClass}">${statusText}</span>
                </div>
                <div style="position: relative;">
                    <button class="node-menu-btn" onclick="document.getElementById('popover-${id}').classList.toggle('active')">
                        ⚙️
                    </button>
                    <div id="popover-${id}" class="popover">
                        <div class="popover-item" onclick="promptDuty('${id}')">Set Duty %</div>
                        <div class="popover-item" onclick="sendRead('${id}')">Read Sensor</div>
                        <div class="popover-item" onclick="sendStop('${id}')">Stop Load</div>
                    </div>
                </div>
            </div>
            <div class="node-metrics">
                <div class="metric">
                    <span class="metric-label">Duty / Cmd / Tgt</span>
                    <span class="metric-val">
                        <span id="duty-${id}">0</span>% / 
                        <span style="color:var(--text-secondary)"><span id="cmd-${id}">0</span>%</span> / 
                        <span style="color:var(--accent-cyan)"><span id="tgt-${id}">0</span>%</span>
                    </span>
                </div>
                <div class="metric">
                    <span class="metric-label">Power</span>
                    <span class="metric-val"><span id="power-${id}">0.0</span> mW</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Voltage</span>
                    <span class="metric-val"><span id="volt-${id}">0.0</span> V</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Current</span>
                    <span class="metric-val"><span id="curr-${id}">0.0</span> mA</span>
                </div>
            </div>
            <div class="power-bar-bg">
                <div id="bar-${id}" class="power-bar-fill"></div>
            </div>
        `;
        container.appendChild(card);
    }

    // Update status
    card.className = `node-card ${statusClass}`;
    const badge = card.querySelector('.badge');
    badge.className = `badge ${statusClass}`;
    badge.textContent = statusText;

    // Update values
    document.getElementById(`duty-${id}`).textContent = data.duty || 0;
    document.getElementById(`cmd-${id}`).textContent = data.commanded_duty || 0;
    document.getElementById(`tgt-${id}`).textContent = data.target_duty || 0;

    document.getElementById(`power-${id}`).textContent = data.power ? data.power.toFixed(1) : "0.0";
    document.getElementById(`volt-${id}`).textContent = data.voltage ? data.voltage.toFixed(2) : "0.00";
    document.getElementById(`curr-${id}`).textContent = data.current ? data.current.toFixed(2) : "0.00";

    // Power bar
    const maxScale = 500;
    let pct = Math.min(((data.power || 0) / maxScale) * 100, 100);
    const bar = document.getElementById(`bar-${id}`);
    bar.style.width = `${pct}%`;
    // Color the bar based on power level
    if (pct > 80) bar.style.backgroundColor = 'var(--accent-red)';
    else if (pct > 50) bar.style.backgroundColor = 'var(--accent-orange)';
    else bar.style.backgroundColor = 'var(--accent-blue)';

    // Brief flash on data update
    const powerSpan = document.getElementById(`power-${id}`);
    powerSpan.style.color = "var(--accent-green)";
    setTimeout(() => { powerSpan.style.color = ""; }, 400);
}

// Global hooks for popover actions
window.promptDuty = (id) => {
    document.getElementById(`popover-${id}`).classList.remove('active');
    const val = prompt(`Enter new duty cycle for Node ${id} (0-100):`);
    if (val !== null && !isNaN(val)) {
        sendCmdFunc(`node ${id} duty ${val}`);
    }
};

window.sendRead = (id) => {
    document.getElementById(`popover-${id}`).classList.remove('active');
    sendCmdFunc(`node ${id} read`);
};

window.sendStop = (id) => {
    document.getElementById(`popover-${id}`).classList.remove('active');
    sendCmdFunc(`node ${id} stop`);
};

// Close all popovers on outside click
document.addEventListener('click', (e) => {
    if (!e.target.matches('.node-menu-btn')) {
        document.querySelectorAll('.popover.active').forEach(p => p.classList.remove('active'));
    }
});
