// package_source.hpp - the single seam between the UI and where packages come
// from. MockSource and AlpmSource both satisfy this; the app never knows which.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "model/collection.hpp"
#include "model/package.hpp"
#include "model/package_detail.hpp"

namespace pacseek::data {

// A pre-commit preview of removing a package with `pacman -Rs` semantics: the
// target plus every dependency that would become unneeded once it goes. Sources
// that cannot resolve the local graph leave it unavailable, and the UI simply
// skips the cascade prompt.
struct RemovalPreview {
  bool available = false;
  std::vector<std::string> packages;  // target first, then the freed deps
  int64_t reclaimed_bytes = 0;
};

// The answer to "which package owns/provides this file or command?" - the one
// find question name/description search can't answer. Sources that can't map a
// path to a package (mock/flatpak, or no libalpm handle) leave it unavailable.
struct FileOwnerResult {
  bool available = false;              // false when the source can't answer
  std::string query;                   // the normalized query echoed back
  std::vector<std::string> owners;     // package names owning/providing the file
  bool from_files_db = false;          // owners came from the sync files DB (-F), not the local DB (-Qo)
  std::string note;                    // hint/explanation, e.g. files DB absent → run `pacman -Fy`
};

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

  // A pre-commit preview of removing `package` with -Rs semantics: `package`
  // plus dependencies that would become unneeded. Default: unavailable, so a
  // source only implements it when it can walk the local dependency graph.
  virtual RemovalPreview PreviewRemoval(const model::Package& package) {
    (void)package;
    return {};
  }

  // The source's package groups exposed as collections (e.g. pacman's
  // base-devel), so they browse and install through the collections machinery.
  // Default: none, so a source only implements it when it has real groups.
  virtual std::vector<model::Collection> Groups() { return {}; }

  // Resolves a file path or bare command name to the package(s) that own it
  // (`pacman -Qo`) or, if none is installed, provide it (`pacman -F`). Reads the
  // files database only when it is already synced locally; never triggers a
  // `-Fy`. Default: unavailable, so a source only implements it when it can map
  // paths to packages.
  virtual FileOwnerResult FindFileOwner(const std::string& query) {
    (void)query;
    return {};
  }

  // When this source's package data was last synchronized with its remote, as
  // unix seconds (for libalpm, the newest sync-db mtime - i.e. the last -Sy).
  // The footer turns this into an honesty cue: update counts are only as fresh
  // as this moment. 0 = unknown/not applicable, and the footer stays quiet.
  virtual int64_t LastSyncSeconds() { return 0; }

  // True when this source cannot apply install/remove transactions, so the UI
  // renders the action buttons as informational only.
  virtual bool IsReadOnly() const = 0;

  // Short human label for the footer / status line (e.g. "libalpm", "mock").
  virtual std::string Name() const = 0;
};

}  // namespace pacseek::data
