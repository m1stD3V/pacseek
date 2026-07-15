#include "config/config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace pacseek::config {

namespace {

namespace fs = std::filesystem;

// The template dropped on first run. Every setting is commented out, so loading
// it yields the same defaults as having no file at all.
constexpr char kTemplate[] =
    "# pacseek configuration\n"
    "# Lines starting with '#' are comments. Remove the '#' to enable a setting.\n"
    "\n"
    "# Initial view when pacseek starts: browse | installed | updates | orphans | collections\n"
    "# view = browse\n"
    "\n"
    "# Initial source filter: all | pacman | aur | flatpak | homebrew | npm | bun | pnpm\n"
    "# (the SOURCES axis; pick aur to open on a live AUR search box)\n"
    "# source = all\n"
    "\n"
    "# Initial sort order: size | name\n"
    "# sort = size\n"
    "\n"
    "# Preferred AUR helper, overriding auto-detection. Must speak pacman syntax\n"
    "# (-S / -Syu). Auto-detected in order: paru, yay, pikaur, trizen.\n"
    "# aur_helper = paru\n"
    "\n"
    "# Color theme: default | tokyo-night | catppuccin-mocha | catppuccin-macchiato | gruvbox\n"
    "# theme = tokyo-night\n"
    "\n"
    "# Package managers to surface, comma-separated: pacman (always on), aur,\n"
    "# flatpak, homebrew, npm, bun, pnpm. Governs which sources load and which\n"
    "# legend/filter entries appear. npm/bun/pnpm list globally-installed packages\n"
    "# (npm install -g, etc.). Set on first run; edit here to change it later.\n"
    "# package_managers = pacman, aur, flatpak\n"
    "\n"
    "# Glyph set: unicode (default) or ascii. Use ascii on terminals that render\n"
    "# symbol glyphs double-width or as boxes (alacritty, ghostty, tty console).\n"
    "# glyphs = unicode\n"
    "\n"
    "# Keybindings: rebind any single-key letter/symbol action with 'key_<action> = <char>'\n"
    "# (e.g. 'key_sort = S'). The value must be exactly one non-space character; the\n"
    "# structural keys (enter, space, esc, arrows, digits 1-5) are fixed and not listed.\n"
    "# Two actions bound to the same key resolve first-match-wins - your problem to avoid.\n"
    "# key_quit = q\n"
    "# key_detail = d\n"
    "# key_search = /\n"
    "# key_sort = s\n"
    "# key_update = u\n"
    "# key_reason = r\n"
    "# key_filter = f\n"
    "# key_file_lookup = o\n"
    "# key_help = ?\n"
    "# key_collections_back = h\n"
    "# key_export_list = x\n"
    "# key_import_list = i\n"
    "# key_mark_all = a\n"
    "# key_clean_cache = c\n"
    "# key_pacdiff = m\n"
    "# key_refresh = y\n";

std::string Trim(const std::string& text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return "";
  }
  const auto last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

std::string ToLower(std::string text) {
  for (char& c : text) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return text;
}

void ApplyView(const std::string& value, Config& config) {
  const std::string v = ToLower(value);
  if (v == "browse") {
    config.view = model::View::Browse;
  } else if (v == "installed") {
    config.view = model::View::Installed;
  } else if (v == "updates") {
    config.view = model::View::Updates;
  } else if (v == "aur") {
    // Legacy: AUR was once a view. It is now a source, so land on Browse with the
    // AUR source pre-selected - the same packages, in the new model.
    config.view = model::View::Browse;
    config.source = model::Source::Aur;
  } else if (v == "collections") {
    config.view = model::View::Collections;
  } else if (v == "orphans") {
    config.view = model::View::Orphans;
  }
  // Anything else: leave the default in place.
}

void ApplySort(const std::string& value, Config& config) {
  const std::string v = ToLower(value);
  if (v == "size") {
    config.sort = model::Sort::SizeDescending;
  } else if (v == "name") {
    config.sort = model::Sort::NameAscending;
  }
}

// The initial SOURCES filter. Unknown values leave the default (All), like the
// rest of the lenient parser.
void ApplySource(const std::string& value, Config& config) {
  const std::string v = ToLower(value);
  if (v == "all") {
    config.source = model::Source::All;
  } else if (v == "pacman") {
    config.source = model::Source::Pacman;
  } else if (v == "aur") {
    config.source = model::Source::Aur;
  } else if (v == "flatpak") {
    config.source = model::Source::Flatpak;
  } else if (v == "homebrew" || v == "brew") {
    config.source = model::Source::Homebrew;
  } else if (v == "npm") {
    config.source = model::Source::Npm;
  } else if (v == "bun") {
    config.source = model::Source::Bun;
  } else if (v == "pnpm") {
    config.source = model::Source::Pnpm;
  }
}

// A `package_managers = pacman, aur, …` line is authoritative: the listed set is
// exactly what's surfaced, so start every optional manager off and enable only
// what's named. Pacman is implicit and always on. Unknown tokens are ignored.
void ApplyPackageManagers(const std::string& value, Config& config) {
  config.aur_enabled = false;
  config.flatpak_enabled = false;
  config.homebrew_enabled = false;
  config.npm_enabled = false;
  config.bun_enabled = false;
  config.pnpm_enabled = false;
  std::stringstream items(value);
  std::string item;
  while (std::getline(items, item, ',')) {
    const std::string token = ToLower(Trim(item));
    if (token == "aur") {
      config.aur_enabled = true;
    } else if (token == "flatpak") {
      config.flatpak_enabled = true;
    } else if (token == "homebrew" || token == "brew") {
      config.homebrew_enabled = true;
    } else if (token == "npm") {
      config.npm_enabled = true;
    } else if (token == "bun") {
      config.bun_enabled = true;
    } else if (token == "pnpm") {
      config.pnpm_enabled = true;
    }
    // "pacman" and anything unknown: no-op (pacman is always surfaced).
  }
}

void ApplyGlyphs(const std::string& value, Config& config) {
  const std::string v = ToLower(value);
  if (v == "ascii" || v == "compat" || v == "plain") {
    config.ascii_glyphs = true;
  } else if (v == "unicode" || v == "default") {
    config.ascii_glyphs = false;
  }
}

// Applies a single 'key_<action> = <char>' binding. `action` is the token after
// 'key_' (already lowercased by the caller); `value` is the raw, case-preserved
// value. Only a single non-space character rebinds the field - empty or
// multi-char values are ignored, keeping the default, as lenient as the rest of
// the parser. Unknown action tokens are ignored too.
void ApplyKeybinding(const std::string& action, const std::string& value, Config& config) {
  if (value.size() != 1) {
    return;  // empty / multi-char: keep the default
  }
  const char key = value[0];
  if (action == "quit") {
    config.keys.quit = key;
  } else if (action == "detail") {
    config.keys.detail = key;
  } else if (action == "search") {
    config.keys.search = key;
  } else if (action == "sort") {
    config.keys.sort = key;
  } else if (action == "update") {
    config.keys.update = key;
  } else if (action == "reason") {
    config.keys.reason = key;
  } else if (action == "filter") {
    config.keys.filter = key;
  } else if (action == "file_lookup") {
    config.keys.file_lookup = key;
  } else if (action == "help") {
    config.keys.help = key;
  } else if (action == "collections_back") {
    config.keys.collections_back = key;
  } else if (action == "export_list") {
    config.keys.export_list = key;
  } else if (action == "import_list") {
    config.keys.import_list = key;
  } else if (action == "mark_all") {
    config.keys.mark_all = key;
  } else if (action == "clean_cache") {
    config.keys.clean_cache = key;
  } else if (action == "pacdiff") {
    config.keys.pacdiff = key;
  } else if (action == "refresh") {
    config.keys.refresh = key;
  }
  // Unknown action tokens leave every default in place.
}

// Drops the commented template at `path` if its directory can be created and no
// file is already there. Best-effort: any failure is silently ignored.
void WriteTemplateIfMissing(const std::string& path) {
  std::error_code error;
  fs::create_directories(fs::path(path).parent_path(), error);
  if (error) {
    return;
  }
  std::ofstream out(path);
  if (out) {
    out << kTemplate;
  }
}

}  // namespace

std::string DefaultConfigPath() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0') {
    return std::string(xdg) + "/pacseek/config.ini";
  }
  if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
    return std::string(home) + "/.config/pacseek/config.ini";
  }
  return {};
}

std::string DefaultPackageListPath() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0') {
    return std::string(xdg) + "/pacseek/pkglist.txt";
  }
  if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
    return std::string(home) + "/.config/pacseek/pkglist.txt";
  }
  return {};
}

Config ParseConfig(const std::string& text) {
  Config config;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
      continue;
    }
    const auto equals = trimmed.find('=');
    if (equals == std::string::npos) {
      continue;
    }
    const std::string key = ToLower(Trim(trimmed.substr(0, equals)));
    const std::string value = Trim(trimmed.substr(equals + 1));

    if (key == "view") {
      ApplyView(value, config);
    } else if (key == "sort") {
      ApplySort(value, config);
    } else if (key == "source") {
      ApplySource(value, config);
    } else if (key == "aur_helper") {
      config.aur_helper = value;
    } else if (key == "theme") {
      config.theme = value;
    } else if (key == "package_managers") {
      ApplyPackageManagers(value, config);
    } else if (key == "glyphs") {
      ApplyGlyphs(value, config);
    } else if (key.rfind("key_", 0) == 0) {
      // A rebindable single-key action: 'key_<action> = <char>'.
      ApplyKeybinding(key.substr(4), value, config);
    }
    // Unknown keys are ignored so newer config files don't break older binaries.
  }
  return config;
}

Config LoadConfig() {
  const std::string path = DefaultConfigPath();
  if (path.empty()) {
    return {};
  }

  std::ifstream file(path);
  if (!file) {
    WriteTemplateIfMissing(path);
    Config defaults;
    defaults.first_run = true;  // no file yet: main runs the manager prompt
    return defaults;
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();
  return ParseConfig(buffer.str());
}

bool PersistFirstRunChoices(const Config& config) {
  const std::string path = DefaultConfigPath();
  if (path.empty()) {
    return false;
  }
  std::error_code error;
  fs::create_directories(fs::path(path).parent_path(), error);

  // Append an authoritative block beneath the commented template LoadConfig
  // already dropped; the live keys override the comments above them.
  std::ofstream out(path, std::ios::app);
  if (!out) {
    return false;
  }
  std::string managers = "pacman";
  if (config.aur_enabled) {
    managers += ", aur";
  }
  if (config.flatpak_enabled) {
    managers += ", flatpak";
  }
  if (config.homebrew_enabled) {
    managers += ", homebrew";
  }
  if (config.npm_enabled) {
    managers += ", npm";
  }
  if (config.bun_enabled) {
    managers += ", bun";
  }
  if (config.pnpm_enabled) {
    managers += ", pnpm";
  }
  out << "\n# --- written by the first-run setup ---\n"
      << "package_managers = " << managers << "\n"
      << "glyphs = " << (config.ascii_glyphs ? "ascii" : "unicode") << "\n";
  return out.good();
}

}  // namespace pacseek::config
