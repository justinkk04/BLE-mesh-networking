# Dashboard UI Overhaul — "Midnight Vercel" Design

**Date:** March 1, 2026
**Branch:** `feature/ui-overhaul`
**Scope:** Frontend only (`/dashboard` directory). Zero Python backend changes.

## Aesthetic

Hybrid of **Midnight Command Center** (deep navy atmosphere, subtle blue-purple glows) and **Clean Vercel** (ultra-clean Inter typography, generous whitespace, micro-animations).

## Navigation — Tab System

Three tabs in a slim horizontal bar below the header:

| Tab | Content | Default |
|-----|---------|---------|
| Dashboard | Topology, Node Cards, Power Manager, Console | ✅ |
| Analytics | Full-width per-node charts with metric toggle | |
| Settings | Stub — future poll/PM config UI | |

Tabs switch instantly via JS class toggle. WebSocket stays connected. No page reload.

## Design Tokens

| Token | Value |
|-------|-------|
| `--bg-primary` | `#0a0a0f` |
| `--bg-card` | `rgba(15, 18, 30, 0.7)` + `backdrop-filter: blur(16px)` |
| `--accent-primary` | `hsl(230, 80%, 65%)` — soft electric blue |
| `--accent-online` | `hsl(145, 65%, 50%)` — green with glow pulse |
| `--border` | `rgba(255,255,255,0.06)` — barely visible |
| Font weights | 700 headings, 500 labels, 400 body |

## Animations

- Card entrance: `slideUp` (12px + fade, 300ms)
- Value update: accent color flash (300ms)
- Online badge: subtle `box-shadow` pulse
- Topology links: dashed `stroke-dashoffset` flow
- Tab switch: `opacity` crossfade (150ms)

## Backend Contract (LOCKED)

These must NOT change:

- WebSocket events: `sensor_data`, `state`, `log`, `event` (subtypes: `pm_update`, `poll_update`, `connected`, `disconnected`)
- REST: `/api/state`, `/api/history`, `/api/command`, `/api/settings`
- DOM IDs: `pm-active`, `pm-threshold`, `pm-budget`, `pm-total`, `pm-priority`, `duty-${id}`, `power-${id}`, `volt-${id}`, `curr-${id}`, `bar-${id}`, `node-summary`, `ws-status`, `connection-badge`, `poll-toggle`, `poll-interval`
- JS exports: `init()`, `updateNode()`, `getAllNodes()`, `addPoint()`, `appendLog()`, `sendCommand()`
