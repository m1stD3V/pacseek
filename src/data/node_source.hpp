// node_source.hpp - globally-installed JavaScript packages (npm / bun / pnpm) as
// a PackageSource. Each enabled manager's global node_modules is walked directly
// on the filesystem: names and versions come from each package's package.json,
// real install sizes from a recursive byte count - so the storage-first UI treats
// a global `typescript` exactly like a pacman package.
//
// Read locally only; no registry access, so no update detection (that would need
// the network, which pacseek keeps user-initiated). Packages are tagged
// Repo::Npm / Bun / Pnpm; transactions run `<mgr> add/remove -g` as the invoking
// user (no sudo - the global prefix is expected to be user-writable).
#pragma once

#include <string>
#include <vector>

#include "data/package_source.hpp"
#include "model/package.hpp"

namespace pacseek::data {

class NodeSource : public PackageSource {
 public:
  NodeSource(bool npm, bool bun, bool pnpm) : npm_(npm), bun_(bun), pnpm_(pnpm) {}

  std::vector<model::Package> LoadPackages() override;
  model::PackageDetail Describe(const model::Package& package) override;
  bool IsReadOnly() const override { return false; }
  std::string Name() const override { return "node"; }

 private:
  bool npm_ = false;
  bool bun_ = false;
  bool pnpm_ = false;
};

}  // namespace pacseek::data
