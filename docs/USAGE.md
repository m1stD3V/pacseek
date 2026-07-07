# PacSeek — Usage

Everything PacSeek can do, in more detail than the README wants to carry. If you
just want to get going, the README's Quick Start is enough; this is the manual for
when you're ready to make it sing.

## Keys

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
config file (see [Configuration](CONFIGURATION.md)), and the popout reflects your
own keys.

The **mouse** works too: the wheel scrolls the list (or an open detail pane),
clicking a row selects it, and clicking a sidebar entry switches views.

## Views

- **Browse**: everything in the sync repositories.
- **Installed**: only what's currently on the system.
- **Updates**: packages whose installed version is older than the sync version.
- **AUR**: foreign packages (AUR or hand-built) already on the system, surfaced
  from the local database. Type a term and press `enter` to replace the list with
  **live results from the AUR RPC** — packages you can install but don't yet have.
  The fetch runs on a background thread, so the interface never freezes; results
  drop in when they arrive. Switching views returns to the local foreign list.
  The search is deliberately gentle on the AUR: it fires only on `enter` (never
  per keystroke), needs at least two characters, collapses repeat Enter-presses
  while a request is in flight, and memoizes each term for the session so the same
  query is never fetched twice. Results carry the AUR's **trust signals**: they
  arrive sorted by popularity (then votes), the vote count fills the size column
  (`▲ 1227` — an un-built package has no meaningful size anyway), and packages
  flagged **out-of-date** upstream wear a badge in the list.
- **Collections**: hand-curated package bundles grouped by use case — **Gaming**,
  **Creative Work**, **Development**, **Multimedia**, and **System & Terminal**. A
  two-level browse: the picker lists each collection with how many of its members
  are available locally and how many are already installed; press `enter` to drill
  in and see those members as an ordinary package list (`esc` / `h` / `←` backs out).
  Inside a collection everything works as usual — `space` to mark, `enter` to install
  the marked batch, `d` for detail — so a collection is a fast path to "install the
  usual set for X". Members live in the curated list, not the network: each name is
  resolved against the local databases, so collections never reach out to the AUR
  (members that aren't in the sync repos simply show as unavailable until installed).
  The **official pacman groups** (`base-devel`, …) that libalpm exposes are folded
  in after the curated and user-defined sets, so a group browses and installs
  through the same machinery.
- **Orphans**: installed packages that were pulled in as dependencies but nothing
  needs any more (`pacman -Qdt`) — the safe-to-remove pile. The nav count glows
  when any exist, and the footer shows the total space you'd reclaim by removing
  them. Work it like any other view: `space` to mark the ones to drop, `enter` to
  remove the batch. A quick way to actually free the space the footprint card's
  "reclaimable" line has been telling you about.

Search filters the active view by a case-insensitive match over package **name and
description** (within a collection it narrows that collection's members). The nav
counts always reflect the whole dataset, independent of the current search. Press
`f` to cycle a **repo filter** (core / extra / multilib / aur / flatpak) that
narrows the list to a single source on top of whatever view and search are active.

## The detail pane

Press `d` on any row to open its **detail pane** — an expanded, scrollable view of
that one package, loaded on demand from libalpm (file lists are too large to carry
for every row up front):

- **Provenance**: repository, installed size, licenses, upstream URL, packager,
  build date, and — for installed packages — the install date, whether it was
  installed explicitly or as a dependency, and a **why installed** line that
  traces the shortest chain from an explicitly-installed root down to it
  (`gnome → gvfs`) for packages pulled in indirectly.
- **Install cost** (for a not-installed package): the true disk cost of adding it —
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
walks the trail back the way you came — so "what actually is this dependency?"
is answered without leaving the pane.

For AUR packages the pane also carries an **AUR trust section**, fetched in the
background over the same RPC interface while the local fields render instantly:
votes, popularity, the maintainer (with an explicit *orphaned* warning when there
is none), the last-updated date, and a highlighted **flagged out-of-date** warning
with the flag date when upstream has marked the package stale. Answers are cached
for the session; network errors are not, so reopening retries.

## Reading the storage impact

The whole point of the app, really.

- **Impact bar**: each package's installed size as a fraction of the heaviest
  package currently in view, so bars stay comparable as you sort and filter.
- **Heavy highlight**: packages at or above **300 MiB** render their size and bar
  in the orange accent.
- **`% OF MAX`**: the same fraction as a number, alongside the compressed
  **download** size.

## The disk-footprint card

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

## Applying changes

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

Under `--mock`, `enter` only reports what it *would* do — nothing touches the system.

## Previews and safety prompts

Before a transaction commits, PacSeek surfaces what the raw command won't:

- **Partial-upgrade guard.** Installing while updates are pending (a `-S` without
  `-Syu`, which risks a partial-upgrade breakage) opens a confirmation first —
  `enter` to proceed anyway, `u` to run the full upgrade instead, `esc` to cancel.
- **Removal cascade preview.** Removing a single package first shows everything
  `-Rs` will also drag out and the space it reclaims, so there are no surprises at
  pacman's own confirm screen.

Both use the same in-TUI prompt; it appears only when there is something to warn
about, and otherwise the action applies straight through.

## Keeping the system healthy

- **Orphans and reclaimable space.** Packages installed as dependencies that
  nothing needs anymore (the `pacman -Qdt` set) carry an `ORPHAN` badge, and the
  disk-footprint card gains a "reclaim" line totalling what removing them frees.
  Mark them with `space` and remove the batch to act on it.
- **Fix install reasons.** Press `r` to flip the selected installed package
  between *explicit* and *dependency* (`pacman -D`) — the correction that keeps the
  orphan set trustworthy.
- **Unmerged configs.** A bounded scan of `/etc` at startup counts `.pacnew` /
  `.pacsave` files left after updates; when any exist the footer shows a count, so
  they aren't silently forgotten — and `m` launches `pacdiff` right there to merge
  them (the key declines with a notice when there's nothing to merge).
- **Sync-database age.** The footer's `● SYNCED` chip is honest: it shows how long
  ago the sync databases were last refreshed (`SYNCED 6h AGO`), and past a week it
  turns amber — so "0 updates" is never mistaken for "up to date" on a system that
  simply hasn't run `-Sy` lately. An empty Updates view says the same thing in
  place (`databases synced 23h ago · y re-checks`), and `y` runs `sudo pacman -Sy`
  through the usual terminal handoff, reloading the catalog on return so the
  Updates tab reflects upstream again — no dropping to a shell just to find out
  what's pending. The refresh is only ever user-initiated, and the
  partial-upgrade guard already covers the classic refresh-then-install hazard.
- **Package-cache reclaim.** The disk-footprint card shows the size of
  `/var/cache/pacman/pkg`, and `c` runs `paccache -r` (keep the 3 newest versions
  of each package) behind the same confirmation prompt as any transaction. Both
  maintenance keys need `pacman-contrib` installed and say so when it isn't.
- **File → package lookup.** Press `o`, type a path or command, and PacSeek names
  the package that owns it — scanning your installed files first, then the sync
  files database when it is present (it never runs `-Fy`; it hints to when absent).

## Multi-select (mass install / removal)

Press `space` to **mark** the selected package (a `✓` appears and the selection
auto-advances, so marking a run is one repeated keystroke), or `a` to mark
**everything currently visible** in one stroke — filter to the Orphans view and
`a` + `enter` is "remove all orphans". When every visible row is already marked,
`a` clears them instead. Marks are kept by name, so they persist as you change
views, search, and sort. The footer shows a live `N marked · enter to apply`
prompt.

Press `enter` to apply the whole marked set as a **single batch**:

- All-install marks become one `sudo pacman -S --needed <pkgs>` — or one
  `paru`/`yay` `-S <pkgs>` when any are from the AUR (the helper installs repo and
  AUR packages together).
- All-remove marks become one `sudo pacman -Rs <pkgs>`.
- A batch is one action type: if the marked set mixes packages to install *and*
  remove, PacSeek declines and asks you to apply one kind at a time.

With nothing marked, `enter` falls back to acting on the single selected row.

## Package managers

Beyond pacman repositories and the AUR, PacSeek also surfaces **flatpak**
applications when the `flatpak` CLI is installed: installed apps appear in the
catalog tagged `FLATPAK` (a blue badge and its own legend / footprint segment),
and `enter` removes them via `flatpak uninstall`. Flatpak **updates** are detected
too — from flatpak's own cached remote summaries (`remote-ls --cached`), so no
network request is made — and show up in the Updates view; when any are pending,
the `u` system upgrade chains `flatpak update` after `pacman -Syu`. Stale
flatpaks never trigger the partial-upgrade guard, which only counts pacman
updates. Remote (flathub) search is a later milestone, so for now every flatpak
row is one you already have.

AUR transactions run through the first helper found on `PATH`, probed in order:
`paru`, `yay`, `pikaur`, `trizen` (override with `aur_helper` in
the config). A batch (multi-select) is one manager at a time — pacman/AUR can mix,
but flatpak applies separately.
