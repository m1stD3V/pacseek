// alpm_source.hpp — read-only package data from pacman's own libalpm: the local
// database (what's installed) joined against the sync databases (what's
// available, sizes, and newer versions). No transactions are performed.
#pragma once

#include <string>
#include <vector>

#include "data/package_source.hpp"
#include "model/package.hpp"

namespace pacseek::data {

class AlpmSource : public PackageSource {
 public:
  // Defaults mirror a standard Arch install; overridable for testing.
  AlpmSource(std::string root = "/", std::string db_path = "/var/lib/pacman/");

  // Throws std::runtime_error if libalpm cannot be initialized (e.g. the db path
  // is unreadable); the caller can then suggest `--mock`.
  std::vector<model::Package> LoadPackages() override;
  bool IsReadOnly() const override { return true; }
  std::string Name() const override { return "libalpm"; }

 private:
  std::string root_;
  std::string db_path_;
};

}  // namespace pacseek::data
