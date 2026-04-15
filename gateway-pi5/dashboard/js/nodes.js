// Node Card Grid Logic
let sendCmdFunc;
const nodesData = {};

export function init(sendCommandCallback) {
    sendCmdFunc = sendCommandCallback;

    // Event delegation for inline node controls
    const container = document.getElementById('node-cards');
    container.addEventListener('click', (e) => {
        const btn = e.target.closest('[data-action]');
        if (!btn) return;
        const nodeId = btn.dataset.node;
        const action = btn.dataset.action;

        if (action === 'read') {
            sendCmdFunc(`node ${nodeId} read`);
        } else if (action === 'duty') {
            const input = btn.closest('.duty-input-group').querySelector('.duty-input');
            const val = input.value;
            if (val !== '') sendCmdFunc(`node ${nodeId} duty ${val}`);
        }
    });

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
    if (!data.last_seen) return { cls: 'online', text: 'Online' };

    const ageSec = Date.now() / 1000 - data.last_seen;

    if (ageSec > 30) return { cls: 'offline', text: `Offline · ${Math.round(ageSec)}s` };
    if (ageSec > 10) return { cls: 'stale', text: `Stale · ${Math.round(ageSec)}s` };
    return { cls: 'online', text: 'Online' };
}

function renderCard(id, data) {
    let card = document.getElementById(`node-card-${id}`);
    const container = document.getElementById('node-cards');
    const { cls: statusClass, text: statusText } = getStatus(data);

    if (!card) {
        card = document.createElement('div');
        card.id = `node-card-${id}`;
        card.className = `node-card ${statusClass} animate-in`;

        card.innerHTML = `
            <div class="node-header">
                <div class="node-title">
                    Node ${id} <span class="badge ${statusClass}">${statusText}</span>
                </div>
            </div>
            <div class="node-metrics">
                <div class="metric">
                    <span class="metric-label">POWER</span>
                    <span class="metric-value" data-field="power">-- mW</span>
                </div>
                <div class="metric">
                    <span class="metric-label">VOLTAGE</span>
                    <span class="metric-value" data-field="voltage">-- V</span>
                </div>
                <div class="metric">
                    <span class="metric-label">CURRENT</span>
                    <span class="metric-value" data-field="current">-- mA</span>
                </div>
            </div>
            <div class="duty-bar-row">
                <span class="duty-label">DUTY CYCLE</span>
                <div class="duty-bar">
                    <div class="duty-fill" data-field="duty-fill"></div>
                </div>
                <span class="duty-pct" data-field="duty-pct">0%</span>
            </div>
            <div class="node-controls">
                <div class="duty-input-group">
                    <label>Set Duty:</label>
                    <input type="number" class="duty-input" min="0" max="100" step="5" placeholder="%" data-node="${id}">
                    <button class="btn-set" data-action="duty" data-node="${id}">Set</button>
                </div>
                <button class="btn-read" data-action="read" data-node="${id}">Read</button>
            </div>
        `;
        container.appendChild(card);

        // Remove animation class after it completes
        card.addEventListener('animationend', () => {
            card.classList.remove('animate-in');
        }, { once: true });
    }

    // Update status
    card.className = `node-card ${statusClass}`;
    const badge = card.querySelector('.badge');
    badge.className = `badge ${statusClass}`;
    badge.textContent = statusText;

    // Update metric values using data-field selectors within this card
    const powerEl  = card.querySelector('[data-field="power"]');
    const voltEl   = card.querySelector('[data-field="voltage"]');
    const currEl   = card.querySelector('[data-field="current"]');
    const dutyFill = card.querySelector('[data-field="duty-fill"]');
    const dutyPct  = card.querySelector('[data-field="duty-pct"]');

    const newPower = data.power   ? `${data.power.toFixed(1)} mW`   : '-- mW';
    const newVolt  = data.voltage ? `${data.voltage.toFixed(2)} V`   : '-- V';
    const newCurr  = data.current ? `${data.current.toFixed(2)} mA`  : '-- mA';

    if (powerEl.textContent !== newPower) {
        powerEl.textContent = newPower;
        flashElement(powerEl);
    }
    if (voltEl.textContent !== newVolt) {
        voltEl.textContent = newVolt;
        flashElement(voltEl);
    }
    if (currEl.textContent !== newCurr) {
        currEl.textContent = newCurr;
        flashElement(currEl);
    }

    // Duty bar — width is duty %, color based on level
    const duty = Math.max(0, Math.min(100, Number(data.duty) || 0));
    dutyFill.style.width = `${duty}%`;
    dutyPct.textContent = `${duty}%`;

    let color;
    if (duty > 80)      color = 'var(--accent-red)';
    else if (duty > 50) color = 'var(--accent-orange)';
    else if (duty > 0)  color = 'var(--accent-primary)';
    else                color = 'transparent';
    dutyFill.style.background = color;
}

function flashElement(el) {
    el.classList.remove('flash');
    // Force reflow to restart animation
    void el.offsetWidth;
    el.classList.add('flash');
}
