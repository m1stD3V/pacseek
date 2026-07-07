// config.hpp - optional user configuration loaded from
// $XDG_CONFIG_HOME/pacseek/config.ini (or ~/.config/pacseek/config.ini).
//
// The format is a flat, commented "key = value" file; parsing is lenient so an
// older binary ignores keys it doesn't know and a newer file never breaks it.
// This layer depends only on model/, never on UI or FTXUI.
#pragma once

#include <string>

#include "config/keybindings.hpp"
#include "model/catalog.hpp"  // model::View, model::Sort

namespace pacseek::config {

struct Config {
  model::View view = model::View::Browse;          // initial view
  model::Sort sort = model::Sort::SizeDescending;  // initial sort
  std::string aur_helper;  // override auto-detection ("paru" / "yay"); empty = auto
  std::string theme;       // reserved for upcoming theme support; empty = default
  Keybindings keys;        // rebindable single-key actions; defaults = current keys

  // Which package managers the user has chosen to surface. Pacman is always on;
  // the rest govern whether their data source loads and whether their legend /
  // filter entries appear. Defaults keep the pre-selection behaviour (AUR and
  // flatpak on, Homebrew off) so an older config with no `package_managers` key
  // behaves exactly as before. The first-run prompt writes this key explicitly.
  bool aur_enabled = true;
  bool flatpak_enabled = true;
  bool homebrew_enabled = false;

  // Glyph rendering mode: false = Unicode chrome (default), true = the ASCII-safe
  // set for terminals without dependable ambiguous-width handling (see theme.hpp
  // and the `glyphs = ascii` key / --ascii flag).
  bool ascii_glyphs = false;

  // Set by LoadConfig when no config file existed yet, so main can run the
  // first-run package-manager prompt. Never read from the file itself.
  bool first_run = false;
};

// Resolves the config file path: $XDG_CONFIG_HOME/pacseek/config.ini, falling
// back to ~/.config/pacseek/config.ini. Empty when neither variable is set.
std::string DefaultConfigPath();

// Resolves the exported package-list path, mirroring DefaultConfigPath() but
// ending in pkglist.txt: $XDG_CONFIG_HOME/pacseek/pkglist.txt, falling back to
// ~/.config/pacseek/pkglist.txt. Empty when neither variable is set.
std::string DefaultPackageListPath();

// Parses an INI-ish "key = value" document. Blank lines and lines starting with
// '#' or ';' are ignored, as are unknown keys and unparsable values; recognized
// keys override the defaults. Pure and side-effect free (for testing).
Config ParseConfig(const std::string& text);

// Loads the config from DefaultConfigPath(). Returns defaults when the file is
// absent or unreadable; when it is absent, sets Config::first_run and also writes
// a commented template so the available options are discoverable (never
// overwrites an existing file).
Config LoadConfig();

// Persists the first-run package-manager and glyph choices by appending an
// authoritative block to the config file (the template it sits under is all
// comments, so the appended keys win). Best-effort: returns false if the file
// can't be written. Called once, right after the first-run prompt.
bool PersistFirstRunChoices(const Config& config);

}  // namespace pacseek::config
