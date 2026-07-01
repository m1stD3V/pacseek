#include "data/composite_source.hpp"

#include <utility>

namespace pacseek::data {

CompositeSource::CompositeSource(std::vector<std::unique_ptr<PackageSource>> sources)
    : sources_(std::move(sources)) {}

std::vector<model::Package> CompositeSource::LoadPackages() {
  std::vector<model::Package> combined;
  owner_.clear();
  for (const std::unique_ptr<PackageSource>& source : sources_) {
    for (model::Package& package : source->LoadPackages()) {
      // First source to claim a name owns it (names rarely clash: flatpak uses
      // reverse-DNS ids, pacman uses short names).
      owner_.emplace(package.name, source.get());
      combined.push_back(std::move(package));
    }
  }
  return combined;
}

model::PackageDetail CompositeSource::Describe(const model::Package& package) {
  const auto it = owner_.find(package.name);
  PackageSource* source = it != owner_.end() ? it->second
                          : sources_.empty() ? nullptr
                                             : sources_.front().get();
  if (source == nullptr) {
    model::PackageDetail detail;
    detail.name = package.name;
    detail.version = package.version;
    detail.description = package.description;
    detail.repo = package.repo;
    return detail;
  }
  return source->Describe(package);
}

bool CompositeSource::IsReadOnly() const {
  for (const std::unique_ptr<PackageSource>& source : sources_) {
    if (!source->IsReadOnly()) {
      return false;
    }
  }
  return true;
}

std::string CompositeSource::Name() const {
  std::string name;
  for (const std::unique_ptr<PackageSource>& source : sources_) {
    if (!name.empty()) {
      name += " + ";
    }
    name += source->Name();
  }
  return name;
}

}  // namespace pacseek::data
