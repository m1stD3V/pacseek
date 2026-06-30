# Handoff: PacSeek — Pacman + AUR Package Manager

## Overview
**PacSeek** is a desktop GUI package manager / app store for Arch Linux, covering both official `pacman` repositories (core/extra/multilib) and the **AUR**. It supports searching, browsing, an installed view, available updates, an AUR filter, and makes **storage impact** a first-class part of the UI (per-package size bars + a total disk-footprint summary broken down by repo). It is themed in the same tech-brutalist language as the companion Hyprland desktop (machined chrome, concrete, single Braun-orange accent, mono data type).

## About the Design Files
The file in this bundle is a **design reference created in HTML** — a prototype of look and behavior, not production code to ship. Recreate it in the target codebase's environment using its established patterns: e.g. a native GTK4/libadwaita or Qt/QML app, a Tauri/Electron app (React/Svelte/Vue), or a TUI. The actual data layer is **`pacman`/`libalpm`** + an **AUR helper** (`yay`, `paru`) or the AUR RPC; the HTML defines the UI, not the backend.

## Fidelity
**High-fidelity (hifi).** Final colors, typography, spacing, and interactions — match exactly.

---

## Design Tokens

### Colors
| Token | Hex | Use |
|---|---|---|
| `bg-void` | `#0a0a0b` | Behind window |
| `window` | `#0e0e10` | App window body |
| `sidebar` | `#0b0b0d` | Sidebar / inset / footer |
| `row-divider` | `#0b0b0d` | Row separators |
| `line` | `#000` / `#050506` | Borders, seams |
| `accent` | `#de542c` | Braun orange — primary, "heavy package" bars, active nav |
| `accent-grad` | `#e8643a` → `#cf471d` | INSTALL button / heavy bar gradient |
| `text` | `#e9e7e2` | Primary |
| `text-dim` | `#9a9a95` | Descriptions |
| `text-faint` | `#6a6a66` / `#5d5d5a` | Mono labels |
| **Repo colors** | | repo badges + legend dots + stacked bar |
| `core` | `#de542c` | orange |
| `extra` | `#8a8f9a` | grey |
| `aur` | `#7fae8b` | sage |
| `multilib` | `#e0b341` | amber |
| `update` | `#e0b341` | amber update badge/count |
| `ok` | `#7fae8b` | sage "SYNCED" dot |

### Typography
- **Display / package names / UI:** `Space Grotesk` (package name 600 16px; window/section titles 600).
- **Data / labels / versions / sizes:** `JetBrains Mono` (sizes/versions/badges 10–15px; section labels 10px uppercase, `letter-spacing:2px`, `#5d5d5a`).

### Geometry & depth
- Square corners throughout. Window: `border:1px solid #050506`, `box-shadow: inset 0 1px 0 rgba(255,255,255,.06), 0 40px 90px rgba(0,0,0,.6)`.
- Custom scrollbar: 10px, track `#0c0c0e`, thumb `#2a2a2d`, hover `#de542c`.
- Repo badge: `padding:3px 9px`, mono 10px uppercase, `#0a0a0b` text on the repo color.

---

## Screen: PacSeek window (1560×968, centered on a concrete backdrop)

### Title bar (44px)
Metal gradient `linear-gradient(180deg,#1a1a1d,#131315)`, `border-bottom:1px solid #000`. Orange `PACSEEK` mark (700, black rotated diamond) · mono subtitle `PACKAGE MANAGER · PACMAN + AUR` · three window-control squares at right (last one tinted red-ish `#3a2422`).

### Sidebar (248px, `#0b0b0d`)
- **LIBRARY** nav (each row: 15px icon + label + mono count): **Browse ◈**, **Installed ▣**, **Updates ↑** (count amber when >0), **AUR ✦**. Active row: `border-left:2px solid #de542c`, `background:rgba(222,84,44,.07)`, text `#e9e7e2`; inactive `#9a9a95`. Hover `rgba(255,255,255,.03)`.
- **REPOSITORIES** legend: colored 9px square + repo name + count of installed packages in that repo.
- **DISK FOOTPRINT** card (bottom, `#0e0e10` border `#000`): big total in **GiB** (34px 600) + "GiB" mono; a **stacked horizontal bar** segmented by repo color (widths proportional to installed size per repo); `<N> packages installed`.

### Main column
- **Search + controls bar:** search field (`#0b0b0d`, orange `❯`, mono 15px input, placeholder "Search pacman + AUR…", live `<N> RESULTS` at right) + **SORT** toggle button cycling `SIZE ↓` / `NAME ↓`.
- **Column header** (mono 10px uppercase `#5d5d5a`): `PACKAGE` (flex) · `REPO` (120px) · `STORAGE IMPACT` (300px) · `SIZE` (110px, right) · `ACTION` (130px, right).
- **Package rows** (15px vertical padding, divider `#0b0b0d`, hover `rgba(255,255,255,.02)`):
  - **Package:** name (600 16px) + version (mono 11px `#5d5d5a`) + optional **UPDATE** badge (mono 9px amber, `border:1px solid #5a4a1a`, `background:rgba(224,179,65,.08)`); description below (`#9a9a95` 13px, truncated).
  - **Repo:** colored badge.
  - **Storage impact:** a 10px track (`#0b0b0d` border `#000`) with a fill = `size / maxSize` %. Fill is **orange gradient when the package is "heavy" (≥300 MiB)**, else grey gradient (`#5a5a5e→#8a8f9a`). Caption row: `DL <download size>` left, `<pct>% OF MAX` right (mono 10px `#5d5d5a`).
  - **Size:** mono 15px 500; **orange when heavy (≥300 MiB)**, else `#e9e7e2`. Sizes format as MiB, or GiB (2dp) at ≥1024.
  - **Action:** **INSTALL** (orange gradient, `#0a0a0b`) when not installed; **REMOVE** (`#16161a`, `#b8b6b0`, `border:1px solid #2a2a2d`) when installed. Hover `brightness(1.12)`.
  - Empty state: `⊘` + `NO PACKAGES MATCH "<query>"`.
- **Footer status bar** (`#0b0b0d`, mono 11px `#6a6a66`): sage dot + `SYNCED` · `<N> UPDATES AVAILABLE` / `SYSTEM UP TO DATE` · right: `DOWNLOAD CACHE · 1.8 GiB`.

---

## Interactions & Behavior
- **Search** filters the current view by name + description (live, case-insensitive).
- **Nav tabs** filter the package set: Browse = all; Installed = installed only; Updates = has-update only; AUR = repo==aur. Counts in the nav reflect the full dataset.
- **Sort** toggles between size-descending and name-ascending.
- **Install/Remove** flips a package's installed state (and clears its update flag on reinstall); this live-updates the result list, nav counts, repo legend, and the disk-footprint total + stacked bar.
- Storage-impact bars are normalized to the **largest package in the whole dataset** so bars are comparable across views.
- Transitions are minimal (`filter .12s` on buttons/bars) — keep it crisp, not animated.

## State Management
Prototype state: `view` (`browse|installed|updates|aur`), `query`, `sort` (`size|name`), and `pkgs[]` (the package list with `installed`/`update` mutated in place). In production, replace `pkgs[]` with a query against `libalpm` (sync + local DBs) and an AUR source; "size" = installed size, "dl" = download/compressed size, "update" = newer version available; install/remove dispatch real transactions (with privilege escalation + progress), not local state flips.

## Design Tokens — quick reference
- Heavy-package threshold: **≥ 300 MiB** (drives orange size text + orange bar).
- Size format: `<n> MiB`, or `(n/1024).toFixed(2) GiB` at ≥ 1024 MiB.
- Repo color map: core `#de542c`, extra `#8a8f9a`, aur `#7fae8b`, multilib `#e0b341`.

## Assets
- Fonts: **Space Grotesk** + **JetBrains Mono** (Google Fonts; bundle locally in production).
- Concrete backdrop: inline SVG `feTurbulence` (prototype only) — irrelevant once embedded in a real window.
- All icons are Unicode glyphs (◈ ▣ ↑ ✦ ❯ ⊘ etc.); swap for the platform's real icon set.

## Files
- `Package Manager.dc.html` — the PacSeek prototype (full source: sidebar, search, sort, package rows, storage bars, disk-footprint summary, footer).
- `support.js` — Design Component runtime (prototype harness only; ignore for production).
