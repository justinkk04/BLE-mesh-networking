// Time-Series Charts — Single-Node View with Node Selector
let charts = {};      // { nodeId: Chart instance }
let currentWindowMinutes = 5;
let currentMetric = 'power';
let selectedNode = null;  // Currently viewed node (null = auto-pick first)
let clockOffset = 0;  // (browser_time - server_time) in seconds

const nodeSeries = {}; // { nodeId: [{x, power, voltage, current}] }
const colors = [
    '#bc8cff', '#58a6ff', '#3fb950', '#d29922', '#39c5cf', '#f85149'
];

const metricInfo = {
    power: { label: 'Power (mW)', key: 'power', unit: 'mW', decimals: 1 },
    voltage: { label: 'Voltage (V)', key: 'voltage', unit: 'V', decimals: 3 },
    current: { label: 'Current (mA)', key: 'current', unit: 'mA', decimals: 2 },
};

// Dark theme defaults
Chart.defaults.color = '#8b949e';
Chart.defaults.font.family = "'Inter', sans-serif";

export function init() {
    // Wire up time controls
    document.querySelectorAll('.time-btn').forEach(btn => {
        btn.addEventListener('click', (e) => {
            document.querySelectorAll('.time-btn').forEach(b => b.classList.remove('active'));
            e.target.classList.add('active');
            currentWindowMinutes = parseInt(e.target.dataset.minutes);
            fetchHistory();
        });
    });

    // Wire up metric tabs
    document.querySelectorAll('.metric-tab').forEach(tab => {
        tab.addEventListener('click', (e) => {
            document.querySelectorAll('.metric-tab').forEach(t => t.classList.remove('active'));
            e.target.classList.add('active');
            currentMetric = e.target.dataset.metric;
            rebuildAllCharts();
        });
    });

    // Event delegation for node selector pills
    const nodeSelector = document.getElementById('node-selector');
    if (nodeSelector) {
        nodeSelector.addEventListener('click', (e) => {
            const btn = e.target.closest('.node-tab');
            if (!btn) return;
            selectedNode = btn.dataset.node;
            updateNodeSelectorUI();
            updateChartVisibility();
        });
    }

    // Render empty selector immediately so UI isn't blank before data arrives
    rebuildNodeSelector();
    showEmptyState();

    // Initial fetch
    fetchHistory();

    // Sliding window refresh every 2s
    setInterval(updateAllWindows, 2000);
}

function showEmptyState() {
    const container = document.getElementById('charts-container');
    if (!container) return;
    if (Object.keys(charts).length === 0 && !container.querySelector('.charts-empty-state')) {
        container.innerHTML = '<div class="charts-empty-state">Waiting for node data…</div>';
    }
}

function getOrCreateChart(nodeId) {
    if (charts[nodeId]) return charts[nodeId];

    const container = document.getElementById('charts-container');
    if (!container) return null;

    // Remove empty state if present
    const empty = container.querySelector('.charts-empty-state');
    if (empty) empty.remove();

    // Create wrapper div for this node's chart
    const wrapper = document.createElement('div');
    wrapper.className = 'node-chart-wrapper';
    wrapper.id = `chart-wrap-${nodeId}`;
    wrapper.innerHTML = `
        <div class="node-chart-label">Node ${nodeId}</div>
        <canvas id="chart-${nodeId}"></canvas>
    `;
    container.appendChild(wrapper);

    const ctx = document.getElementById(`chart-${nodeId}`).getContext('2d');
    const colorIdx = Object.keys(charts).length;
    const color = colors[colorIdx % colors.length];
    const info = metricInfo[currentMetric];

    const chart = new Chart(ctx, {
        type: 'line',
        data: {
            datasets: [{
                label: info.label,
                data: [],
                borderColor: color,
                backgroundColor: color + '18',
                borderWidth: 2,
                tension: 0.3,
                pointRadius: 0,
                pointHitRadius: 10,
                fill: true,
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            animation: false,
            interaction: { mode: 'nearest', intersect: false },
            plugins: {
                legend: { display: false },
                tooltip: {
                    backgroundColor: 'rgba(15, 15, 19, 0.95)',
                    borderColor: 'rgba(255,255,255,0.1)',
                    borderWidth: 1,
                    callbacks: {
                        title: (items) => {
                            if (!items.length) return '';
                            return new Date(items[0].parsed.x).toLocaleTimeString([], { hour12: false });
                        },
                        label: (ctx) => ` ${ctx.parsed.y.toFixed(info.decimals)} ${info.unit}`
                    }
                }
            },
            scales: {
                x: {
                    type: 'linear',
                    ticks: {
                        callback: (v) => new Date(v).toLocaleTimeString([], { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' }),
                        maxTicksLimit: 6,
                        maxRotation: 0,
                        font: { size: 11 }
                    },
                    grid: { color: 'rgba(255,255,255,0.04)', drawBorder: false },
                },
                y: {
                    beginAtZero: true,
                    title: { display: true, text: info.label, color: '#484f58', font: { size: 11 } },
                    grid: { color: 'rgba(255,255,255,0.04)', drawBorder: false },
                    ticks: { font: { size: 11 } }
                }
            }
        }
    });

    charts[nodeId] = chart;
    rebuildNodeSelector();
    updateChartVisibility();
    return chart;
}

function rebuildNodeSelector() {
    const selector = document.getElementById('node-selector');
    if (!selector) return;

    const nodeIds = Object.keys(nodeSeries).sort((a, b) => Number(a) - Number(b));

    if (nodeIds.length === 0) {
        selector.innerHTML = '<span class="node-tab-empty">No nodes</span>';
        return;
    }

    // Auto-select first if none selected or selected no longer exists
    if (!selectedNode || !nodeIds.includes(selectedNode)) {
        selectedNode = nodeIds[0];
    }

    selector.innerHTML = nodeIds.map(nid => `
        <button class="node-tab ${nid === selectedNode ? 'active' : ''}" data-node="${nid}">
            Node ${nid}
        </button>
    `).join('');
}

function updateNodeSelectorUI() {
    document.querySelectorAll('.node-tab').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.node === selectedNode);
    });
}

function updateChartVisibility() {
    Object.entries(charts).forEach(([nid, chart]) => {
        const wrapper = document.getElementById(`chart-wrap-${nid}`);
        if (wrapper) {
            wrapper.classList.toggle('hidden', nid !== selectedNode);
        }
    });
    // Force active chart to resize to new container dimensions
    if (selectedNode && charts[selectedNode]) {
        charts[selectedNode].resize();
        charts[selectedNode].update('none');
    }
}

async function fetchHistory() {
    try {
        const res = await fetch(`/api/history?minutes=${currentWindowMinutes}&limit=5000`);
        const history = await res.json();

        // Clear all series
        Object.keys(nodeSeries).forEach(k => delete nodeSeries[k]);

        // Adjust server timestamps to client time using clock offset
        const offset = clockOffset;

        // Group by node (API returns DESC, iterate backwards for chronological)
        for (let i = history.length - 1; i >= 0; i--) {
            const h = history[i];
            addPoint(h.node_id, h.timestamp + offset, {
                power: h.power_mw,
                voltage: h.voltage,
                current: h.current_ma,
            }, false);
        }

        rebuildNodeSelector();
        rebuildAllCharts();
    } catch (e) {
        console.error("Failed to fetch history:", e);
    }
}

export function addPoint(nodeId, timestampSeconds, data, live = true) {
    const isNewNode = !nodeSeries[nodeId];
    if (isNewNode) {
        nodeSeries[nodeId] = [];
    }

    nodeSeries[nodeId].push({
        x: timestampSeconds * 1000,
        power: data.power || 0,
        voltage: data.voltage || 0,
        current: data.current || 0,
    });

    // Prune old data (24h max in memory)
    const cutoff = Date.now() - (1440 * 60 * 1000);
    nodeSeries[nodeId] = nodeSeries[nodeId].filter(pt => pt.x > cutoff);

    // Live update: push to chart immediately
    if (live) {
        const chart = getOrCreateChart(nodeId);
        if (chart) {
            const metricKey = metricInfo[currentMetric].key;
            const point = { x: timestampSeconds * 1000, y: data[metricKey] || 0 };
            chart.data.datasets[0].data.push(point);

            // Trim chart data to window
            const minTs = Date.now() - (currentWindowMinutes * 60 * 1000);
            chart.data.datasets[0].data = chart.data.datasets[0].data.filter(p => p.x > minTs);

            chart.update('none');
        }
        if (isNewNode) rebuildNodeSelector();
    }
}

function rebuildAllCharts() {
    const metricKey = metricInfo[currentMetric].key;
    const info = metricInfo[currentMetric];
    const now = Date.now();
    const minTs = now - (currentWindowMinutes * 60 * 1000);

    // Get all known node IDs
    const nodeIds = Object.keys(nodeSeries).sort((a, b) => Number(a) - Number(b));

    for (const nid of nodeIds) {
        const chart = getOrCreateChart(nid);
        if (!chart) continue;

        const colorIdx = nodeIds.indexOf(nid);
        const color = colors[colorIdx % colors.length];

        // Filter to current window and map to {x, y}
        const data = nodeSeries[nid]
            .filter(pt => pt.x > minTs)
            .map(pt => ({ x: pt.x, y: pt[metricKey] }));

        chart.data.datasets = [{
            label: info.label,
            data: data,
            borderColor: color,
            backgroundColor: color + '18',
            borderWidth: 2,
            tension: 0.3,
            pointRadius: 0,
            pointHitRadius: 10,
            fill: true,
        }];

        chart.options.scales.x.min = minTs;
        chart.options.scales.x.max = now;
        chart.options.scales.y.title.text = info.label;
        chart.options.plugins.tooltip.callbacks.label = (ctx) =>
            ` ${ctx.parsed.y.toFixed(info.decimals)} ${info.unit}`;

        chart.update('none');
    }

    updateChartVisibility();
}

function updateAllWindows() {
    const now = Date.now();
    const minTs = now - (currentWindowMinutes * 60 * 1000);

    for (const [nid, chart] of Object.entries(charts)) {
        chart.options.scales.x.min = minTs;
        chart.options.scales.x.max = now;

        // Trim old data points
        if (chart.data.datasets[0]) {
            chart.data.datasets[0].data = chart.data.datasets[0].data.filter(p => p.x > minTs);
        }

        chart.update('none');
    }
}

// Called by app.js to sync server/client clocks for historical data
export function setClockOffset(offset) {
    clockOffset = offset;
}

// Called by app.js when switching to Analytics tab
// Chart.js needs the container visible to measure canvas dimensions
export function refresh() {
    rebuildAllCharts();
    // Resize the visible chart now that container has dimensions
    if (selectedNode && charts[selectedNode]) {
        charts[selectedNode].resize();
    }
}
