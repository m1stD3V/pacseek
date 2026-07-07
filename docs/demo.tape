# PacSeek demo recording — render with:  vhs docs/demo.tape
# Drives the offline --mock dataset so it's deterministic and touches nothing.

Output docs/demo.gif

Set Shell "bash"
Set FontSize 13
Set Width 1300
Set Height 760
Set Padding 12
Set Framerate 24
Set PlaybackSpeed 1.0

# Launch on the mock catalog
Hide
Type "./build/pacseek --mock"
Enter
Sleep 2s
Show

# Browse — let the storage-impact bars sit for a beat
Sleep 2s

# Walk down the list
Down@400ms 4
Sleep 1s

# Sort by size — the heavy packages float up in orange
Type "s"
Sleep 2s

# Open the detail pane on the selected package
Type "d"
Sleep 2500ms

# Scroll its body
Down@300ms 3
Sleep 1s

# Back out
Escape
Sleep 1s

# Jump to the Installed view, then Orphans, to show the footprint card shift
Type "2"
Sleep 1500ms
Type "6"
Sleep 2s

# Open the controls popout to show the keymap, then close
Type "?"
Sleep 2s
Escape
Sleep 800ms

# Quit
Type "q"
Sleep 800ms
