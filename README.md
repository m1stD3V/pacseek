<div align="center">

# ◆ PacSeek

**A tech-brutalist TUI package manager for Arch Linux.**

The one that judges your disk usage - `pacman` (core · extra · multilib), the
**AUR**, and **flatpak**, in one keyboard-driven terminal, with zero mercy for
that 4 GiB you forgot you installed.

![C++17](https://img.shields.io/badge/C%2B%2B-17-de542c?style=flat-square&labelColor=0e0e10)
![FTXUI](https://img.shields.io/badge/UI-FTXUI%20v6.1.9-8a8f9a?style=flat-square&labelColor=0e0e10)
![libalpm](https://img.shields.io/badge/data-libalpm-7fae8b?style=flat-square&labelColor=0e0e10)
![platform](https://img.shields.io/badge/platform-Arch%20Linux-e0b341?style=flat-square&labelColor=0e0e10)
![license](https://img.shields.io/badge/license-MIT-e9e7e2?style=flat-square&labelColor=0e0e10)

![PacSeek in action](docs/demo.gif)

</div>

## What it is

Most package front-ends will happily install a 700 MiB toolchain without so much
as a raised eyebrow. PacSeek raises the eyebrow. It treats **disk usage as the
headline metric**: every package wears a storage-impact bar normalized to the
heaviest thing in view, and the sidebar carries a live **disk-footprint card** that
puts your installed total against your whole drive, broken down by repository color.

It reads real data straight from pacman's own **libalpm** - no shelling out, no
scraping CLI output - and dresses it in a Braun-inspired brutalist skin: square
corners, machined chrome, a single orange accent, and mono data type.

## Highlights

- **Storage-first, and it means it.** Per-package impact bars, heavy-package
  highlighting (anything ≥ 300 MiB turns orange), a repo-segmented disk-footprint
  card, and an orphans view that tells you exactly how much space you'd reclaim.
- **Real data, no scraping.** Links libalpm directly - the local database joined
  against the sync databases, with foreign / hand-built packages surfaced as AUR,
  plus live AUR RPC search and flatpak apps folded into the same catalog.
- **Built for big systems.** The package list is virtualized: it renders only the
  rows that fit your terminal, so a full 15,000-package catalog scrolls instantly.
- **Safe by default.** A partial-upgrade guard, a removal-cascade preview, and an
  honest sync-age footer catch the hazards the raw commands don't flag - and
  nothing touches your system until you confirm at the real prompts.
- **Keyboard-driven, yours to rebind.** Views, search, sort, detail, and batches
  are all a keystroke away, every letter binding is configurable, and your config
  folder carries your themes, collections, and muscle memory to a fresh machine.

## Quick start

```sh
./install.sh                 # build + install to ~/.local (no root)
./install.sh --system        # build + install to /usr/local (uses sudo)
./install.sh --help          # prefix / build-dir / jobs options
```

`install.sh` verifies the toolchain, does a Release build, installs, and - if the
install dir isn't on your `PATH` - prints the exact line to add for your shell.

Then run it:

```sh
pacseek            # live system, via libalpm
pacseek --mock     # the 22-package design prototype (offline, read-only)
pacseek --help
```

Kicking the tires? `--mock` is a self-contained dataset that touches nothing -
it's the same thing you see in the GIF above.

<details>
<summary>Build it by hand instead</summary>

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure   # optional: run the test suite
cmake --install build --prefix ~/.local       # drops the binary at ~/.local/bin/pacseek
```

**Requirements:** a C++17 compiler (GCC or Clang), CMake ≥ 3.20, `libalpm`
(pacman's own library - you already have it), and `libcurl` for AUR search.
`nlohmann-json` is optional and auto-detected; FTXUI is fetched and pinned
automatically by CMake (v6.1.9).

</details>

## Usage at a glance

Six views - **Browse · Installed · Updates · AUR · Collections · Orphans** - on the
number keys `1`–`6`. `/` searches, `s` sorts by size or name, `f` filters by repo,
`d` opens a package's detail pane, `space` marks packages for a batch, and `enter`
applies. Lost? Press `?` for the full keymap.

That's the tour. The full manual lives in the docs:

| Document | What's in it |
|----------|--------------|
| [**docs/USAGE.md**](docs/USAGE.md) | Every key, every view, the detail pane, transactions, safety prompts, batches, and the flatpak/AUR machinery |
| [**docs/CONFIGURATION.md**](docs/CONFIGURATION.md) | `config.ini`, themes, custom keybindings, portable package lists, and user-defined collections |
| [**docs/ARCHITECTURE.md**](docs/ARCHITECTURE.md) | The layered design, the data-source interface, list virtualization, and the design-token system |
| [**CHANGELOG.md**](CHANGELOG.md) | Everything that's built, and the features deliberately left on the cutting-room floor |

## Status

**Feature-complete for 1.0** - a sentence every developer has said and PacSeek
actually means. Browse, search, live sizes, update detection, real transactions
(pacman, AUR helpers, and flatpak), the detail pane, orphan cleanup, and the whole
disk-footprint story are live. There's a `pacseek.1` man page, a CTest suite under
Woodpecker CI, and a `PKGBUILD` in [`packaging/`](packaging/) waiting on a release
tag. See the [changelog](CHANGELOG.md) for the full ledger.

## License

Released under the [MIT License](LICENSE).

---

<div align="center">

Created by **m1st** · built to make you feel bad about `texlive`

</div>
