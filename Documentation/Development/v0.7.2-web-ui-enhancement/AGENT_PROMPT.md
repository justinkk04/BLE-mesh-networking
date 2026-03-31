# v0.7.2 Web UI Dashboard Enhancement — Agent Prompts

> **Instructions:** Use these prompts one at a time, in order.
> Each phase is independently testable — verify it passes before moving to the next.

---

## Phase 1 Prompt (Layout Restructure — HTML + JS)

```
You are implementing Phase 1 of the BLE Mesh Dashboard Enhancement (v0.7.2).

REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Restructure the dashboard layout — move the console to its own tab, add inline controls to node cards, and compact the topology + PM sections. No visual/CSS changes yet, just structural.

**Pre-requisites:**
- v0.7.1 dashboard is complete and working (all features functional)
- The gateway backend is UNCHANGED — no Python modifications

**Context:** Read these files FIRST:
- `Documentation/Development/v0.7.2-web-ui-enhancement/WEB_UI_DASHBOARD_PLAN.md` — Phase 1 section
- `Documentation/Development/v0.7.2-web-ui-enhancement/WEB_UI_DASHBOARD_IMPLEMENTATION.md` — Section 2 (Phase 1 details)

Then read the existing frontend:
- `gateway-pi5/dashboard/index.html` — Current HTML structure
- `gateway-pi5/dashboard/js/app.js` — Tab navigation system
- `gateway-pi5/dashboard/js/nodes.js` — Node card rendering
- `gateway-pi5/dashboard/js/console.js` — Console DOM selectors
- `gateway-pi5/dashboard/js/topology.js` — D3 force parameters

**Tech Stack:** Vanilla HTML/CSS/JavaScript, D3.js v7 (CDN), Chart.js v4 (CDN)

---

### CRITICAL: Frontend-Only Changes

> No Python backend changes. No new WebSocket message types. No new API endpoints.
> The JS modules keep the same export interfaces — only their DOM rendering changes.
> Existing `app.js` tab system automatically handles new `data-tab` buttons.

### CRITICAL: DO NOT break existing functionality

> All existing features must continue working after restructure:
> - WebSocket connection + auto-reconnect
> - Real-time node card updates
> - D3 topology graph
> - Chart.js analytics
> - Console command input + log output
> - Theme toggle
> - Poll controls

---

### Task 1: Restructure `index.html`

**File:** `gateway-pi5/dashboard/index.html`

1. Add Console tab button to the tab bar (between Analytics and Settings)
2. Move `#console-section` out of `#tab-dashboard` into new `#tab-console` div
3. Wrap topology + PM panel in a `.dashboard-top-row` grid container
4. Update node card structure in `#nodes-section` (inline controls will be rendered by JS)
5. Expand Settings tab with card groups (Appearance, PM Config, Command Reference)

See Section 2.1 of IMPLEMENTATION.md for exact HTML.

**Verify:** Open page → 4 tabs visible. Console tab shows log + input. Dashboard has no console section.

---

### Task 2: Update `nodes.js` — Inline Controls

**File:** `gateway-pi5/dashboard/js/nodes.js`

1. Replace `renderCard()` HTML template with new structure:
   - Large metric triad (Power | Voltage | Current)
   - Duty cycle bar with percentage
   - Inline controls: duty % input + "Set" button + "Read" button
2. Remove the ⚙️ popover menu system entirely
3. Add event delegation on `#node-cards` container for `[data-action]` buttons
4. Keep all existing exports unchanged: `init()`, `updateNode()`, `getAllNodes()`

See Section 2.2 of IMPLEMENTATION.md for exact template and event handler.

**Verify:** Node cards show inline Set/Read buttons. Click Read → sensor data updates. Set duty → command executes.

---

### Task 3: Update `console.js` — Selector Changes

**File:** `gateway-pi5/dashboard/js/console.js`

Update DOM selectors if the console HTML structure changed. The element IDs
(`console-log`, `console-input`) stay the same, so this may need no changes at all.

**Verify:** Switch to Console tab → type command → output appears.

---

### Task 4: Update `topology.js` — Compact Sizing

**File:** `gateway-pi5/dashboard/js/topology.js`

Adjust D3 force simulation parameters for the smaller container:
- Reduce link distance (80 → 60)
- Reduce charge strength (-200 → -150)

**Verify:** Topology graph fits in compact card without nodes flying off-screen.

---

### Task 5: End-to-End Verification

1. Start `python gateway.py --web` (with BLE nodes connected)
2. Open `http://localhost:8000`
3. **Dashboard tab:** Topology + PM side by side on top, node cards below with inline controls
4. **Analytics tab:** Charts work as before
5. **Console tab:** Full-height log, command input at bottom, can send commands
6. **Settings tab:** Theme toggle, PM info, command reference
7. Node card "Read" button → triggers read → metrics update
8. Node card "Set Duty" → sends command → node responds
9. WebSocket reconnect still works (kill/restart gateway)

```

---

## Phase 2 Prompt (Visual Redesign — CSS)

```
You are implementing Phase 2 of the BLE Mesh Dashboard Enhancement (v0.7.2).

REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Restyle the dashboard with a premium "command center" aesthetic — bigger text, better spacing, polished cards, terminal-style console. Match the Stitch mockup aesthetic while keeping all existing functionality.

**Pre-requisite:** Phase 1 (layout restructure) is complete and verified — 4 tabs working, inline controls on cards.

**Context:** Read these files FIRST:
- `Documentation/Development/v0.7.2-web-ui-enhancement/WEB_UI_DASHBOARD_PLAN.md` — Phase 2 section
- `Documentation/Development/v0.7.2-web-ui-enhancement/WEB_UI_DASHBOARD_IMPLEMENTATION.md` — Section 3 (Phase 2 details)

Then read the current frontend:
- `gateway-pi5/dashboard/index.html` — Phase 1 HTML structure
- `gateway-pi5/dashboard/css/style.css` — Current "Midnight Vercel" CSS
- `gateway-pi5/dashboard/js/charts.js` — Chart.js configuration

---

### CRITICAL: Design Requirements

> The dashboard MUST feel premium and professional. Follow these rules:
>
> 1. **Bigger text** — metric values at 1.5rem+, labels uppercase 0.75rem
> 2. **More whitespace** — generous padding inside cards, gap between sections
> 3. **Metric numbers in monospace** — JetBrains Mono for all data values
> 4. **Duty bar prominent** — colored progress bar with percentage aligned right
> 5. **Console = terminal** — dark bg (#0d1117), monospace, full height
> 6. **Subtle purple accents** — hsl(270, 60%, 60%) instead of blue
> 7. **Smooth transitions** — all hover/state changes use 0.2-0.3s ease
> 8. **Theme toggle = pill switch** — clean toggle, no moon/sun emojis
> 9. **Keep dark/light themes** — light mode must also look polished
> 10. **No build step** — vanilla CSS, no Tailwind, no preprocessors

---

### Task 1: Rewrite `style.css` Design Tokens

**File:** `gateway-pi5/dashboard/css/style.css`

Replace the `:root` design tokens with the v0.7.2 values from IMPLEMENTATION.md Section 3.1.
Key changes: purple accent, larger font tokens, console background color, more spacing variables.

---

### Task 2: Restyle Node Cards

**File:** `gateway-pi5/dashboard/css/style.css`

Apply new card styles from IMPLEMENTATION.md Section 3.2:
- `.node-card` with generous padding and blur backdrop
- `.metric-value` at 1.5rem monospace bold
- `.metric-label` uppercase, letter-spaced, dim
- `.duty-bar` + `.duty-fill` progress bar
- `.node-controls` with inline button styling
- `.btn-read` with accent border glow

---

### Task 3: Style Console Tab

**File:** `gateway-pi5/dashboard/css/style.css`

Apply console styles from IMPLEMENTATION.md Section 3.2:
- `#console-tab-content` full viewport height
- `#console-log` dark terminal background
- `.console-input-row` with `$` prompt
- Monospace font, generous line-height

---

### Task 4: Style Dashboard Top Row and Settings

**File:** `gateway-pi5/dashboard/css/style.css`

- `.dashboard-top-row` 2-column grid (stacks on mobile)
- `.settings-group` card styling
- `.toggle-track` + `.toggle-thumb` pill switch
- Responsive breakpoints at 768px and 1200px

---

### Task 5: Update Chart.js Options

**File:** `gateway-pi5/dashboard/js/charts.js`

Update chart options for taller containers: `maintainAspectRatio: false`, larger font sizes for labels and tooltips. See IMPLEMENTATION.md Section 3.3.

---

### Task 6: Visual Verification

1. Open `http://localhost:8000`
2. **Dashboard:** Node cards have large, readable metric numbers with duty bar
3. **Analytics:** Charts are taller with readable axis labels
4. **Console:** Full-height dark terminal, `$` prompt, monospace text
5. **Settings:** Clean card groups, pill toggle switch
6. **Resize to phone width:** Everything stacks cleanly
7. **Toggle dark/light mode:** Both themes look polished
8. **Hard refresh (Ctrl+Shift+R)** to clear CSS cache

```

---

## CHANGELOG Template

After completing both phases, update `Documentation/Development/v0.7.2-web-ui-enhancement/CHANGELOG.md`:

```markdown
## [v0.7.2] - 2026-XX-XX

### Dashboard Enhancement — "Command Center" Redesign

**Frontend-only changes. No backend modifications.**

- **Layout Restructure (Phase 1)**
  - Added dedicated Console tab — freed Dashboard from cramped console section
  - New 4-tab navigation: Dashboard · Analytics · Console · Settings
  - Node cards redesigned with inline duty input and Read button (removed popover menu)
  - Topology and Power Manager compacted into side-by-side cards
  - Settings expanded with card-based Appearance and PM Configuration groups

- **Visual Redesign (Phase 2)**
  - New design system with larger typography (1.5rem metric values), generous spacing, and purple accent
  - Node cards feature monospace metric triad (Power | Voltage | Current) and prominent duty bar
  - Console tab styled as full-height terminal with dark background and $ prompt
  - Theme toggle simplified to pill switch
  - Charts given more vertical space with larger labels
  - Responsive breakpoints refined for phone/tablet/desktop
```
