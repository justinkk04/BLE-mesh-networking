# v0.7.2 Web UI Dashboard Enhancement Plan

**Date:** March 30, 2026
**Author:** Justin Kwarteng
**Status:** Draft — Awaiting Review

---

## 1. Problem Statement

The v0.7.1 dashboard is functional but has significant UX issues:

- **Text is too small** — metrics on node cards are hard to read at a glance
- **Console wastes space** — sits at the bottom of the Dashboard tab, cramped and rarely used during monitoring
- **Topology dominates** — the D3.js graph takes the entire top-left but shows only 2-3 nodes
- **Power Manager is oversized** — a full sidebar panel for 5 stat rows
- **Analytics charts are compressed** — low vertical height makes the chart lines nearly unreadable
- **Settings tab is empty** — only a theme toggle and command reference text

### What's Working (Keep)

- ✅ WebSocket real-time data push (no changes needed)
- ✅ D3.js topology graph (just needs better sizing)
- ✅ Chart.js analytics with power/voltage/current tabs and time windows
- ✅ Node cards with status badges (online/stale/offline)
- ✅ Console with command history and auto-scroll
- ✅ Theme toggle (dark/light mode)
- ✅ Poll controls in header
- ✅ All backend APIs (`web_server.py`, `db.py`, `dc_gateway.py`)

### What Changes (Layout + Styling Only)

No backend modifications. No new WebSocket message types. No new API endpoints. Just restructure and restyle the existing frontend.

## 2. Goal

**Restructure and restyle the existing dashboard** to match a premium "command center" aesthetic:

1. **Move console to its own tab** — frees dashboard space, gives console room to breathe
2. **Make node cards bigger** — large readable metrics with inline duty control and read button
3. **Shrink topology** — compact card instead of full-width section
4. **Compact Power Manager** — slim card, not a sidebar panel
5. **Give charts more height** — full-width, taller charts on Analytics tab
6. **Expand Settings** — proper card-based layout with Appearance + PM configuration
7. **Better typography** — larger font sizes, more whitespace, better contrast

### Tab Structure

```
Before (v0.7.1):  Dashboard · Analytics · Settings
After  (v0.7.2):  Dashboard · Analytics · Console · Settings
```

### Target Layout — Dashboard Tab

```
┌──────────────────────────────────────────────────────────────┐
│  DC Monitor Mesh    Dashboard  Analytics  Console  Settings  │
│                                          POLL [2] s [STOP]   │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌─────────────────────────┐  ┌────────────────────────────┐ │
│  │     MESH TOPOLOGY       │  │      POWER MANAGER         │ │
│  │                         │  │  Status: BALANCED           │ │
│  │   (compact D3 graph)    │  │  Total: 324.5 mW            │ │
│  │                         │  │  Threshold: 500 mW          │ │
│  │                         │  │  Budget: 175.5 remaining     │ │
│  └─────────────────────────┘  └────────────────────────────┘ │
│                                                              │
│  ACTIVE NODES                                                │
│  ┌──────────────────────────┐  ┌──────────────────────────┐  │
│  │  Node 0        [ONLINE]  │  │  Node 1        [ONLINE]  │  │
│  │                          │  │                          │  │
│  │  POWER    VOLTAGE  CURR  │  │  POWER    VOLTAGE  CURR  │  │
│  │  156 mW   12.28V  2.5mA │  │  168 mW   12.24V  2.7mA │  │
│  │                          │  │                          │  │
│  │  DUTY CYCLE ████░░ 12.5% │  │  DUTY CYCLE █████░ 14.2% │  │
│  │  [Set Duty: ___]  [Read] │  │  [Set Duty: ___]  [Read] │  │
│  └──────────────────────────┘  └──────────────────────────┘  │
│                                                              │
├──────────────────────────────────────────────────────────────┤
│  2 nodes active                    WebSocket: Connected      │
└──────────────────────────────────────────────────────────────┘
```

### Target Layout — Console Tab

```
┌──────────────────────────────────────────────────────────────┐
│  Console Logs                                                │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐  │
│  │ [10:16:51] System ready. Type 'help' for commands.     │  │
│  │ [10:16:51] Connection follow...                        │  │
│  │ [10:16:54] Connecting to ESP-BLE-MESH...               │  │
│  │ [10:16:55] Connected to Node 0 (GATT gateway)          │  │
│  │ [10:17:01] READ → Node 0: V=12.28, I=2.50, P=156.00   │  │
│  │ [10:17:01] READ → Node 1: V=12.24, I=2.75, P=168.00   │  │
│  │                                                        │  │
│  │                                                        │  │
│  │                                                        │  │
│  └────────────────────────────────────────────────────────┘  │
│  $ [Type a command and press Enter...]                    ↵  │
└──────────────────────────────────────────────────────────────┘
```

## 3. Scope — Frontend Only

> [!IMPORTANT]
> This is a **frontend-only** enhancement. No Python backend changes.
> All existing WebSocket messages, REST APIs, and data structures remain identical.
> The JS modules keep the same exports/interfaces — only their DOM rendering changes.

### Files Changed

| File | Change |
|------|--------|
| `gateway-pi5/dashboard/index.html` | **[MOD]** Restructure HTML: add Console tab, move console markup, redesign node card structure with inline controls |
| `gateway-pi5/dashboard/css/style.css` | **[REWRITE]** New design system: bigger typography, better spacing, redesigned cards, console tab styling |
| `gateway-pi5/dashboard/js/app.js` | **[MOD]** Add Console tab switching logic (existing `initTabs()` handles it) |
| `gateway-pi5/dashboard/js/nodes.js` | **[MOD]** Update `renderCard()` to use new HTML structure with inline duty input + read button |
| `gateway-pi5/dashboard/js/console.js` | **[MOD]** Update selectors if console DOM structure changes |
| `gateway-pi5/dashboard/js/topology.js` | **[MINOR]** Adjust sizing constants for compact container |
| `gateway-pi5/dashboard/js/charts.js` | **[MINOR]** Adjust chart height/options for taller containers |

### Files NOT Changed

| File | Reason |
|------|--------|
| `gateway-pi5/gateway-code/web_server.py` | No API changes |
| `gateway-pi5/gateway-code/dc_gateway.py` | No backend changes |
| `gateway-pi5/gateway-code/db.py` | No schema changes |
| `gateway-pi5/gateway-code/power_manager.py` | No PM changes |
| `gateway-pi5/gateway-code/gateway.py` | No CLI changes |

## 4. Two-Phase Approach

> [!IMPORTANT]
> Each phase is independently testable. Complete and verify each phase before starting the next.

---

### Phase 1: Layout Restructure (HTML + JS)

**Scope:** Restructure the HTML layout and update JS modules to match. No visual changes yet — just move things to the right places.

**What changes:**

| File | Change |
|------|--------|
| `index.html` | Add `tab-console` tab button. Move `#console-section` from Dashboard tab into new `#tab-console` div. Restructure node cards to include inline duty input and read button. Compact topology + PM into side-by-side cards. |
| `app.js` | No code changes needed — existing tab system handles any `data-tab` button. Just ensure Console tab ID matches. |
| `nodes.js` | Update `renderCard()` HTML template: add inline `<input type="number">` for duty and a "Read" `<button>`. Wire click handlers via `sendCmdFunc`. Remove ⚙️ popover menu (replaced by inline controls). |
| `console.js` | Update `querySelector` if `#console-section` ID or structure changes. |
| `topology.js` | Reduce default force simulation dimensions to fit compact container. |

**Risk:** 🟢 Low — moving DOM elements, no logic changes
**Effort:** ~200 lines of HTML/JS changes

#### How to Test Phase 1

1. Start `python gateway.py --web`
2. Open `http://localhost:8000`
3. Verify 4 tabs visible: Dashboard, Analytics, Console, Settings
4. Dashboard tab: topology + PM side by side, node cards below with inline controls
5. Console tab: full-height log area with command input
6. Click "Read" button on a node card → sensor data updates
7. Set duty via inline input on card → command executes
8. Analytics and Settings tabs unchanged

**Phase 1 is DONE when:** All 4 tabs work, console has its own tab, node cards have inline controls, no console on the Dashboard tab.

---

### Phase 2: Visual Redesign (CSS)

**Scope:** Restyle everything to match the "command center" aesthetic from the Stitch mockups. Bigger text, better spacing, premium feel.

**What changes:**

| File | Change |
|------|--------|
| `style.css` | Full CSS rewrite. New design tokens (larger font sizes, more generous spacing). Redesigned node cards (bigger metric numbers, prominent duty bar). Compact topology card. Console tab styling (full-height, monospace, colored log levels). Settings card layout. |
| `charts.js` | Update Chart.js options: taller aspect ratio, larger axis labels, thicker lines. |

**Key design decisions:**

- **Font sizes:** Base 16px, metric numbers 1.5rem+, labels 0.75rem uppercase
- **Node cards:** 2-column grid, generous padding, metric triad (Power | Voltage | Current) in large monospace, duty bar with percentage, inline controls at bottom
- **Topology:** Contained in a ~300px tall card, D3 graph auto-fits
- **Power Manager:** Compact horizontal card with key stats in a row
- **Console:** Dark terminal background (`#0d1117`), monospace font, each log line with timestamp column, command input with `$` prefix
- **Settings:** Card-based groups (Appearance + PM Configuration), dark mode toggle as a proper pill switch

**Risk:** 🟢 Low — CSS-only changes, no logic
**Effort:** ~400 lines of CSS

#### How to Test Phase 2

1. Open `http://localhost:8000` — visually compare against the Stitch mockups
2. Dashboard: node metrics should be large and readable at arm's length
3. Console tab: logs should feel like a real terminal
4. Settings: theme toggle should be a clean pill switch
5. Resize browser window — responsive breakpoints at 768px and 1200px
6. Toggle dark/light mode — both themes should look polished

**Phase 2 is DONE when:** The dashboard looks premium and matches the Stitch mockup aesthetic. Text is large, spacing is generous, cards feel professional.

---

## 5. Design Inspiration — What We Take vs. What We Skip

### ✅ Take from Stitch Mockups

| Element | How We Apply It |
|---------|----------------|
| Horizontal tab bar (Dashboard · Analytics · Console · Settings) | Direct match — our new tab structure |
| Big metric numbers on node cards (156 mW, 12.28 V) | Enlarge our existing metrics display |
| Duty cycle progress bar with percentage | We already have power bars, make them more prominent |
| Console as full tab with table-style log formatting | Move existing console, add timestamp columns |
| Settings with card groups (Appearance, Configuration) | Expand our Settings tab |
| Dark theme with subtle purple accents | Refine our existing "Midnight Vercel" palette |
| Status badges (ONLINE / STABLE) | We already have these, just restyle |

### ❌ Skip from Stitch Mockups (not in our codebase)

| Element | Why Skip |
|---------|----------|
| Left sidebar navigation | Overkill for 4 tabs, wastes horizontal space |
| API & Keys management | Not implemented in backend |
| Firmware update / Factory reset | Not implemented |
| Deploy Node button | Not implemented |
| Search/filter bar on console | Console is simple log stream + command input |
| Health Overview (CPU, latency) | Not tracked by our gateway |
| Metric Distribution bar chart | Not implemented |
| Export button on logs | Not implemented |
| Log level filter buttons (INFO/WARN/DEBUG/ERROR) | Logs are unstructured strings from gateway |
