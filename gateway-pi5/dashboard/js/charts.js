// Time-Series Charts — Multi-Metric (Power / Voltage / Current)
let charts = {};      // { nodeId: Chart instance }
let currentWindowMinutes = 5;
let currentMetric = 'power';

const nodeSeries = {}; // { nodeId: [{x, power, voltage, current}] }
const colors = [
    '#58a6ff', '#3fb950', '#bc8cff', '#d29922', '#39c5cf', '#f85149'
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

    // Initial fetch
    fetchHistory();

    // Sliding window refresh every 2s
    setInterval(updateAllWindows, 2000);
}

function getOrCreateChart(nodeId) {
    if (charts[nodeId]) return charts[nodeId];

    const container = document.getElementById('charts-container');
    if (!container) return null;

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
                        font: { size: 10 }
                    },
                    grid: { color: 'rgba(255,255,255,0.04)', drawBorder: false },
                },
                y: {
                    beginAtZero: true,
                    title: { display: true, text: info.label, color: '#484f58', font: { size: 10 } },
                    grid: { color: 'rgba(255,255,255,0.04)', drawBorder: false },
                    ticks: { font: { size: 10 } }
                }
            }
        }
    });

    charts[nodeId] = chart;
    return chart;
}

async function fetchHistory() {
    try {
        const res = await fetch(`/api/history?minutes=${currentWindowMinutes}&limit=5000`);
        const history = await res.json();

        // Clear all series
        Object.keys(nodeSeries).forEach(k => delete nodeSeries[k]);

        // Group by node (API returns DESC, iterate backwards for chronological)
        for (let i = history.length - 1; i >= 0; i--) {
            const h = history[i];
            addPoint(h.node_id, h.timestamp, {
                power: h.power_mw,
                voltage: h.voltage,
                current: h.current_ma,
            }, false);
        }

        rebuildAllCharts();
    } catch (e) {
        console.error("Failed to fetch history:", e);
    }
}

export function addPoint(nodeId, timestampSeconds, data, live = true) {
    if (!nodeSeries[nodeId]) {
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
    }
}

function rebuildAllCharts() {
    const metricKey = metricInfo[currentMetric].key;
    const info = metricInfo[currentMetric];
    const now = Date.now();
    const minTs = now - (currentWindowMinutes * 60 * 1000);

    // Get all known node IDs
    const nodeIds = Object.keys(nodeSeries).sort();

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

// Called by app.js when switching to Analytics tab
// Chart.js needs the container visible to measure canvas dimensions
export function refresh() {
    rebuildAllCharts();
}

