# MESH_DASHBOARD_PLAN Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Radically improve the BLE Mesh Dashboard UI smoothing and aesthetic, delivering a premium feel without optimistic faking. The UI should smoothly transition states and expose all Gateway TUI features (commands, reads, etc.) directly on the node cards via pop-ups/editors, while ensuring reliable backend console integration.

**Architecture:**

1. **Aesthetics**: Overhaul `style.css` with modern web design (vibrant colors, glassmorphism, dynamic animations).
2. **Smoothing**: Eliminate D3 force graph jitter on pure data updates.
3. **Smooth Transitions**: Use CSS transitions for node states (online/stale/offline) to avoid choppy visual snaps.
4. **Interactive Node Cards**: Add pop-up menus/inline forms on Node cards to directly send commands (duty, read, stop) without needing the console tab, mimicking the flexibility of the TUI. Wait for real confirmation before updating the metrics.
5. **Console Reliability**: Fix the race condition/fighting between the Gateway TUI and the Dashboard Console by ensuring real command outcomes are reported back.

**Tech Stack:** Vanilla CSS, Vanilla JavaScript, D3.js, Python/Flask.

---

### Task 1: Premium CSS Overhaul & Smooth Offline Transitions

**Files:**

- Modify: `gateway-pi5/dashboard-c/static/style.css`

**Step 1: Write the updated CSS styling**
Overhaul `:root` variables to a deep, premium dark theme. Add glassmorphism to cards. Add smooth CSS transitions to background colors and borders so that when a node drops offline, the visual change is smooth. Ensure hover menus/popovers are styled elegantly for Task 3.

```css
/* Replace the root variables */
:root {
    --bg-primary: #09090b;
    --bg-secondary: #0f0f13;
    --bg-card: rgba(28, 35, 51, 0.6);
    --bg-elevated: rgba(33, 40, 59, 0.8);
    --border: rgba(255, 255, 255, 0.1);
    --border-light: rgba(255, 255, 255, 0.2);
    /* ... keep other colors ... */
}

/* Add glassmorphism and transition to node cards */
.node-card {
    background: var(--bg-card);
    backdrop-filter: blur(8px);
    -webkit-backdrop-filter: blur(8px);
    border: 1px solid var(--border);
    transition: transform 0.2s cubic-bezier(0.175, 0.885, 0.32, 1.275), border-color 0.4s ease, background-color 0.4s ease;
    position: relative; /* For absolutely positioned popovers */
}
.node-card:hover {
    transform: translateY(-2px);
    border-color: var(--border-light);
}

/* Smooth transitions for badges */
.node-status {
    transition: background-color 0.4s ease, color 0.4s ease;
}

/* Command Menu Popover styling */
.node-command-menu {
    position: absolute;
    top: 100%;
    right: 0;
    z-index: 50;
    background: var(--bg-elevated);
    border: 1px solid var(--border-light);
    border-radius: 8px;
    padding: 0.5rem;
    display: none; /* toggled via JS */
    box-shadow: 0 10px 15px -3px rgba(0, 0, 0, 0.5);
    backdrop-filter: blur(12px);
}
.node-command-menu.show {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
}
.node-command-menu button {
    background: transparent;
    border: none;
    color: var(--text-primary);
    text-align: left;
    padding: 0.4rem 0.8rem;
    border-radius: 4px;
    cursor: pointer;
    transition: background 0.2s;
}
.node-command-menu button:hover {
    background: rgba(255,255,255,0.1);
}
```

**Step 2: Run test to verify it passes**
Run: `cd gateway-pi5/dashboard-c && python dashboard.py --mock`
Target: Dashboard opens at port 5555. UI looks premium.

**Step 3: Commit**

```bash
git add gateway-pi5/dashboard-c/static/style.css
git commit -m "style: premium glassmorphism aesthetic and smooth state transitions"
```

### Task 2: Fix D3 Jitter on Data Updates

**Files:**

- Modify: `gateway-pi5/dashboard-c/static/dashboard.js`

**Step 1: Write minimal implementation to prevent jitter**
Stop the D3 simulation from aggressively restarting on pure data updates. Modify `updateGraph(state)`.

```javascript
// Replace the block around line 403 in updateGraph():
    // Only restart simulation when topology changes (node added/removed)
    // Data-only updates should NOT jitter the graph
    if (changed) {
        simulation.alpha(0.3).restart(); // gentle restart, not 1.0
    }
```

**Step 2: Run test to verify it passes**
Run: `cd gateway-pi5/dashboard-c && python dashboard.py --mock`
Target: The graph should NOT jump around violently every 2 seconds. Node metric values should update smoothly using `animateValue`.

**Step 3: Commit**

```bash
git add gateway-pi5/dashboard-c/static/dashboard.js
git commit -m "fix(ui): prevent D3 graph jitter on data updates"
```

### Task 3: Interactive Node Cards (Inline Commands)

**Files:**

- Modify: `gateway-pi5/dashboard-c/static/dashboard.js`

**Step 1: Implement Pop-up Command Menus on Node Cards**
Instead of optimistic faking, add a "⚙️" or "⋯" button to each Node Card header. Clicking it opens a small context menu (`.node-command-menu`) allowing the user to select commands to send (e.g., Set Duty, Force Read, Stop). This sends an API request to the backend. The UI only updates the actual metrics when the true BLE response dictates changes in the state file.

```javascript
// Example addition to node card HTML generation in updateNodeCards:
html += `
<div class="node-card" data-nid="${nid}">
    <div class="node-card-header">
        <span class="node-name">${nodeLabel(nid)}</span>
        <div style="display: flex; align-items: center; gap: 0.5rem;">
            <span class="node-status ${st}">${stLabel}</span>
            <button class="menu-trigger" onclick="toggleMenu(event, '${nid}')">⋯</button>
        </div>
    </div>
    <div class="node-card-metrics">...</div>
    
    <!-- Pop-up Menu -->
    <div class="node-command-menu" id="menu-${nid}">
        <button onclick="sendInlineCommand('${nid}', 'read')">Force Read</button>
        <button onclick="promptDuty('${nid}')">Set Duty %</button>
        <button onclick="sendInlineCommand('${nid}', 'stop')" style="color: var(--accent-red)">Stop Node</button>
    </div>
</div>`;

// Add simple JS handlers
window.toggleMenu = function(e, nid) {
    e.stopPropagation();
    document.querySelectorAll('.node-command-menu').forEach(m => {
        if (m.id !== `menu-${nid}`) m.classList.remove('show');
    });
    document.getElementById(`menu-${nid}`).classList.toggle('show');
};

window.sendInlineCommand = function(nid, action, value='') {
    const cmd = value ? `node ${nid} ${action} ${value}` : `node ${nid} ${action}`;
    fetch('/api/command', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({command: cmd})
    }).then(r => r.json()).then(resp => {
        // UI naturally syncs on the next polling cycle when actual confirmation returns
        if(resp.error) console.error(resp.error);
    });
    document.getElementById(`menu-${nid}`).classList.remove('show');
};

window.promptDuty = function(nid) {
    const d = prompt(`Set target duty (0-100) for Node ${nid}:`);
    if (d !== null && !isNaN(parseInt(d))) {
        sendInlineCommand(nid, 'duty', parseInt(d));
    }
}
```

**Step 2: Commit**

```bash
git add gateway-pi5/dashboard-c/static/dashboard.js
git commit -m "feat(ui): add inline command menus to node cards"
```

### Task 4: Reliable Console Tab Integration

**Goal:** Currently, the console writes to `mesh_commands.json` and `gateway.py`'s TUI worker marks it as "Forwarded" instantly. We need to await the actual BLE mesh response before writing to `mesh_response.json` so the dashboard knows if it *actually* succeeded or timed out.

**Files:**

- Modify: `gateway-pi5/gateway.py`

**Step 1: Make `_check_dashboard_commands` wait for real command completion**
Instead of calling `self.app.dispatch_command()` (which returns instantly), parse the command in `_check_dashboard_commands` and `await self.send_to_node()`. Only write `mesh_response.json` once the node acks (or times out).

```python
# In gateway.py `_check_dashboard_commands`:
# Parse cmd_text manually (e.g. if cmd_text.startswith("node ")).
# Then:
# success = await self.send_to_node(target_node, action)
# response = "Executed" if success else "Failed/Timeout"
# Write to mesh_response.json
```

*(Exact implementation details left to the executing agent. Ensure it does not block the main polling loop.)*

**Step 2: Run test to verify it passes**
Run: `python gateway.py` and in another terminal `python dashboard.py`
Send a command in the dashboard console or via the new Node Card inline menu to an offline node. It should hang for a few seconds, then report "Failed/Timeout" without faking the response.

**Step 3: Commit**

```bash
git add gateway-pi5/gateway.py
git commit -m "fix(backend): make dashboard console wait for actual mesh acks"
```
