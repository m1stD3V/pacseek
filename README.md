<div align="center">

# ◆ PacSeek

**A tech-brutalist TUI package manager for Arch Linux, where storage impact is a first-class citizen.**

`pacman` repositories (core · extra · multilib) and the **AUR**, in one keyboard-driven terminal interface.

![C++17](https://img.shields.io/badge/C%2B%2B-17-de542c?style=flat-square&labelColor=0e0e10)
![FTXUI](https://img.shields.io/badge/UI-FTXUI%20v6.1.9-8a8f9a?style=flat-square&labelColor=0e0e10)
![libalpm](https://img.shields.io/badge/data-libalpm-7fae8b?style=flat-square&labelColor=0e0e10)
![platform](https://img.shields.io/badge/platform-Arch%20Linux-e0b341?style=flat-square&labelColor=0e0e10)

</div>

---

## What it is

PacSeek is a terminal program for browsing the packages on your Arch system and in
its repositories. Unlike most package front-ends, it treats **disk usage as the
headline metric**: every package shows a storage-impact bar normalized to the
heaviest package in view, and the sidebar carries a live **disk-footprint card**
that puts your installed total against your whole drive, broken down by repository
color.

It reads real data straight from pacman's own **libalpm** (no shelling out, no
scraping CLI output) and recreates the `design_handoff_pacseek` visual reference
in the terminal: square corners, machined chrome, a single Braun-orange accent,
and mono data type.

> A captured frame lives in [`docs/preview.txt`](docs/preview.txt). Colors are lost
> in plain text; run it for the real thing.

```
 ◆ PACSEEK   PACKAGE MANAGER · PACMAN + AUR                                   ▢ ▢ ▣
──────────────────────────────┬─────────────────────────────────────────────────────
                              │ ❯ Search pacman + AUR…                22 RESULTS  SORT SIZE ↓
  LIBRARY                     ├─────────────────────────────────────────────────────────
                              │  PACKAGE                       REPO     STORAGE IMPACT      SIZE    ACTION
  ◈ Browse                22 ├─────────────────────────────────────────────────────────
  ▣ Installed             17 │ rustup  1.27.1-1               EXTRA    ██████████████     680 MiB  REMOVE
  ↑ Updates                3  │ The Rust toolchain installer            DL 210 MiB  100% OF MAX
  ✦ AUR                    3  ├─────────────────────────────────────────────────────────
                              │ blender  4.1.1-3               EXTRA    ██████████░░░░     520 MiB  INSTALL
  REPOSITORIES                │ A fully integrated 3D suite             DL 180 MiB   76% OF MAX
  █ CORE                   2  │ …
  █ EXTRA                 12  │
  █ AUR                    3  │
  █ MULTILIB               0  │
──────────────────────────────┤
  DISK FOOTPRINT              │
  24.56 GiB / 852.86 GiB      │   ← installed total / whole-drive capacity
  ██████░░░░░░░░░░░░░░░░      │   ← segmented by repository color
  1365 packages · 2.9% of disk│
```

## Highlights

- **Storage-first by design.** Per-package impact bars, heavy-package highlighting
  (anything ≥ 300 MiB turns orange), and a repo-segmented disk-footprint summary.
- **Real data, no scraping.** Links libalpm directly: the local database (what's
  installed) joined against the sync databases (what's available, sizes, newer
  versions), with foreign / hand-built packages surfaced as AUR.
- **Built for big systems.** The package list is virtualized: it renders only the
  rows that fit your terminal, so a full 15,000-package catalog scrolls instantly.
- **Two interchangeable backends.** The live libalpm source and a self-contained
  22-package mock dataset share one interface; the UI never knows the difference.
- **Keyboard-driven.** Views, search, sort, and navigation are all a keystroke away.
- **A single design language.** Every color, threshold, and column width lives in
  one theme file, with no hardcoded values scattered through the render layer.

## Status

**Browse and modify.** Search, listing, sizes, update detection, and the disk
footprint are live from libalpm. On the live system, `enter` now applies **real
transactions**: repository packages through `sudo pacman`, AUR packages through a
detected helper (`paru`, `yay`, `pikaur`, …), and **flatpak** apps through the
flatpak CLI when it is installed. The `--mock` dataset stays read-only and just
shows what it would do. In the **AUR** view, pressing `enter` in the search box
runs a **live AUR RPC search**, and `space`/`enter` mark and apply batches across
multiple packages.

**Feature-complete for 1.0.** The whole roadmap is now built: orphan detection
with reclaimable space, install-reason flipping, transaction previews (a
partial-upgrade guard, per-install marginal disk cost, and a removal cascade
preview), a file → package lookup, official pacman groups in Collections, a repo
filter, an unmerged-config (`.pacnew`) indicator, portable explicit-package
export/import, rebindable keys, and a `--version` flag plus a `pacseek.1` man
page - and, on top of that: AUR trust signals (votes, popularity, maintainer,
out-of-date flags), an honest sync-age footer, flatpak update detection, a
navigable detail pane, mouse support, cache cleaning and `pacdiff` launching, a
mark-all key, and a CTest suite with Woodpecker CI. See [Roadmap](#roadmap).

---

## Install

### Requirements

| Dependency | Notes |
|------------|-------|
| C++17 compiler | GCC or Clang |
| CMake ≥ 3.20 | build system |
| `libalpm` | pacman's library, present on every Arch system |
| `libcurl` | live AUR RPC search |
| `nlohmann-json` | optional; auto-detected |
| FTXUI | **fetched and pinned automatically** by CMake (v6.1.9) |

### Quick install

```sh
./install.sh                 # build + install to ~/.local (no root)
./install.sh --system        # build + install to /usr/local (uses sudo)
./install.sh --help          # prefix / build-dir / jobs options
```

`install.sh` verifies the toolchain, configures a Release build, compiles,
installs, and - if the install directory isn't on your `PATH` - prints the exact
line to add for your shell. It's the manual steps below, wrapped and checked.

### Build (manual)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### Tests

```sh
ctest --test-dir build --output-on-failure
```

A framework-free suite over the pure layers - the transaction command builders
(including the injection-rejection paths), the config and collections parsers,
and the catalog filtering/sorting. The same suite runs in CI
([`.woodpecker.yml`](.woodpecker.yml)) on every push. Pass `-DBUILD_TESTING=OFF`
at configure time to skip building it.

### Run

```sh
./build/pacseek          # live system, via libalpm
./build/pacseek --mock   # the design prototype's 22-package dataset (offline)
./build/pacseek --help
```

### Install to your PATH

```sh
cmake --install build --prefix ~/.local
```

This drops the binary at `~/.local/bin/pacseek`. If `~/.local/bin` is on your
`PATH`, you can now run `pacseek` from anywhere. For a system-wide install, omit
`--prefix` (defaults to `/usr/local`, needs root).

---

## Usage

### Keys

| Key | Action |
|-----|--------|
| `1` – `6` | switch view: Browse / Installed / Updates / AUR / Collections / Orphans |
| `/` | focus search (`Esc` or `Enter` to leave) |
| `enter` (in search, AUR view) | run a live AUR RPC search for the typed term |
| `enter` (Collections picker) | open the highlighted collection |
| `esc` / `h` / `←` (in a collection) | back out to the collections picker |
| `j` / `k` | move selection (also `↓` / `↑`) |
| `s` | toggle sort: size ↓ / name ↓ |
| `f` | cycle the repo filter (core / extra / multilib / aur / flatpak / off) |
| `u` | update: full system upgrade (`-Syu`) |
| `y` | refresh the sync databases (`-Sy`) so the update counts are current |
| `d` | open the detail pane for the selected package (`d` / `esc` / `q` to close) |
| `tab` (in detail) | cycle through the pane's package links (dependencies, provides, …) |
| `enter` (in detail) | jump to the highlighted link's own detail pane |
| `backspace` / `←` (in detail) | walk back along the link trail |
| `o` | file → package lookup: which package owns a file or command |
| `r` | flip the selected installed package's install reason (explicit ↔ dependency) |
| `x` / `i` | export / import the explicit package list (`~/.config/pacseek/pkglist.txt`) |
| `space` | mark / unmark the selected package for a batch (auto-advances) |
| `a` | mark every visible package (press again to clear them all) |
| `c` | clean the package cache (`paccache -r`, from pacman-contrib) |
| `m` | merge unmerged `.pacnew` / `.pacsave` configs (`pacdiff`) |
| `enter` | apply the marked batch, act on the selection, or confirm a pending prompt |
| `?` | open the controls popout (every binding); `?` / `esc` / `q` to close |
| `q` / `esc` | quit (or cancel a confirmation / close a popout) |

The footer carries a single `? controls` legend rather than a wall of keys; press
`?` for the full popout, which floats over a dimmed background so it reads as a
focused dialog. In the detail pane, `j` / `k` (or `↓` / `↑`) scroll through the
dependencies and file list. Every letter binding above is rebindable from the
config file (see [Configuration](#configuration)), and the popout reflects your
own keys.

The **mouse** works too: the wheel scrolls the list (or an open detail pane),
clicking a row selects it, and clicking a sidebar entry switches views.

### Views

- **Browse**: everything in the sync repositories.
- **Installed**: only what's currently on the system.
- **Updates**: packages whose installed version is older than the sync version.
- **AUR**: foreign packages (AUR or hand-built) already on the system, surfaced
  from the local database. Type a term and press `enter` to replace the list with
  **live results from the AUR RPC** - packages you can install but don't yet have.
  The fetch runs on a background thread, so the interface never freezes; results
  drop in when they arrive. Switching views returns to the local foreign list.
  The search is deliberately gentle on the AUR: it fires only on `enter` (never
  per keystroke), needs at least two characters, collapses repeat Enter-presses
  while a request is in flight, and memoizes each term for the session so the same
  query is never fetched twice. Results carry the AUR's **trust signals**: they
  arrive sorted by popularity (then votes), the vote count fills the size column
  (`▲ 1227` - an un-built package has no meaningful size anyway), and packages
  flagged **out-of-date** upstream wear a badge in the list.
- **Collections**: hand-curated package bundles grouped by use case - **Gaming**,
  **Creative Work**, **Development**, **Multimedia**, and **System & Terminal**. A
  two-level browse: the picker lists each collection with how many of its members
  are available locally and how many are already installed; press `enter` to drill
  in and see those members as an ordinary package list (`esc` / `h` / `←` backs out).
  Inside a collection everything works as usual - `space` to mark, `enter` to install
  the marked batch, `d` for detail - so a collection is a fast path to "install the
  usual set for X". Members live in the curated list, not the network: each name is
  resolved against the local databases, so collections never reach out to the AUR
  (members that aren't in the sync repos simply show as unavailable until installed).
  The **official pacman groups** (`base-devel`, …) that libalpm exposes are folded
  in after the curated and user-defined sets, so a group browses and installs
  through the same machinery.
- **Orphans**: installed packages that were pulled in as dependencies but nothing
  needs any more (`pacman -Qdt`) - the safe-to-remove pile. The nav count glows
  when any exist, and the footer shows the total space you'd reclaim by removing
  them. Work it like any other view: `space` to mark the ones to drop, `enter` to
  remove the batch. A quick way to actually free the space the footprint card's
  "reclaimable" line has been telling you about.

Search filters the active view by a case-insensitive match over package **name and
description** (within a collection it narrows that collection's members). The nav
counts always reflect the whole dataset, independent of the current search. Press
`f` to cycle a **repo filter** (core / extra / multilib / aur / flatpak) that
narrows the list to a single source on top of whatever view and search are active.

### The detail pane

Press `d` on any row to open its **detail pane** - an expanded, scrollable view of
that one package, loaded on demand from libalpm (file lists are too large to carry
for every row up front):

- **Provenance**: repository, installed size, licenses, upstream URL, packager,
  build date, and - for installed packages - the install date, whether it was
  installed explicitly or as a dependency, and a **why installed** line that
  traces the shortest chain from an explicitly-installed root down to it
  (`gnome → gvfs`) for packages pulled in indirectly.
- **Install cost** (for a not-installed package): the true disk cost of adding it -
  how many dependencies aren't already present and the total install size,
  resolved over the sync databases.
- **Dependencies**: the package's hard and optional dependencies, what it
  **provides**, **conflicts** with, or **replaces**, and (for installed packages)
  what currently **requires** it.
- **Files**: the absolute paths the package owns. Available only once installed;
  for an available-but-not-installed package the pane says so. Very large file
  lists are truncated with a summary line.

The package name stays pinned at the top while the body scrolls with `j` / `k`.
For an un-built AUR search result there is no database entry yet, so the pane shows
the search fields and notes that the rest appears after installation.

The relationship bullets are **navigable links**: `tab` cycles a `▸` cursor
through the dependencies, provides, conflicts, replaces, and required-by entries,
`enter` opens the highlighted package's own detail pane, and `backspace` (or `←`)
walks the trail back the way you came - so "what actually is this dependency?"
is answered without leaving the pane.

For AUR packages the pane also carries an **AUR trust section**, fetched in the
background over the same RPC interface while the local fields render instantly:
votes, popularity, the maintainer (with an explicit *orphaned* warning when there
is none), the last-updated date, and a highlighted **flagged out-of-date** warning
with the flag date when upstream has marked the package stale. Answers are cached
for the session; network errors are not, so reopening retries.

### Reading the storage impact

- **Impact bar**: each package's installed size as a fraction of the heaviest
  package currently in view, so bars stay comparable as you sort and filter.
- **Heavy highlight**: packages at or above **300 MiB** render their size and bar
  in the orange accent.
- **`% OF MAX`**: the same fraction as a number, alongside the compressed
  **download** size.

### The disk-footprint card

Pinned to the foot of the sidebar:

```
  DISK FOOTPRINT
  24.56 GiB / 852.86 GiB        installed total / total drive capacity
  ██████░░░░░░░░░░░░░░░░         repo breakdown: CORE · EXTRA · AUR · MULTILIB
  1365 packages · 2.9% of disk   share of the whole filesystem
  cache 4.41 GiB · c to clean    pacman package cache, reclaimable with paccache
```

Drive capacity is measured once at startup from the root filesystem. The segmented
bar uses each repository's identity color, so you can see at a glance which sources
own your disk.

### Applying changes

On the live backend, `enter` installs the selected package (or removes it if it's
already installed). PacSeek suspends its interface, runs the operation in the plain
terminal so you can see and confirm everything, then reloads and resumes:

- **Repository packages** run through `sudo pacman -S --needed <pkg>` (install) or
  `sudo pacman -Rs <pkg>` (remove, which also clears dependencies no other package
  still needs). You'll be prompted for your password and for pacman's own
  confirmation.
- **AUR packages** install through a detected helper, `paru` or `yay`, run as your
  normal user. If neither is installed, PacSeek says so rather than guessing.
- PacSeek never composes a privileged command from anything but a validated package
  name, and it performs no action until you confirm at the prompts.

Under `--mock`, `enter` only reports what it *would* do - nothing touches the system.

### Previews and safety prompts

Before a transaction commits, PacSeek surfaces what the raw command won't:

- **Partial-upgrade guard.** Installing while updates are pending (a `-S` without
  `-Syu`, which risks a partial-upgrade breakage) opens a confirmation first -
  `enter` to proceed anyway, `u` to run the full upgrade instead, `esc` to cancel.
- **Removal cascade preview.** Removing a single package first shows everything
  `-Rs` will also drag out and the space it reclaims, so there are no surprises at
  pacman's own confirm screen.

Both use the same in-TUI prompt; it appears only when there is something to warn
about, and otherwise the action applies straight through.

### Keeping the system healthy

- **Orphans and reclaimable space.** Packages installed as dependencies that
  nothing needs anymore (the `pacman -Qdt` set) carry an `ORPHAN` badge, and the
  disk-footprint card gains a "reclaim" line totalling what removing them frees.
  Mark them with `space` and remove the batch to act on it.
- **Fix install reasons.** Press `r` to flip the selected installed package
  between *explicit* and *dependency* (`pacman -D`) - the correction that keeps the
  orphan set trustworthy.
- **Unmerged configs.** A bounded scan of `/etc` at startup counts `.pacnew` /
  `.pacsave` files left after updates; when any exist the footer shows a count, so
  they aren't silently forgotten - and `m` launches `pacdiff` right there to merge
  them (the key declines with a notice when there's nothing to merge).
- **Sync-database age.** The footer's `● SYNCED` chip is honest: it shows how long
  ago the sync databases were last refreshed (`SYNCED 6h AGO`), and past a week it
  turns amber - so "0 updates" is never mistaken for "up to date" on a system that
  simply hasn't run `-Sy` lately. An empty Updates view says the same thing in
  place (`databases synced 23h ago · y re-checks`), and `y` runs `sudo pacman -Sy`
  through the usual terminal handoff, reloading the catalog on return so the
  Updates tab reflects upstream again - no dropping to a shell just to find out
  what's pending. The refresh is only ever user-initiated, and the
  partial-upgrade guard already covers the classic refresh-then-install hazard.
- **Package-cache reclaim.** The disk-footprint card shows the size of
  `/var/cache/pacman/pkg`, and `c` runs `paccache -r` (keep the 3 newest versions
  of each package) behind the same confirmation prompt as any transaction. Both
  maintenance keys need `pacman-contrib` installed and say so when it isn't.
- **File → package lookup.** Press `o`, type a path or command, and PacSeek names
  the package that owns it - scanning your installed files first, then the sync
  files database when it is present (it never runs `-Fy`; it hints to when absent).

### Multi-select (mass install / removal)

Press `space` to **mark** the selected package (a `✓` appears and the selection
auto-advances, so marking a run is one repeated keystroke), or `a` to mark
**everything currently visible** in one stroke - filter to the Orphans view and
`a` + `enter` is "remove all orphans". When every visible row is already marked,
`a` clears them instead. Marks are kept by name, so they persist as you change
views, search, and sort. The footer shows a live `N marked · enter to apply`
prompt.

Press `enter` to apply the whole marked set as a **single batch**:

- All-install marks become one `sudo pacman -S --needed <pkgs>` - or one
  `paru`/`yay` `-S <pkgs>` when any are from the AUR (the helper installs repo and
  AUR packages together).
- All-remove marks become one `sudo pacman -Rs <pkgs>`.
- A batch is one action type: if the marked set mixes packages to install *and*
  remove, PacSeek declines and asks you to apply one kind at a time.

With nothing marked, `enter` falls back to acting on the single selected row.

### Package managers

Beyond pacman repositories and the AUR, PacSeek also surfaces **flatpak**
applications when the `flatpak` CLI is installed: installed apps appear in the
catalog tagged `FLATPAK` (a blue badge and its own legend / footprint segment),
and `enter` removes them via `flatpak uninstall`. Flatpak **updates** are detected
too - from flatpak's own cached remote summaries (`remote-ls --cached`), so no
network request is made - and show up in the Updates view; when any are pending,
the `u` system upgrade chains `flatpak update` after `pacman -Syu`. Stale
flatpaks never trigger the partial-upgrade guard, which only counts pacman
updates. Remote (flathub) search is a later milestone, so for now every flatpak
row is one you already have.

AUR transactions run through the first helper found on `PATH`, probed in order:
`paru`, `yay`, `pikaur`, `trizen` (override with `aur_helper` in
the config). A batch (multi-select) is one manager at a time - pacman/AUR can mix,
but flatpak applies separately.

---

## Configuration

PacSeek reads an optional config file from `$XDG_CONFIG_HOME/pacseek/config.ini`
(falling back to `~/.config/pacseek/config.ini`). On first run, if no file exists,
PacSeek writes a fully-commented template there so the options are discoverable -
it never overwrites an existing file.

The format is a flat, commented `key = value` file. Parsing is lenient: blank
lines and lines starting with `#` or `;` are ignored, as are unknown keys and
unparsable values, so a newer config never breaks an older binary.

```ini
# Initial view when pacseek starts: browse | installed | updates | aur
view = browse

# Initial sort order: size | name
sort = size

# Preferred AUR helper, overriding auto-detection. Must speak pacman syntax
# (-S / -Syu). Leave unset to auto-detect in order: paru, yay, pikaur, trizen.
aur_helper = paru

# Color theme: default | tokyo-night | catppuccin-mocha | catppuccin-macchiato | gruvbox
theme = tokyo-night

# Rebind any letter action with key_<action> = <char> (see below).
# key_sort = S
```

| Key | Values | Effect |
|-----|--------|--------|
| `view` | `browse` · `installed` · `updates` · `aur` | the view shown at startup |
| `sort` | `size` · `name` | the initial sort order |
| `aur_helper` | `paru` · `yay` · `pikaur` · … | forces the helper instead of probing `PATH` |
| `theme` | `default` · `tokyo-night` · `catppuccin-mocha` · `catppuccin-macchiato` · `gruvbox` | the color palette (names are case-insensitive; spaces/underscores ok) |
| `key_<action>` | a single character | rebinds a letter action (see below) |

### Custom keybindings

Every single-letter action is rebindable with a `key_<action> = <char>` line, so
your muscle memory travels with your config folder. The structural keys - `enter`,
`space`, `esc`, the arrows, and the `1`–`5` view numbers - stay fixed; only the
letter/symbol actions rebind. Unset actions keep their defaults, and an empty or
multi-character value is ignored. The controls popout (`?`) always reflects the
keys actually in effect.

```ini
# Defaults shown; uncomment and change a value to rebind.
# key_quit = q          key_detail = d         key_search = /
# key_sort = s          key_update = u         key_reason = r
# key_filter = f        key_file_lookup = o    key_help = ?
# key_collections_back = h
# key_export_list = x   key_import_list = i
# key_mark_all = a      key_clean_cache = c    key_pacdiff = m
# key_refresh = y
```

### Portable package list

`x` exports the explicit-install set (`pacman -Qqe`) to
`~/.config/pacseek/pkglist.txt`, one name per line; `i` reads it back and installs
whatever is missing (through the AUR helper when one is present, so repo and AUR
names restore together). Back up the config folder and a fresh machine rehydrates
with a single keystroke - the whole-system sibling of user-defined collections.

### User-defined collections

Alongside `config.ini`, PacSeek reads an optional `collections.ini` from the same
folder. Each `[section]` defines a collection whose id is the section name, so
backing up your config folder carries your own collections to a fresh install.
Like `config.ini`, a commented template is dropped on first run.

```ini
[my-setup]
name = My Setup
icon = ★
description = My personal must-haves
packages = neovim, git, tmux, ripgrep
```

| Key | Required | Effect |
|-----|----------|--------|
| `name` | yes | display label in the collections picker |
| `packages` | yes | comma-separated package names |
| `icon` | no | single glyph for the row (defaults to `▸`) |
| `description` | no | one-line summary under the name |

Your collections appear in the Collections view after the built-in ones. Unknown
keys are ignored, so a newer file never breaks an older binary. Package names are
**not** checked against the network at startup: any that aren't installed or in
your repos simply render as *unavailable*, exactly like the AUR entries in the
built-in collections.

A **malformed** collection - a missing `name`, an empty package entry (a stray
comma), a duplicate id, or a bad section header - is a **hard error**. PacSeek
refuses to start and prints each offender by collection and line number, so a
typo is never silently swallowed:

```
pacseek: refusing to start - user-defined collections are invalid:
  collections.ini [my-setup] (line 4): empty package name in 'packages' (stray comma?)
Fix ~/.config/pacseek/collections.ini and try again.
```

---

## Architecture

The codebase is layered so that each concern is isolated and testable, with the
render layer kept free of I/O and the data layer kept free of presentation. The
dependency direction is strict and one-way: solid arrows are calls / ownership,
dashed arrows are interface implementations and design-token lookups.

```mermaid
flowchart TD
    CLI([" terminal · keystrokes "]):::ext

    subgraph entry [" "]
        MAIN["main.cpp<br/><i>flags · source selection</i>"]:::app
    end

    subgraph applayer ["app · owns state and the loop"]
        APP["App<br/><i>event loop · key bindings</i>"]:::app
        STATE["AppState<br/><i>view · query · sort · selection</i>"]:::app
    end

    subgraph uilayer ["ui · pure state to Element"]
        RENDER["RenderApp<br/><i>brutalist render builders</i>"]:::ui
        VLIST["PackageList<br/><i>virtualized window</i>"]:::ui
    end

    subgraph datalayer ["data · where packages come from"]
        PSRC{{"PackageSource<br/><i>interface</i>"}}:::data
        ALPM["AlpmSource<br/><i>live</i>"]:::data
        MOCK["MockSource<br/><i>offline</i>"]:::data
    end

    subgraph modellayer ["model · pure logic, no I/O"]
        CAT["Catalog<br/><i>filter · sort · counts · footprint</i>"]:::model
        PKG["Package"]:::model
    end

    subgraph syslayer ["system · OS side effects"]
        TX["transaction<br/><i>install · remove</i>"]:::sys
    end

    THEME["theme.hpp<br/><i>every design token, named</i>"]:::theme
    LIBALPM[("libalpm<br/>pacman DBs")]:::ext
    PACMAN[("sudo pacman<br/>· AUR helper")]:::ext

    CLI -->|input| APP
    MAIN -->|selects| PSRC
    MAIN -->|runs| APP

    APP -->|mutates| STATE
    APP -->|owns| CAT
    APP -->|draws via| RENDER
    APP -->|loads once| PSRC
    APP -->|applies| TX

    RENDER -->|reads| STATE
    RENDER -->|queries| CAT
    RENDER --> VLIST

    PSRC -.implemented by.-> ALPM
    PSRC -.implemented by.-> MOCK
    ALPM -->|reads| LIBALPM
    TX -->|shells out| PACMAN
    ALPM -->|produces| PKG
    MOCK -->|produces| PKG
    CAT -->|owns| PKG

    RENDER -.tokens.-> THEME
    CAT -.tokens.-> THEME
    PKG -.tokens.-> THEME

    classDef app fill:#24160f,stroke:#de542c,stroke-width:2px,color:#e9e7e2
    classDef ui fill:#15151a,stroke:#8a8f9a,stroke-width:1.5px,color:#e9e7e2
    classDef data fill:#13201a,stroke:#7fae8b,stroke-width:1.5px,color:#e9e7e2
    classDef model fill:#1a1a1d,stroke:#b8b6b0,stroke-width:1.5px,color:#e9e7e2
    classDef theme fill:#2a2210,stroke:#e0b341,stroke-width:1.5px,color:#e9e7e2
    classDef ext fill:#0b0b0d,stroke:#5d5d5a,stroke-width:1px,color:#9a9a95,stroke-dasharray:4 3
    classDef sys fill:#201410,stroke:#e8643a,stroke-width:1.5px,color:#e9e7e2

    style entry fill:#0e0e10,stroke:#1c1c20
    style applayer fill:#0e0e10,stroke:#1c1c20,color:#5d5d5a
    style uilayer fill:#0e0e10,stroke:#1c1c20,color:#5d5d5a
    style datalayer fill:#0e0e10,stroke:#1c1c20,color:#5d5d5a
    style modellayer fill:#0e0e10,stroke:#1c1c20,color:#5d5d5a
    style syslayer fill:#0e0e10,stroke:#1c1c20,color:#5d5d5a
```

The file map:

```
src/
  theme.hpp            every design token: the Palette struct and named themes
                       (brutalist / tokyo-night / catppuccin / gruvbox) swapped by
                       SetTheme, plus the 300 MiB "heavy" threshold, GiB / TiB
                       cutoffs, and column widths. No magic numbers downstream.
  model/               pure domain logic, no I/O (fully testable)
    package.{hpp,cpp}    Package type, repo ↔ color/name, size formatting
    package_detail.hpp   PackageDetail: the expanded per-package view
                         (dependencies, files, provenance) for the detail pane
    catalog.{hpp,cpp}    filtering / search / sort, view + repo counts,
                         disk-footprint totals
  data/                where packages come from, behind one interface
    package_source.hpp   abstract PackageSource (LoadPackages + Describe)
    mock_source.{hpp,cpp}  the prototype's fixed 22-package dataset (--mock)
    alpm_source.{hpp,cpp}  libalpm: local DB joined against sync DBs, with
                           update detection; foreign packages shown as AUR
    flatpak_source.{hpp,cpp}  installed flatpak apps via the flatpak CLI
    composite_source.{hpp,cpp}  merges sources (alpm + flatpak) into one catalog
                                and routes Describe back to the owning source
  system/                OS side effects, isolated from the rest
    transaction.{hpp,cpp}  install / remove / system update via pacman + AUR
                           helper, with safe command building and tool detection
    aur_client.{hpp,cpp}   live AUR RPC search over libcurl, JSON parsed with
                           nlohmann; returns packages tagged AUR
  config/                user configuration, no UI dependency
    config.{hpp,cpp}       loads ~/.config/pacseek/config.ini (view / sort / AUR
                           helper); writes a commented template on first run
  ui/
    components.{hpp,cpp} the brutalist render layer: pure state → Element
                         builders, including the virtualized package list
  app/
    app_state.hpp        the mutable UI state (view / query / sort / selection)
    app.{hpp,cpp}        run loop, key bindings, state transitions
  main.cpp             flag parsing + source selection
```

The UI never knows which `PackageSource` it is drawing, so `--mock` and the live
libalpm backend are fully interchangeable. For a deeper tour (data flow, the
rendering model, and the list-virtualization design) see
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Design system

The default visual language is a single Braun-inspired accent over near-black
surfaces, with repository identity colors doing the categorical work.

| Token | Hex | Role |
|-------|-----|------|
| Accent | `#de542c` | Braun orange: brand, heavy packages, active nav |
| Repo · core | `#de542c` | orange |
| Repo · extra | `#8a8f9a` | grey |
| Repo · aur | `#7fae8b` | sage |
| Repo · multilib | `#e0b341` | amber |
| Text | `#e9e7e2` | primary type |

Every color lives in a single `Palette` struct, and the whole UI references it by
name (`color::Accent`, …). Alternate themes - **tokyo-night**, **catppuccin-mocha**,
**catppuccin-macchiato**, **gruvbox** - are just other `Palette` values; the active
one is chosen by the `theme` config key and swapped at startup, so no call site
hardcodes a hex. Thresholds and dimensions live alongside the palette in
[`src/theme.hpp`](src/theme.hpp): the 300 MiB heavy cutoff, the 1024 MiB → GiB and
1024 GiB → TiB formatting steps, and every column width.

## Roadmap

- [x] Read-only browse with live sizes, update detection, and disk footprint
- [x] Virtualized package list (handles full catalogs smoothly)
- [x] Disk footprint against total drive capacity
- [x] Apply transactions (install / remove) with privilege escalation
- [x] Live AUR RPC search
- [x] Per-package detail pane (dependencies, files, provenance)
- [x] Multi-select for install/remove transactions for mass install or removal
- [x] Configuration file at `~/.config/pacseek/config.ini` (initial view / sort, AUR helper override; theme + package-manager keys to follow)
- [x] Support other package managers: flatpak backend (list / install / remove) and broader AUR-helper detection (paru, yay, pikaur, trizen - helpers that speak pacman's -S/-Syu syntax)
- [x] Theme support: default (brutalist), tokyo-night, catppuccin-mocha, catppuccin-macchiato, gruvbox
- [x] Curated package collections by use case (gaming, creative work, development, multimedia, system/terminal)
- [x] Allow for user defined collections in a sibling file `collections.ini` in the pacseek config folder, so backing up your config carries your own collections to a fresh installation. Malformed collections (missing name, empty package entry, duplicate id, bad syntax) are a hard error that names the offending collection and halts loading, so mistakes are never silent. Packages that don't resolve against the local databases aren't fatal - they render as "unavailable" exactly like built-in AUR entries, keeping the offender visible without a startup network hit or false-positive rejection of valid AUR names.
- [x] installation script (`install.sh`) that prioritizes functionality, but has a branded output nothing too elaborate function over form.
Each item here answered: does it help someone browse, understand storage impact,
transact safely, keep their system healthy, or move their setup - and does
pacseek do it meaningfully better than the one-line command? If not, it belongs
to the CLI, not here (see _Considered and declined_). Everything that cleared
that bar is now built:

- [x] Partial-upgrade guard. Warns before installing while system updates are pending (`-S` without `-Syu` risks a partial-upgrade breakage), via a confirmation prompt reusing the already-detected update count. Purely local - a real correctness guard for a hazard the raw command doesn't flag.
- [x] Orphan / unneeded-package detection with reclaimable space. Surfaces the `pacman -Qdt` set (dependencies nothing needs, required or optionally) computed locally via libalpm: a per-row `ORPHAN` badge plus a footprint-card "reclaim" line, turning the storage-first framing from descriptive into actionable.
- [x] Change a package's install reason (`pacman -D --asdeps` / `--asexplicit`). The `r` key flips the selected installed package's reason, the missing verb that makes orphan detection trustworthy.
- [x] Marginal install cost. The detail pane of a not-installed package shows its true disk cost - the count and size of dependencies not already present, resolved over the sync databases ("+6 new deps → 340 MiB total").
- [x] File → package ownership lookup (`pacman -F` / `-Qo`). The `o` key opens a lookup that answers "which package owns/provides this file or command?" - scanning local filelists first, then the sync files database only when it is already present, with a "run `pacman -Fy` to enable" hint when it isn't. Never triggers the `-Fy` sync.
- [x] Export / import of the explicit package list (the `pacman -Qqe` set). `x` writes it to `~/.config/pacseek/pkglist.txt`; `i` installs whatever is missing. The whole-system sibling of user-defined collections. Fully local.
- [x] Removal cascade preview. A single-package removal shows what `-Rs` will also drag out, and the space it reclaims, in the TUI at decision time before committing.
- [x] Reverse-dependency "why is this installed?" - a line in the detail pane tracing the shortest chain from an explicitly-installed root down to the package ("gnome → gvfs"), via libalpm reverse-deps.
- [x] Conflicts / replaces / provides in the detail pane, the fields people still open `pacman -Qi` for before an install.
- [x] Official package groups, folded into Collections - the pacman groups libalpm exposes (`base-devel`, etc.) browse and install through the existing collection machinery.
- [x] `.pacnew` / `.pacsave` indicator - a footer count of config files left unmerged after updates (a bounded `/etc` scan), surfacing the problem and deferring the merge to `pacdiff`.
- [x] Repo filter in browse (core / extra / multilib / aur / flatpak) - the `f` key cycles a source filter, finishing the navigation the sidebar repo legend implies.
- [x] Configurable keybindings in the config file - rebind the letter actions via `key_<action>` entries, extending "back up your config, carry it anywhere" to muscle memory.
- [x] Release completeness: a `--version` / `-V` flag, a `pacseek.1` man page, and a clear "AUR unreachable" message when the network drops instead of a raw error.
- [x] AUR package build - a `PKGBUILD` and `.SRCINFO` live in [`packaging/`](packaging/), ready for AUR submission once a release tag is pushed.
- [x] AUR trust signals - search results sorted by popularity with vote counts in the list, and a detail-pane section (votes, popularity, maintainer / orphan warning, last-updated, out-of-date flag) fetched asynchronously over the same RPC interface.
- [x] Honest sync indicator - the footer shows the age of the last database refresh and turns amber past a week, so "0 updates" can't masquerade as "up to date".
- [x] In-TUI database refresh (`y`) - runs `sudo pacman -Sy` through the terminal handoff and reloads, so finding out what's actually pending never requires a shell; the empty Updates view names the databases' age and points at the key.
- [x] Flatpak update detection from flatpak's cached remote summaries (no network hit), with `u` chaining `flatpak update` when flatpak updates are pending.
- [x] Navigable detail pane - `tab` cycles the dependency/provides/conflicts links, `enter` drills into them, `backspace` walks back.
- [x] Mouse support - wheel scrolling, click-to-select, and sidebar view switching.
- [x] Cache maintenance - the footprint card shows the pacman package-cache size and `c` reclaims it via `paccache -r`; `m` launches `pacdiff` when unmerged configs exist.
- [x] Mark-all (`a`) - one keystroke to mark everything visible, making "remove all orphans" a two-key operation.
- [x] Tests and CI - a framework-free CTest suite over the pure layers (command builders, config/collections parsers, catalog) and a Woodpecker pipeline that builds and runs it on every push.

### Considered and declined

Kept off the active list on purpose, to stay lean and utilitarian. Recorded here
so the reasoning isn't relitigated:

- **Storage growth over time** - analytics for its own sake: needs persistence infrastructure to show a trend nobody actually needs to act on. Neat, not useful.
- **Undo last transaction** - deceptively unsafe and rare: upgrades can't be cleanly reversed, and reversing a remove needs a cache tarball that may be gone. High risk for low frequency.
- **Downgrade from cache** - genuine but rare (only when an update breaks), and the `downgrade` tool already covers it. Revisit only if it proves a recurring need.
- **Transaction history view** - on its own it's a `pacman.log` viewer the CLI (`less`, `paclog`) already handles; its main value was as substrate for Undo, which is declined.
- **Health / landing dashboard** - adds no new capability, only re-frames signals, and only pays off once the pieces exist. Reconsider later, if the aggregation earns its own maintenance.
- **User-defined themes** - compounds the theme set into a config subsystem for pure aesthetics. The keybinding half survived as its own item; the palette half did not.


---

## License

Released under the [MIT License](LICENSE).

---

<div align="center">

Created by **m1st**

</div>
