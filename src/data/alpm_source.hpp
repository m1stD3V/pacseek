// alpm_source.hpp - read-only package data from pacman's own libalpm: the local
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
  // Re-opens libalpm to gather one package's depends / files / provenance,
  // preferring the local (installed) entry so files and install metadata are
  // available, and falling back to the sync entry otherwise.
  model::PackageDetail Describe(const model::Package& package) override;
  // Computes the -Rs removal closure over the local database: the target plus
  // installed dependencies that nothing outside the set still requires. Used to
  // preview a cascade before committing. Null-safe and bounded; unavailable if
  // libalpm cannot be opened.
  RemovalPreview PreviewRemoval(const model::Package& package) override;
  // Gathers pacman's package groups (base-devel, …) from the sync databases as
  // collections, merging members across repos. Re-opens libalpm once, like the
  // other loads; returns empty if the handle cannot be opened.
  std::vector<model::Collection> Groups() override;
  // Resolves a file path / command name to its owning package. Scans the local
  // filelists first (`pacman -Qo` semantics); failing that, and only if the
  // sync files database is already present on disk, scans it (`pacman -F`)
  // without ever triggering a `-Fy` sync. Null-safe and bounded.
  FileOwnerResult FindFileOwner(const std::string& query) override;
  // The newest sync-db mtime under db_path/sync - i.e. when `pacman -Sy` last
  // ran. 0 when no sync database exists or none is statable.
  int64_t LastSyncSeconds() override;
  // The live system can be modified: install/remove shell out to pacman (and an
  // AUR helper) with privilege escalation. See system/transaction.
  bool IsReadOnly() const override { return false; }
  std::string Name() const override { return "libalpm"; }

 private:
  std::string root_;
  std::string db_path_;
};

}  // namespace pacseek::data
