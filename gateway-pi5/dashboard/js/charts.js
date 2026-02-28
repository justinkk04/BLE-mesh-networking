// Time-Series Charts Logic
let powerChart;
let chartConfig;
let currentWindowMinutes = 5;

const nodeSeries = {};
const colors = [
    '#58a6ff', '#3fb950', '#bc8cff', '#d29922', '#39c5cf', '#f85149'
];

export function init() {
    const ctx = document.getElementById('power-chart').getContext('2d');

    // Dark theme defaults
    Chart.defaults.color = '#8b949e';
    Chart.defaults.font.family = "'Inter', sans-serif";

    chartConfig = {
        type: 'line',
        data: { datasets: [] },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            animation: false,
            interaction: {
                mode: 'nearest',
                intersect: false,
            },
            plugins: {
                legend: {
                    position: 'top',
                    align: 'end',
                    labels: {
                        usePointStyle: true,
                        pointStyle: 'circle',
                        padding: 16,
                        font: { size: 12 }
                    }
                },
                tooltip: {
                    backgroundColor: 'rgba(15, 15, 19, 0.95)',
                    borderColor: 'rgba(255,255,255,0.1)',
                    borderWidth: 1,
                    titleFont: { weight: '600' },
                    callbacks: {
                        title: function (items) {
                            if (!items.length) return '';
                            return new Date(items[0].parsed.x).toLocaleTimeString([], { hour12: false });
                        },
                        label: function (ctx) {
                            return ` ${ctx.dataset.label}: ${ctx.parsed.y.toFixed(1)} mW`;
                        }
                    }
                }
            },
            scales: {
                x: {
                    type: 'linear',
                    ticks: {
                        callback: function (value) {
                            const d = new Date(value);
                            return d.toLocaleTimeString([], { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' });
                        },
                        maxTicksLimit: 8,
                        maxRotation: 0,
                        font: { size: 11 }
                    },
                    grid: {
                        color: 'rgba(255,255,255,0.04)',
                        drawBorder: false,
                    },
                    title: {
                        display: true,
                        text: 'Time',
                        color: '#484f58',
                        font: { size: 11 }
                    }
                },
                y: {
                    beginAtZero: true,
                    title: {
                        display: true,
                        text: 'Power (mW)',
                        color: '#484f58',
                        font: { size: 11 }
                    },
                    grid: {
                        color: 'rgba(255,255,255,0.04)',
                        drawBorder: false,
                    },
                    ticks: {
                        font: { size: 11 },
                        callback: function (val) { return val.toFixed(0); }
                    }
                }
            }
        }
    };

    powerChart = new Chart(ctx, chartConfig);

    // Wire up time controls
    document.querySelectorAll('.time-btn').forEach(btn => {
        btn.addEventListener('click', (e) => {
            document.querySelectorAll('.time-btn').forEach(b => b.classList.remove('active'));
            e.target.classList.add('active');
            currentWindowMinutes = parseInt(e.target.dataset.minutes);
            fetchHistory();
        });
    });

    // Initial fetch
    fetchHistory();

    // Sliding window refresh
    setInterval(updateChartWindow, 2000);
}

async function fetchHistory() {
    try {
        const res = await fetch(`/api/history?minutes=${currentWindowMinutes}&limit=5000`);
        const history = await res.json();

        // Clear series
        Object.keys(nodeSeries).forEach(k => delete nodeSeries[k]);

        // Group by node (API returns DESC, iterate backwards for chronological)
        for (let i = history.length - 1; i >= 0; i--) {
            const h = history[i];
            addPoint(h.node_id, h.timestamp, h.power_mw, false);
        }

        rebuildDatasets();
    } catch (e) {
        console.error("Failed to fetch history:", e);
    }
}

export function addPoint(nodeId, timestampSeconds, powerMw, shouldRebuild = true) {
    if (!nodeSeries[nodeId]) {
        nodeSeries[nodeId] = [];
        if (shouldRebuild) rebuildDatasets();
    }

    nodeSeries[nodeId].push({ x: timestampSeconds * 1000, y: powerMw });

    // Prune very old data (max 24h in memory)
    const cutoff = Date.now() - (1440 * 60 * 1000);
    nodeSeries[nodeId] = nodeSeries[nodeId].filter(pt => pt.x > cutoff);
}

function rebuildDatasets() {
    const keys = Object.keys(nodeSeries).sort();
    chartConfig.data.datasets = keys.map((nid, i) => ({
        label: `Node ${nid}`,
        data: nodeSeries[nid],
        borderColor: colors[i % colors.length],
        backgroundColor: colors[i % colors.length] + '20',
        borderWidth: 2,
        tension: 0.3,
        pointRadius: 0,
        pointHitRadius: 10,
        fill: true,
    }));
    powerChart.update('none');
}

function updateChartWindow() {
    const now = Date.now();
    const minTimestamp = now - (currentWindowMinutes * 60 * 1000);

    chartConfig.options.scales.x.min = minTimestamp;
    chartConfig.options.scales.x.max = now;

    powerChart.update('none');
}
