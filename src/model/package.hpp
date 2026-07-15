// package.hpp - the core domain type and its pure helpers.
#pragma once

#include <cstdint>
#include <string>

#include <ftxui/screen/color.hpp>

namespace pacseek::model {

// The repository a package belongs to. Drives badge color and the AUR filter.
// Flatpak, Homebrew, and the JavaScript global managers (npm / bun / pnpm) are
// not pacman repos but ride the same machinery so their packages get a badge,
// legend entry, and footprint segment like everything else.
enum class Repo { Core, Extra, Multilib, Aur, Flatpak, Homebrew, Npm, Bun, Pnpm, Unknown };

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

// A user-facing package source: the ecosystem the SOURCES selector groups by.
// Pacman covers the core / extra / multilib repos as one entry; every other
// source maps to a single Repo. `All` clears the filter (matches everything).
enum class Source { All, Pacman, Aur, Flatpak, Homebrew, Npm, Bun, Pnpm };

// Repo <-> presentation helpers.
Repo RepoFromName(const std::string& name);
std::string RepoBadgeLabel(Repo repo);  // uppercase, for the badge / legend
ftxui::Color RepoColor(Repo repo);

// Source helpers. RepoInSource decides whether a package (by its repo) belongs to
// a selected source; SourceLabel is the uppercase selector/chip label; SourceColor
// is the source's identity color (pacman borrows core's orange; All is neutral).
bool RepoInSource(Repo repo, Source source);
std::string SourceLabel(Source source);
ftxui::Color SourceColor(Source source);

// A package is "heavy" when its installed footprint crosses the design's
// threshold; this is what turns the size text and storage bar orange.
bool IsHeavy(const Package& package);

// Formats a byte count the way the design specifies: "<n> MiB", stepping up to
// "<n.nn> GiB" at one gibibyte and "<n.nn> TiB" at one tebibyte (drive scale).
std::string FormatSize(int64_t bytes);

}  // namespace pacseek::model
