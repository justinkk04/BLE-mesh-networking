# v0.7.2 Web UI Dashboard Enhancement — Changelog

## [v0.7.2] - WIP

### Dashboard Enhancement — "Command Center" Redesign

**Frontend-only changes. No backend modifications.**

- **Layout Restructure (Phase 1)** ✅
  - Added dedicated Console tab — freed Dashboard from cramped console section
  - New 4-tab navigation: Dashboard · Analytics · Console · Settings
  - Node cards redesigned with inline duty input and Read button (removed popover menu)
  - Topology and Power Manager compacted into side-by-side cards (`.dashboard-top-row`)
  - Settings expanded with card-based Appearance, PM Configuration, and Command Reference groups

- **Visual Redesign (Phase 2)** ✅
  - Design tokens refreshed — purple accent (`hsl(270 60% 60%)`), dedicated `--bg-console`, text-size + spacing scale
  - Node cards restyled — large JetBrains Mono metric values (1.25rem), 3-col Power/Voltage/Current grid, gradient duty bar, inline Set/Read buttons
  - Console tab restyled as a full-viewport terminal (dark `#0d1117` bg, `$` prompt, mono font throughout)
  - Dashboard top row compacted — `1.5fr 1fr` grid for topology + PM, 300px fixed height, `.compact-panel` wrapper
  - Settings redesigned — card groups with purple pill toggle (`.theme-toggle` track/thumb)

- **Post-Phase 2 Fixes** ✅
  - Removed orphaned `#main-grid` / `#topology-section` / `#pm-panel` / `#console-section` rules that clobbered the new layout with `height: 320px` and grid-area assignments
  - Converted `.tab-content.active` to a column flex container so Dashboard / Analytics / Console tabs all fill the viewport and reduce blank space
  - **Analytics redesigned for single-node view** — added pill-style node selector (`.node-tab`) above the chart; `charts.js` now tracks `selectedNode`, hides non-selected wrappers, auto-picks the first node, and shows a "Waiting for node data…" empty state
  - Node cards widened to `minmax(320px, 1fr)` to fit the 3-metric row without wrap
  - Dead popover CSS (`.node-menu-btn`, `.popover`, `.popover-item`) removed — no longer used after Phase 1's inline controls
