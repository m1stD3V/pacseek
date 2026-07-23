# PacSeek demo recording - render with:  vhs docs/demo.tape
# Drives the offline --mock dataset so it's deterministic and touches nothing.
# A throwaway config enables every manager so the full SOURCES list shows.

Output docs/demo.gif

Set Shell "bash"
Set FontSize 13
Set Width 1300
Set Height 800
Set Padding 12
Set Framerate 24
Set PlaybackSpeed 1.0

# Setup (hidden): a temp config that surfaces every source, then launch on mock.
Hide
Type 'export XDG_CONFIG_HOME=$(mktemp -d)'
Enter
Type 'mkdir -p $XDG_CONFIG_HOME/pacseek'
Enter
Type 'echo "package_managers = pacman, aur, flatpak, npm, bun, pnpm" > $XDG_CONFIG_HOME/pacseek/config.ini'
Enter
Type 'clear'
Enter
Type './build/pacseek --mock'
Enter
Sleep 2s
Show

# Browse: the storage-impact bars, and the two-axis sidebar (VIEWS + SOURCES).
Sleep 2s
Down@350ms 4
Sleep 1s

# Sort by size - the heavy packages float up in orange.
Type "s"
Sleep 1800ms

# The SOURCES axis: f cycles it. Step onto pacman, then AUR - the sidebar
# highlight and the SOURCE chip beside SORT track each step, and the list narrows.
Type "f"
Sleep 1700ms
Type "f"
Sleep 1900ms

# Spin the rest of the way back to All (mock has no flatpak/npm rows to dwell on).
Type "f"
Type "f"
Type "f"
Type "f"
Type "f"
Sleep 1200ms

# The VIEWS axis: Installed, then Orphans - the footprint card shifts with each.
Type "2"
Sleep 1600ms
Type "4"
Sleep 1600ms

# Back to Browse and open the detail pane on the selection.
Type "1"
Sleep 700ms
Type "d"
Sleep 2200ms
Down@300ms 3
Sleep 1s
Escape
Sleep 700ms

# The controls popout: every binding, over a dimmed background.
Type "?"
Sleep 2200ms
Escape
Sleep 700ms

# Quit.
Type "q"
Sleep 800ms
