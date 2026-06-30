// package.hpp — the core domain type and its pure helpers.
#pragma once

#include <cstdint>
#include <string>

#include <ftxui/screen/color.hpp>

namespace pacseek::model {

// The repository a package belongs to. Drives badge color and the AUR filter.
enum class Repo { Core, Extra, Multilib, Aur, Unknown };

// One package as the UI cares about it. Sizes are stored in bytes so the libalpm
// source can hand over native values without lossy conversion.
struct Package {
  std::string name;
  std::string version;
  std::string description;
  Repo repo = Repo::Unknown;
  int64_t install_size_bytes = 0;   // on-disk footprint once installed
  int64_t download_size_bytes = 0;  // compressed download size
  bool installed = false;
  bool has_update = false;
};

// Repo <-> presentation helpers.
Repo RepoFromName(const std::string& name);
std::string RepoBadgeLabel(Repo repo);  // uppercase, for the badge / legend
ftxui::Color RepoColor(Repo repo);

// A package is "heavy" when its installed footprint crosses the design's
// threshold; this is what turns the size text and storage bar orange.
bool IsHeavy(const Package& package);

// Formats a byte count the way the design specifies: "<n> MiB", stepping up to
// "<n.nn> GiB" at one gibibyte and "<n.nn> TiB" at one tebibyte (drive scale).
std::string FormatSize(int64_t bytes);

}  // namespace pacseek::model
