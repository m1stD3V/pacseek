// package_detail.hpp - the expanded, on-demand view of a single package:
// dependencies, files, and provenance. Loaded lazily when the user opens the
// detail pane (libalpm file lists are large), so it is kept separate from the
// lean Package the catalog holds for every row.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "model/package.hpp"

namespace pacseek::model {

struct PackageDetail {
  // False when the source could not describe the package (e.g. an un-built AUR
  // search result). The identity fields below are still filled from the row so
  // the pane is never blank; `note` explains the gap.
  bool available = false;
  std::string note;

  // Identity.
  std::string name;
  std::string version;
  std::string description;
  Repo repo = Repo::Unknown;

  // Provenance.
  std::string url;
  std::string packager;
  std::string build_date;       // formatted local time, empty if unknown
  std::string install_date;     // formatted, empty when not installed
  std::string install_reason;   // "explicit" / "dependency", empty when not installed
  // For an installed package pulled in as a dependency, the shortest chain from
  // an explicitly-installed root down to it, joined with " → " (e.g.
  // "gnome → gvfs"). "explicitly installed" when it is an explicit root,
  // "pulled in as a dependency" when no explicit root can be traced, and empty
  // when the package is not installed / unknown.
  std::string why_installed;
  std::vector<std::string> licenses;
  int64_t install_size_bytes = 0;

  // Marginal install cost: for a not-installed sync package only, the true disk
  // cost of adding it - how many of its dependencies aren't already present and
  // the install / download size of the target plus those new packages. An
  // estimate over the sync dependency closure (ignores version-pinning nuance
  // and conflicts); `marginal_computed` gates whether the pane shows it.
  bool marginal_computed = false;      // true only for a not-installed sync package
  int new_dep_count = 0;               // deps in the closure not already installed
  int64_t marginal_install_bytes = 0;  // install size of this package + those new deps
  int64_t marginal_download_bytes = 0; // download size to fetch the new packages

  // Relationships.
  std::vector<std::string> depends;
  std::vector<std::string> optdepends;  // "name: reason"
  std::vector<std::string> provides;
  std::vector<std::string> conflicts;
  std::vector<std::string> replaces;
  std::vector<std::string> required_by;

  // Owned files (absolute paths). Only populated for installed packages; for
  // others `files_note` carries the reason the list is empty.
  std::vector<std::string> files;
  std::string files_note;

  // AUR metadata (RPC type=info), fetched asynchronously for Repo::Aur rows and
  // merged in once the worker answers; the pane shows a fetching note meanwhile.
  bool aur_info_loaded = false;   // the fields below are real
  bool aur_info_pending = false;  // a fetch is in flight
  std::string aur_info_error;     // fetch failed; shown as a note
  int aur_votes = -1;
  double aur_popularity = -1.0;
  std::string aur_maintainer;     // empty when loaded = orphaned package
  std::string aur_last_modified;  // formatted local date
  std::string aur_out_of_date;    // formatted date when flagged, else empty
};

}  // namespace pacseek::model
