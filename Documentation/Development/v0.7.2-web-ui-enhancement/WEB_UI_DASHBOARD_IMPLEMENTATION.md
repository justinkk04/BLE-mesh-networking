# v0.7.2 — Web UI Dashboard Enhancement Implementation Guide

**Date:** March 30, 2026
**Author:** Justin Kwarteng
**Purpose:** Feed this document to an agent to implement the BLE Mesh dashboard restructure and redesign.

> [!IMPORTANT]
> **Frontend-only changes.** No Python backend, WebSocket, or API modifications.
> All existing data flow (WebSocket messages, REST endpoints, SQLite) remains identical.
> The JS modules keep the same export interfaces — only their DOM rendering changes.

---

## 1. Current State (v0.7.1)

### File Inventory

```
gateway-pi5/dashboard/
├── index.html              ← Main HTML shell (136 lines)
├── css/
│   └── style.css           ← "Midnight Vercel" design system (1090 lines)
└── js/
    ├── app.js              ← WebSocket manager, tab nav, poll controls (285 lines)
    ├── topology.js         ← D3.js force-directed mesh graph (229 lines)
    ├── nodes.js            ← Node card grid with status badges (179 lines)
    ├── charts.js           ← Chart.js time-series (253 lines)
    └── console.js          ← Command console with history (80 lines)
```

### Current Tab Structure

```
Dashboard (active default)
├── #topology-section   — D3.js SVG graph (full width, ~40% height)
├── #pm-panel           — Power Manager sidebar (right column)
├── #nodes-section      — Node cards grid (2-col)
└── #console-section    — Console log + input (bottom)

Analytics
├── .chart-controls     — Metric tabs (Power/Voltage/Current) + time buttons
└── #charts-container   — Per-node Chart.js canvases

Settings
└── #settings-content   — Theme toggle + command reference text
```

### Current JS Module Interfaces (DO NOT CHANGE)

```javascript
// topology.js
export function init()                           // Sets up SVG + ResizeObserver
export function updateGraph(meshNodes, gatewayAddress)  // Rebuilds D3 graph

// nodes.js
export function init(sendCommandCallback)        // Stores command sender
export function updateNode(id, data)             // Creates/updates a card
export function getAllNodes()                     // Returns nodesData object

// charts.js
export function init()                           // Sets up metric/time listeners
export function addDataPoint(nodeId, data)        // Appends to chart
export function rebuildCharts(nodeIds)            // Rebuilds all charts from API

// console.js
export function init(sendCommandCallback)        // Sets up input + key handlers
export function addLog(message)                  // Appends log line to console

// app.js (internal, not imported by others)
// Manages WebSocket, tabs, poll controls, theme toggle
```

---

## 2. Phase 1: Layout Restructure

### 2.1 HTML Changes (`index.html`)

**Tab bar:** Add Console tab button between Analytics and Settings.

```html
<nav class="tab-bar">
    <button class="tab-btn active" data-tab="tab-dashboard">
        <span class="tab-icon">⬡</span> Dashboard
    </button>
    <button class="tab-btn" data-tab="tab-analytics">
        <span class="tab-icon">📊</span> Analytics
    </button>
    <button class="tab-btn" data-tab="tab-console">
        <span class="tab-icon">⌘</span> Console
    </button>
    <button class="tab-btn" data-tab="tab-settings">
        <span class="tab-icon">⚙</span> Settings
    </button>
</nav>
```

**Dashboard tab:** Topology + PM side-by-side in a top row, node cards below. No console.

```html
<div id="tab-dashboard" class="tab-content active">
    <!-- Top row: Topology + Power Manager side by side -->
    <div class="dashboard-top-row">
        <section id="topology-section" class="panel compact-panel">
            <h2>Mesh Topology</h2>
            <svg id="mesh-graph"></svg>
        </section>

        <aside id="pm-panel" class="panel compact-panel">
            <h2>Power Manager</h2>
            <div id="pm-content">
                <div class="stat-row">
                    <span>Status</span> <span id="pm-active" class="badge">Inactive</span>
                </div>
                <div class="stat-row">
                    <span>Threshold</span> <span id="pm-threshold">--</span>
                </div>
                <div class="stat-row">
                    <span>Budget (90%)</span> <span id="pm-budget">--</span>
                </div>
                <div class="stat-row">
                    <span>Total Power</span> <span id="pm-total">--</span>
                </div>
                <div class="stat-row">
                    <span>Priority Node</span> <span id="pm-priority">None</span>
                </div>
            </div>
        </aside>
    </div>

    <!-- Node cards -->
    <section id="nodes-section">
        <h2>Active Nodes</h2>
        <div id="node-cards"><!-- JS populated --></div>
    </section>
</div>
```

**Console tab:** Full-height dedicated console.

```html
<div id="tab-console" class="tab-content">
    <div id="console-tab-content">
        <div class="console-tab-header">
            <h2>Console Logs</h2>
        </div>
        <div id="console-log"></div>
        <div class="console-input-row">
            <span class="console-prompt">$</span>
            <input id="console-input" type="text" placeholder="Type a command and press Enter...">
        </div>
    </div>
</div>
```

**Settings tab:** Expanded with card groups for Appearance and PM Config.

```html
<div id="tab-settings" class="tab-content">
    <div id="settings-content">
        <h2>Settings</h2>

        <!-- Appearance -->
        <div class="settings-group">
            <h3 class="settings-group-title">🖥 Appearance</h3>
            <div class="settings-row">
                <div class="settings-label">
                    <span class="settings-label-text">Dark Mode</span>
                    <span class="settings-label-desc">Switch between dark and light theme</span>
                </div>
                <label class="theme-toggle" id="theme-toggle">
                    <input type="checkbox" id="theme-checkbox">
                    <span class="toggle-track">
                        <span class="toggle-thumb"></span>
                    </span>
                </label>
            </div>
        </div>

        <!-- PM Configuration -->
        <div class="settings-group">
            <h3 class="settings-group-title">⚡ Power Manager</h3>
            <div class="settings-row">
                <div class="settings-label">
                    <span class="settings-label-text">Threshold</span>
                    <span class="settings-label-desc">Total power budget in mW (0 = disabled)</span>
                </div>
                <span id="settings-pm-threshold">--</span>
            </div>
            <div class="settings-row">
                <div class="settings-label">
                    <span class="settings-label-text">Priority Node</span>
                    <span class="settings-label-desc">Node that gets preferential power allocation</span>
                </div>
                <span id="settings-pm-priority">None</span>
            </div>
        </div>

        <!-- Command Reference -->
        <div class="settings-group">
            <h3 class="settings-group-title">📖 Command Reference</h3>
            <div class="command-ref">
                <code>poll &lt;seconds&gt;</code> — Set poll interval<br>
                <code>poll stop</code> — Stop polling<br>
                <code>threshold &lt;mW&gt;</code> — Set PM threshold<br>
                <code>threshold off</code> — Disable PM<br>
                <code>priority &lt;node_id&gt;</code> — Set priority node<br>
                <code>help</code> — Show all commands
            </div>
        </div>
    </div>
</div>
```

### 2.2 Node Card Template Changes (`nodes.js`)

Replace the popover menu with inline controls. New `renderCard()` inner HTML:

```javascript
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
```

Wire click handlers using event delegation on `#node-cards` container:

```javascript
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
```

### 2.3 Console Module Update (`console.js`)

The console DOM now lives in `#tab-console` instead of `#console-section`. Update selectors:

```javascript
export function init(sendCommandCallback) {
    sendCmd = sendCommandCallback;
    logEl = document.getElementById('console-log');
    inputEl = document.getElementById('console-input');
    // ... rest of init unchanged
}
```

No other changes needed — the IDs (`console-log`, `console-input`) remain the same.

### 2.4 Topology Sizing (`topology.js`)

The topology container is now smaller. Adjust the D3 force simulation center and charge:

```javascript
// In updateGraph(), adjust simulation parameters for compact container
simulation = d3.forceSimulation(graphData.nodes)
    .force('link', d3.forceLink(graphData.links).id(d => d.id).distance(60))  // was 80
    .force('charge', d3.forceManyBody().strength(-150))  // was -200
    .force('center', d3.forceCenter(width / 2, height / 2))
    .alphaDecay(0.05);
```

---

## 3. Phase 2: Visual Redesign
**Design Reference:** Use the Stitch mockups for VISUAL AESTHETIC ONLY:
- `Documentation/Development/v0.7.2-web-ui-enhancement/stitch_analytics_redesign/screen.png` — Settings styling
- `Documentation/Development/v0.7.2-web-ui-enhancement/stitch_analytics_redesign (1)/screen.png` — Dashboard styling
- `Documentation/Development/v0.7.2-web-ui-enhancement/stitch_analytics_redesign (2)/screen.png` — Analytics styling

Extract ONLY these visual patterns from the mockups:
1. Color palette: near-black bg (#09090b), violet primary (#a78bfa), emerald green accents (#34d399), zinc grays for surfaces
2. Typography: Geist or Inter/JetBrains Mono equivalent, tight letter-spacing on headings, uppercase tiny labels
3. Card styling: dark surface bg, thin #27272a borders, 8px radius
4. Node card layout: large monospace metric values, uppercase dim labels, 2x2 metric grid
5. Chart area: dark backgrounds with subtle dot-grid pattern, taller containers
6. Metric/time tab buttons: pill-style with active highlight
7. Settings: dark mode pill toggle, card-based groups
8. Console `$` prompt styling

> [!CAUTION]
> DO NOT implement any of the following ghost features from the mockups — 
> they have NO backend support and will create broken, non-functional UI:
> - Left sidebar navigation on Settings
> - System Configuration sliders (Scan Frequency, Replication Delay)
> - API & Keys Management section
> - Maintenance section (Update Firmware, Factory Reset)
> - Display Density selector (Compact/Standard/Comfort)
> - Power Manager gauge ring / efficiency bar / threshold sliders / "Apply Constraints" button
> - Stat cards (Efficiency, Anomalies, Sync Latency, Log Storage)
> - "LIVE" badges, stability indices, spectral analysis text
> - User profile avatar
> - Mobile bottom navigation bar
> - Hero image / decorative footer
> - "Save Changes" / "Cancel" action bar
>
> The backend APIs and WebSocket messages are FIXED. Only restyle existing elements.


### 3.1 Design Tokens

New CSS custom properties (replacing v0.7.1 values):

```css
:root {
    /* Backgrounds — deeper darks */
    --bg-primary: #0a0a0f;
    --bg-secondary: #0e0e16;
    --bg-card: rgba(15, 18, 30, 0.75);
    --bg-elevated: rgba(22, 26, 42, 0.9);
    --bg-surface: rgba(255, 255, 255, 0.04);
    --bg-console: #0d1117;

    /* Borders */
    --border: rgba(255, 255, 255, 0.06);
    --border-hover: rgba(255, 255, 255, 0.12);

    /* Text — higher contrast */
    --text-primary: #e8ecf4;
    --text-secondary: #8b949e;
    --text-dim: #484f58;
    --text-bright: #ffffff;

    /* Accents — subtle purple (from Stitch) */
    --accent-primary: hsl(270, 60%, 60%);
    --accent-primary-glow: hsla(270, 60%, 60%, 0.15);
    --accent-green: hsl(145, 65%, 50%);
    --accent-orange: hsl(35, 85%, 55%);
    --accent-red: hsl(5, 90%, 60%);
    --accent-cyan: hsl(185, 65%, 55%);

    /* Typography — larger base */
    --font: 'Inter', -apple-system, system-ui, sans-serif;
    --mono: 'JetBrains Mono', 'Consolas', monospace;
    --text-xs: 0.75rem;
    --text-sm: 0.85rem;
    --text-base: 1rem;
    --text-lg: 1.25rem;
    --text-xl: 1.5rem;
    --text-2xl: 2rem;

    /* Spacing */
    --radius: 12px;
    --radius-sm: 8px;
    --gap: 1rem;
    --gap-lg: 1.5rem;
}
```

### 3.2 Key Styling Rules

**Node cards — big readable metrics:**

```css
.node-card {
    background: var(--bg-card);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: var(--gap-lg);
    backdrop-filter: blur(16px);
}

.metric-value {
    font-family: var(--mono);
    font-size: var(--text-xl);    /* 1.5rem — large and readable */
    font-weight: 600;
    color: var(--text-bright);
}

.metric-label {
    font-size: var(--text-xs);
    text-transform: uppercase;
    letter-spacing: 1px;
    color: var(--text-dim);
}
```

**Duty bar — prominent progress indicator:**

```css
.duty-bar {
    flex: 1;
    height: 6px;
    background: var(--bg-surface);
    border-radius: 3px;
    overflow: hidden;
}

.duty-fill {
    height: 100%;
    background: var(--accent-primary);
    border-radius: 3px;
    transition: width 0.4s ease;
}

.duty-pct {
    font-family: var(--mono);
    font-size: var(--text-sm);
    color: var(--accent-primary);
    min-width: 3rem;
    text-align: right;
}
```

**Console tab — terminal aesthetic:**

```css
#console-tab-content {
    display: flex;
    flex-direction: column;
    height: calc(100vh - 140px);  /* full height minus header + tabs + footer */
}

#console-log {
    flex: 1;
    background: var(--bg-console);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 1rem;
    font-family: var(--mono);
    font-size: var(--text-sm);
    overflow-y: auto;
    line-height: 1.8;
}

.console-input-row {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    padding: 0.75rem 0;
}

.console-prompt {
    font-family: var(--mono);
    color: var(--accent-primary);
    font-weight: 600;
}
```

**Inline node controls:**

```css
.node-controls {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    padding-top: 0.75rem;
    border-top: 1px solid var(--border);
    margin-top: 0.75rem;
}

.duty-input {
    width: 4rem;
    background: var(--bg-surface);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    padding: 0.35rem 0.5rem;
    font-family: var(--mono);
    font-size: var(--text-sm);
    color: var(--text-primary);
}

.btn-set, .btn-read {
    padding: 0.35rem 0.75rem;
    border-radius: var(--radius-sm);
    border: 1px solid var(--border);
    background: var(--bg-surface);
    color: var(--text-primary);
    font-size: var(--text-sm);
    cursor: pointer;
    transition: background 0.2s, border-color 0.2s;
}

.btn-read {
    background: var(--accent-primary-glow);
    border-color: var(--accent-primary);
    color: var(--accent-primary);
}
```

**Dashboard top row (topology + PM side by side):**

```css
.dashboard-top-row {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: var(--gap);
}

@media (max-width: 768px) {
    .dashboard-top-row {
        grid-template-columns: 1fr;
    }
}
```

**Settings card groups:**

```css
.settings-group {
    background: var(--bg-card);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: var(--gap-lg);
    margin-bottom: var(--gap);
}

.settings-group-title {
    font-size: var(--text-sm);
    font-weight: 600;
    color: var(--text-secondary);
    text-transform: uppercase;
    letter-spacing: 1px;
    margin-bottom: 1rem;
    padding-bottom: 0.75rem;
    border-bottom: 1px solid var(--border);
}
```

**Theme toggle — pill switch:**

```css
.toggle-track {
    width: 48px;
    height: 24px;
    background: var(--bg-surface);
    border: 1px solid var(--border);
    border-radius: 9999px;
    position: relative;
    cursor: pointer;
    transition: background 0.3s;
}

.toggle-thumb {
    width: 18px;
    height: 18px;
    background: var(--text-primary);
    border-radius: 50%;
    position: absolute;
    top: 2px;
    left: 3px;
    transition: transform 0.3s cubic-bezier(0.4, 0, 0.2, 1);
}

.theme-toggle input:checked + .toggle-track {
    background: var(--accent-primary);
}

.theme-toggle input:checked + .toggle-track .toggle-thumb {
    transform: translateX(23px);
}
```

### 3.3 Chart.js Updates (`charts.js`)

Update chart options for taller containers and larger labels:

```javascript
const chartOptions = {
    responsive: true,
    maintainAspectRatio: false,  // Let CSS control height
    plugins: {
        legend: {
            labels: { font: { size: 13 } }
        },
        tooltip: {
            titleFont: { size: 13 },
            bodyFont: { size: 12, family: "'JetBrains Mono', monospace" }
        }
    },
    scales: {
        x: {
            ticks: { font: { size: 11 } },
            grid: { color: 'rgba(255,255,255,0.04)' }
        },
        y: {
            ticks: { font: { size: 11 } },
            grid: { color: 'rgba(255,255,255,0.04)' }
        }
    }
};
```

Set chart container CSS height:

```css
#charts-container canvas {
    height: 300px !important;  /* Taller charts */
}
```
