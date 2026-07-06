// composite_source.hpp - presents several PackageSources as one. LoadPackages
// concatenates them (first source wins on a name clash) and remembers which
// source produced each package so Describe can route back to it. Used to show
// libalpm (pacman + AUR) and flatpak packages in a single catalog.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/package_source.hpp"
#include "model/package.hpp"

namespace pacseek::data {

class CompositeSource : public PackageSource {
 public:
  explicit CompositeSource(std::vector<std::unique_ptr<PackageSource>> sources);

  std::vector<model::Package> LoadPackages() override;
  model::PackageDetail Describe(const model::Package& package) override;
  // Routes to the source that produced the package, exactly like Describe, so a
  // removal cascade is computed by the owning (libalpm) source.
  RemovalPreview PreviewRemoval(const model::Package& package) override;
  // Concatenates every child source's groups, so pacman's groups and any future
  // grouped source appear together in the collections picker.
  std::vector<model::Collection> Groups() override;
  // File ownership isn't tied to a catalog package, so ask each child in turn
  // and return the first source that can actually answer (libalpm, in practice).
  FileOwnerResult FindFileOwner(const std::string& query) override;
  // The freshest child answer; children without a sync notion report 0.
  int64_t LastSyncSeconds() override;
  // Writable if any underlying source is; the read-only sources just no-op.
  bool IsReadOnly() const override;
  // The child names joined, e.g. "libalpm + flatpak".
  std::string Name() const override;

 private:
  std::vector<std::unique_ptr<PackageSource>> sources_;
  std::unordered_map<std::string, PackageSource*> owner_;  // package name -> source
};

}  // namespace pacseek::data
