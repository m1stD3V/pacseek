// catalog.hpp - owns the package set and answers every derived question the UI
// asks: which rows are visible, how the nav/legend counts add up, and the disk
// footprint breakdown. Pure logic, no I/O - trivially testable.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "model/package.hpp"

namespace pacseek::model {

// Which slice of the catalog the user is looking at (the LIBRARY nav).
// Collections is special: it is not a per-package predicate but a picker over
// curated name sets (see model/collection.hpp), so it filters via VisibleInSet
// rather than the view machinery.
enum class View { Browse, Installed, Updates, Aur, Collections, Orphans };

// Row ordering toggled by the SORT control.
enum class Sort { SizeDescending, NameAscending };

// Installed-size totals per repo, used to draw the stacked footprint bar.
struct RepoFootprint {
  Repo repo;
  int64_t install_size_bytes;
};

class Catalog {
 public:
  Catalog() = default;
  explicit Catalog(std::vector<Package> packages);

  void SetPackages(std::vector<Package> packages);
  const std::vector<Package>& All() const { return packages_; }

  // Rows to display for the given view, narrowed by a case-insensitive search
  // over name + description, then ordered by the sort mode. Returns pointers
  // into the owned package list (stable for the lifetime of the catalog).
  std::vector<const Package*> Visible(View view, const std::string& query, Sort sort) const;

  // Rows for a curated collection: the packages whose names are in `names` and
  // that match the search, ordered by the sort mode. Members not present in the
  // catalog (e.g. an AUR package not yet installed) are silently skipped - see
  // MembershipCounts for how many that is.
  std::vector<const Package*> VisibleInSet(const std::vector<std::string>& names,
                                           const std::string& query, Sort sort) const;

  // How many of `names` exist in the catalog, and how many of those are
  // installed - drives the "n of m available" line on each collection card.
  struct Membership {
    int available;
    int installed;
  };
  Membership MembershipCounts(const std::vector<std::string>& names) const;

  // Nav counts reflect the whole dataset, independent of the active search.
  int CountForView(View view) const;

  // Pending updates for pacman-managed packages only (flatpak excluded). This is
  // the count the partial-upgrade guard cares about: installing with -S while
  // *pacman* updates wait risks breakage, whereas a stale flatpak does not.
  int PacmanUpdateCount() const;

  // Installed-package count for one repo (the REPOSITORIES legend).
  int InstalledCountForRepo(Repo repo) const;

  // The largest installed footprint across the whole dataset; storage bars are
  // normalized to this so they stay comparable between views.
  int64_t MaxInstallSizeBytes() const;

  // DISK FOOTPRINT card figures.
  int64_t TotalInstalledBytes() const;
  int InstalledCount() const;
  std::vector<RepoFootprint> InstalledFootprintByRepo() const;

  // Orphans: installed dependencies nothing needs (`pacman -Qdt`). OrphanCount
  // drives the footprint-card reclaim line; ReclaimableBytes sums their on-disk
  // footprint - the space freed by removing them.
  int OrphanCount() const;
  int64_t ReclaimableBytes() const;

 private:
  std::vector<Package> packages_;
};

}  // namespace pacseek::model
