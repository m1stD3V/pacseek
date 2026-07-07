// package.hpp - the core domain type and its pure helpers.
#pragma once

#include <cstdint>
#include <string>

#include <ftxui/screen/color.hpp>

namespace pacseek::model {

// The repository a package belongs to. Drives badge color and the AUR filter.
// Flatpak and Homebrew are not pacman repos but ride the same machinery so their
// apps get a badge, legend entry, and footprint segment like everything else.
enum class Repo { Core, Extra, Multilib, Aur, Flatpak, Homebrew, Unknown };

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
  // The next two are meaningful only when `installed`; a sync-only package
  // leaves both false.
  bool explicit_install = false;  // installed with reason EXPLICIT (vs. a dep)
  bool is_orphan = false;         // a dependency nothing needs - `pacman -Qdt`

  // AUR trust signals, filled only on live RPC search results (local rows keep
  // the sentinels). Votes render in place of the meaningless 0-byte size, and
  // the out-of-date flag gets its own row badge.
  int aur_votes = -1;             // NumVotes; -1 = not a live AUR result
  double aur_popularity = -1.0;   // Popularity; drives the default result order
  bool aur_out_of_date = false;   // maintainer-flagged out-of-date
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
