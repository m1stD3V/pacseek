// first_run.hpp - the one-time setup shown when pacseek starts with no config
// file. It asks which package managers to surface and whether to use the ASCII
// glyph set, folds the answers into a Config, and the caller persists them.
//
// Kept in the app layer (it owns an FTXUI screen) but deliberately tiny: it runs
// before the main App, on the plain data it is handed, and returns plain data.
#pragma once

#include "config/config.hpp"
#include "system/transaction.hpp"

namespace pacseek::app {

// Runs the interactive chooser and returns `base` with the manager toggles and
// ascii_glyphs set from the user's choices. `tools` seeds the initial checks
// (flatpak/homebrew pre-checked when their CLI is on PATH) and annotates each
// row as detected or not. Pure UI: it neither reads nor writes the config file.
config::Config RunFirstRunSetup(const config::Config& base, const system::Tools& tools);

}  // namespace pacseek::app
