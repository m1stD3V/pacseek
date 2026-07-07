# PacSeek - Configuration

PacSeek runs fine with zero config. But if you like your tools to remember your
preferences, here's every knob.

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

## Custom keybindings

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

## Portable package list

`x` exports the explicit-install set (`pacman -Qqe`) to
`~/.config/pacseek/pkglist.txt`, one name per line; `i` reads it back and installs
whatever is missing (through the AUR helper when one is present, so repo and AUR
names restore together). Back up the config folder and a fresh machine rehydrates
with a single keystroke - the whole-system sibling of user-defined collections.

## User-defined collections

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
