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
  std::vector<std::string> licenses;
  int64_t install_size_bytes = 0;

  // Relationships.
  std::vector<std::string> depends;
  std::vector<std::string> optdepends;  // "name: reason"
  std::vector<std::string> required_by;

  // Owned files (absolute paths). Only populated for installed packages; for
  // others `files_note` carries the reason the list is empty.
  std::vector<std::string> files;
  std::string files_note;
};

}  // namespace pacseek::model
