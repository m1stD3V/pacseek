// package_source.hpp - the single seam between the UI and where packages come
// from. MockSource and AlpmSource both satisfy this; the app never knows which.
#pragma once

#include <string>
#include <vector>

#include "model/package.hpp"
#include "model/package_detail.hpp"

namespace pacseek::data {

class PackageSource {
 public:
  virtual ~PackageSource() = default;

  // Pulls the full package set the catalog will own. May be slow (DB / network),
  // so the app calls it once up front rather than per frame.
  virtual std::vector<model::Package> LoadPackages() = 0;

  // Expands one row into its dependencies, files, and provenance, loaded lazily
  // when the user opens the detail pane. Implementations that cannot describe the
  // package (e.g. an un-built AUR result) return a detail with available=false,
  // still echoing the row's identity fields so the pane has something to show.
  virtual model::PackageDetail Describe(const model::Package& package) = 0;

  // True when this source cannot apply install/remove transactions, so the UI
  // renders the action buttons as informational only.
  virtual bool IsReadOnly() const = 0;

  // Short human label for the footer / status line (e.g. "libalpm", "mock").
  virtual std::string Name() const = 0;
};

}  // namespace pacseek::data
