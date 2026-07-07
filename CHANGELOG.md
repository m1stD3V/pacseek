# Changelog

## 1.0 - Feature-complete

The whole original roadmap got built. Every item cleared the same bar: does it help
someone browse, understand storage impact, transact safely, keep their system
healthy, or move their setup - and does PacSeek do it *meaningfully better* than the
one-line command? If not, it belongs to the CLI, not here (see
[Considered and declined](#considered-and-declined)).

### Core

- [x] Read-only browse with live sizes, update detection, and disk footprint
- [x] Virtualized package list (handles full catalogs smoothly)
- [x] Disk footprint against total drive capacity
- [x] Apply transactions (install / remove) with privilege escalation
- [x] Live AUR RPC search
- [x] Per-package detail pane (dependencies, files, provenance)
- [x] Multi-select for mass install / removal
- [x] Configuration file at `~/.config/pacseek/config.ini` (initial view / sort, AUR helper override, theme, keybindings)
- [x] Other package managers: flatpak backend (list / install / remove) and broader AUR-helper detection (paru, yay, pikaur, trizen)
- [x] Theme support: default (brutalist), tokyo-night, catppuccin-mocha, catppuccin-macchiato, gruvbox
- [x] Curated package collections by use case (gaming, creative work, development, multimedia, system/terminal)
- [x] User-defined collections in a sibling `collections.ini`, with malformed collections as a hard error that names the offender; unresolved package names render as "unavailable" rather than failing
- [x] Install script (`install.sh`) - function over form, with a bit of branding

### Storage-first, made actionable

- [x] Partial-upgrade guard - warns before installing while system updates are pending (`-S` without `-Syu` risks a partial-upgrade breakage)
- [x] Orphan / unneeded-package detection with reclaimable space - the `pacman -Qdt` set computed locally, with a per-row `ORPHAN` badge and a footprint-card "reclaim" line
- [x] Change a package's install reason (`pacman -D --asdeps` / `--asexplicit`) - the `r` key, the missing verb that makes orphan detection trustworthy
- [x] Marginal install cost - the detail pane of a not-installed package shows the count and size of dependencies not already present ("+6 new deps → 340 MiB total")
- [x] File → package ownership lookup (`pacman -F` / `-Qo`) - the `o` key, scanning local filelists first, then the sync files database only when present; never triggers `-Fy`
- [x] Export / import of the explicit package list (`pacman -Qqe`) - `x` writes it, `i` installs whatever is missing
- [x] Removal cascade preview - a single-package removal shows what `-Rs` will also drag out, and the space it reclaims, before committing
- [x] Reverse-dependency "why is this installed?" - a detail-pane line tracing the shortest chain from an explicit root ("gnome → gvfs")
- [x] Conflicts / replaces / provides in the detail pane
- [x] Official package groups folded into Collections (`base-devel`, etc.)
- [x] `.pacnew` / `.pacsave` indicator - a footer count of config files left unmerged after updates
- [x] Repo filter in browse (core / extra / multilib / aur / flatpak) - the `f` key
- [x] Configurable keybindings - rebind the letter actions via `key_<action>` entries
- [x] Release completeness - a `--version` / `-V` flag, a `pacseek.1` man page, and a clear "AUR unreachable" message
- [x] AUR package build - a `PKGBUILD` and `.SRCINFO` in [`packaging/`](packaging/), ready for AUR submission once a release tag is pushed
- [x] AUR trust signals - search results sorted by popularity with vote counts, plus a detail-pane section (votes, popularity, maintainer / orphan warning, last-updated, out-of-date flag) fetched asynchronously
- [x] Honest sync indicator - the footer shows the age of the last database refresh and turns amber past a week
- [x] In-TUI database refresh (`y`) - runs `sudo pacman -Sy` through the terminal handoff and reloads
- [x] Flatpak update detection from flatpak's cached remote summaries (no network hit), with `u` chaining `flatpak update`
- [x] Navigable detail pane - `tab` cycles the dependency/provides/conflicts links, `enter` drills in, `backspace` walks back
- [x] Mouse support - wheel scrolling, click-to-select, sidebar view switching
- [x] Cache maintenance - the footprint card shows the pacman package-cache size, `c` reclaims it via `paccache -r`, `m` launches `pacdiff`
- [x] Mark-all (`a`) - one keystroke to mark everything visible
- [x] Tests and CI - a framework-free CTest suite over the pure layers and a Woodpecker pipeline that builds and runs it on every push

## Considered and declined

Kept off the list on purpose, to stay lean and utilitarian. Recorded here so the
reasoning isn't relitigated:

- **Storage growth over time** - analytics for its own sake: needs persistence infrastructure to show a trend nobody actually needs to act on. Neat, not useful.
- **Undo last transaction** - deceptively unsafe and rare: upgrades can't be cleanly reversed, and reversing a remove needs a cache tarball that may be gone. High risk for low frequency.
- **Downgrade from cache** - genuine but rare (only when an update breaks), and the `downgrade` tool already covers it. Revisit only if it proves a recurring need.
- **Transaction history view** - on its own it's a `pacman.log` viewer the CLI (`less`, `paclog`) already handles; its main value was as substrate for Undo, which is declined.
- **Health / landing dashboard** - adds no new capability, only re-frames signals, and only pays off once the pieces exist. Reconsider later, if the aggregation earns its own maintenance.
- **User-defined themes** - compounds the theme set into a config subsystem for pure aesthetics. The keybinding half survived as its own item; the palette half did not.
