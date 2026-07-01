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
    "# Initial view when pacseek starts: browse | installed | updates | aur | collections\n"
    "# view = browse\n"
    "\n"
    "# Initial sort order: size | name\n"
    "# sort = size\n"
    "\n"
    "# Preferred AUR helper, overriding auto-detection.\n"
    "# Auto-detected in order: paru, yay, pikaur, aura, trizen, pamac.\n"
    "# aur_helper = paru\n"
    "\n"
    "# Color theme: default | tokyo-night | catppuccin-mocha | catppuccin-macchiato | gruvbox\n"
    "# theme = tokyo-night\n";

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
    config.view = model::View::Aur;
  } else if (v == "collections") {
    config.view = model::View::Collections;
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
    } else if (key == "aur_helper") {
      config.aur_helper = value;
    } else if (key == "theme") {
      config.theme = value;
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
    return {};
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();
  return ParseConfig(buffer.str());
}

}  // namespace pacseek::config
