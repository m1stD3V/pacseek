// homebrew_source.hpp - installed Homebrew formulae/casks as a PackageSource,
// read by shelling out to the `brew` CLI. Packages are tagged Repo::Homebrew;
// transactions run `brew install`/`brew uninstall` (no sudo - brew owns its own
// prefix as the invoking user).
//
// Only installed packages are surfaced (no remote search yet), so every row is
// removable. If brew isn't installed, LoadPackages simply returns nothing. This
// is the Linuxbrew case on an Arch box, sitting alongside pacman + AUR.
#pragma once

#include <string>
#include <vector>

#include "data/package_source.hpp"
#include "model/package.hpp"

namespace pacseek::data {

class HomebrewSource : public PackageSource {
 public:
  std::vector<model::Package> LoadPackages() override;
  model::PackageDetail Describe(const model::Package& package) override;
  bool IsReadOnly() const override { return false; }
  std::string Name() const override { return "brew"; }
};

}  // namespace pacseek::data
