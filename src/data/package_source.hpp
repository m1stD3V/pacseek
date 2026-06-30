// package_source.hpp — the single seam between the UI and where packages come
// from. MockSource and AlpmSource both satisfy this; the app never knows which.
#pragma once

#include <string>
#include <vector>

#include "model/package.hpp"

namespace pacseek::data {

class PackageSource {
 public:
  virtual ~PackageSource() = default;

  // Pulls the full package set the catalog will own. May be slow (DB / network),
  // so the app calls it once up front rather than per frame.
  virtual std::vector<model::Package> LoadPackages() = 0;

  // True when this source cannot apply install/remove transactions, so the UI
  // renders the action buttons as informational only.
  virtual bool IsReadOnly() const = 0;

  // Short human label for the footer / status line (e.g. "libalpm", "mock").
  virtual std::string Name() const = 0;
};

}  // namespace pacseek::data
