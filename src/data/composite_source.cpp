#include "data/composite_source.hpp"

#include <algorithm>
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

RemovalPreview CompositeSource::PreviewRemoval(const model::Package& package) {
  // Same origin lookup as Describe: the owning source knows the local graph; an
  // unknown owner (or none) yields the default unavailable preview.
  const auto it = owner_.find(package.name);
  PackageSource* source = it != owner_.end() ? it->second
                          : sources_.empty() ? nullptr
                                             : sources_.front().get();
  if (source == nullptr) {
    return {};
  }
  return source->PreviewRemoval(package);
}

std::vector<model::Collection> CompositeSource::Groups() {
  std::vector<model::Collection> combined;
  for (const std::unique_ptr<PackageSource>& source : sources_) {
    for (model::Collection& group : source->Groups()) {
      combined.push_back(std::move(group));
    }
  }
  return combined;
}

FileOwnerResult CompositeSource::FindFileOwner(const std::string& query) {
  // No package name to route by, so poll every child and take the first that can
  // answer (available). Mock/flatpak return the default unavailable result, so
  // in practice this lands on the libalpm source.
  for (const std::unique_ptr<PackageSource>& source : sources_) {
    FileOwnerResult result = source->FindFileOwner(query);
    if (result.available) {
      return result;
    }
  }
  return {};
}

int64_t CompositeSource::LastSyncSeconds() {
  int64_t newest = 0;
  for (const std::unique_ptr<PackageSource>& source : sources_) {
    newest = std::max(newest, source->LastSyncSeconds());
  }
  return newest;
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
