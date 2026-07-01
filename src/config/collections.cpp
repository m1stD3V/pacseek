#include "config/collections.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace pacseek::config {

namespace {

namespace fs = std::filesystem;

// Dropped on first run so the format is discoverable. Every line is commented,
// so loading it yields no collections and no errors - the same as no file.
constexpr char kTemplate[] =
    "# pacseek user-defined collections\n"
    "# Each [section] defines a collection; the section name is its id. Back this\n"
    "# file up alongside config.ini to carry your collections to a new install.\n"
    "#\n"
    "# Malformed collections (missing name, empty package entry, duplicate id) are\n"
    "# a hard error that names the offender and stops pacseek from starting.\n"
    "# Packages that aren't installed or in the repos simply show as \"unavailable\".\n"
    "#\n"
    "# [my-setup]\n"
    "# name = My Setup\n"
    "# icon = \xe2\x98\x85\n"
    "# description = My personal must-haves\n"
    "# packages = neovim, git, tmux, ripgrep\n";

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

// A collection being assembled from consecutive key lines, plus the source
// positions needed to name offenders precisely in error messages.
struct Pending {
  bool active = false;      // a [section] has been opened
  std::string id;           // section name
  int id_line = 0;          // line the [section] header was on
  model::Collection collection;
  bool packages_seen = false;
  int packages_line = 0;
};

// Drops the commented template at `path` if its directory exists (or can be
// created) and no file is already there. Best-effort: failures are ignored.
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

std::string DefaultCollectionsPath() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0') {
    return std::string(xdg) + "/pacseek/collections.ini";
  }
  if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
    return std::string(home) + "/.config/pacseek/collections.ini";
  }
  return {};
}

CollectionsResult ParseCollections(const std::string& text) {
  CollectionsResult result;
  std::set<std::string> seen_ids;

  // Finalizes the pending collection: validates it and either appends it to the
  // result or records the reasons it was rejected.
  auto commit = [&](Pending& pending) {
    if (!pending.active) {
      return;
    }
    if (pending.id.empty()) {
      // The empty-id error was already recorded at the header; nothing to add.
      return;
    }
    if (!seen_ids.insert(pending.id).second) {
      result.errors.push_back({pending.id, pending.id_line, "duplicate collection id"});
      return;
    }
    if (Trim(pending.collection.name).empty()) {
      result.errors.push_back({pending.id, pending.id_line, "collection is missing a 'name'"});
    }
    if (!pending.packages_seen || pending.collection.packages.empty()) {
      result.errors.push_back({pending.id, pending.id_line, "collection has no packages"});
    }
    if (pending.collection.icon.empty()) {
      pending.collection.icon = "\xe2\x96\xb8";  // ▸ default glyph
    }
    // Only keep it when this section contributed no errors of its own; a single
    // bad collection rejects the file regardless (see LoadUserCollections).
    result.collections.push_back(std::move(pending.collection));
  };

  std::istringstream stream(text);
  std::string line;
  int line_number = 0;
  Pending pending;
  while (std::getline(stream, line)) {
    ++line_number;
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
      continue;
    }

    if (trimmed.front() == '[') {
      if (trimmed.back() != ']') {
        result.errors.push_back({pending.id, line_number, "malformed section header (expected [id])"});
        continue;
      }
      commit(pending);
      pending = Pending{};
      pending.active = true;
      pending.id = Trim(trimmed.substr(1, trimmed.size() - 2));
      pending.id_line = line_number;
      pending.collection.id = pending.id;
      if (pending.id.empty()) {
        result.errors.push_back({"", line_number, "empty collection id in section header"});
      }
      continue;
    }

    const auto equals = trimmed.find('=');
    if (equals == std::string::npos) {
      result.errors.push_back({pending.id, line_number, "line is neither a [section] nor a key = value"});
      continue;
    }
    if (!pending.active) {
      result.errors.push_back({"", line_number, "key appears before any [collection] section"});
      continue;
    }

    const std::string key = ToLower(Trim(trimmed.substr(0, equals)));
    const std::string value = Trim(trimmed.substr(equals + 1));
    if (key == "name") {
      pending.collection.name = value;
    } else if (key == "icon") {
      pending.collection.icon = value;
    } else if (key == "description") {
      pending.collection.description = value;
    } else if (key == "packages") {
      pending.packages_seen = true;
      pending.packages_line = line_number;
      std::stringstream items(value);
      std::string item;
      while (std::getline(items, item, ',')) {
        const std::string name = Trim(item);
        if (name.empty()) {
          result.errors.push_back({pending.id, line_number, "empty package name in 'packages' (stray comma?)"});
          continue;
        }
        pending.collection.packages.push_back(name);
      }
    }
    // Unknown keys are ignored so newer files don't break older binaries.
  }
  commit(pending);

  // Any error rejects the whole file: a partially-loaded collection set is more
  // confusing than a clear failure the user can fix.
  if (!result.errors.empty()) {
    result.collections.clear();
  }
  return result;
}

CollectionsResult LoadUserCollections() {
  const std::string path = DefaultCollectionsPath();
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
  return ParseCollections(buffer.str());
}

std::string FormatCollectionError(const CollectionError& error) {
  std::string out = "collections.ini";
  if (!error.collection.empty()) {
    out += " [" + error.collection + "]";
  }
  if (error.line > 0) {
    out += " (line " + std::to_string(error.line) + ")";
  }
  out += ": " + error.message;
  return out;
}

}  // namespace pacseek::config
