// flatpak_source.hpp - installed flatpak applications as a PackageSource, read by
// shelling out to the `flatpak` CLI and parsing its tabular output. Packages are
// tagged Repo::Flatpak; transactions run `flatpak install`/`uninstall`.
//
// Only installed apps are surfaced (no remote search yet), so every row is
// removable. If flatpak isn't installed, LoadPackages simply returns nothing.
#pragma once

#include <string>
#include <vector>

#include "data/package_source.hpp"
#include "model/package.hpp"

namespace pacseek::data {

class FlatpakSource : public PackageSource {
 public:
  std::vector<model::Package> LoadPackages() override;
  model::PackageDetail Describe(const model::Package& package) override;
  bool IsReadOnly() const override { return false; }
  std::string Name() const override { return "flatpak"; }
};

}  // namespace pacseek::data
