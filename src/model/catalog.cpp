#include "model/catalog.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace pacseek::model {

namespace {

std::string ToLowerAscii(const std::string& text) {
  std::string lowered = text;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lowered;
}

bool MatchesView(const Package& package, View view) {
  switch (view) {
    case View::Browse:
      return true;
    case View::Installed:
      return package.installed;
    case View::Updates:
      return package.has_update;
    case View::Aur:
      return package.repo == Repo::Aur;
    case View::Collections:
      // Collections never filter through the view predicate; they go through
      // VisibleInSet. Listing the case keeps the switch exhaustive.
      return false;
  }
  return false;
}

bool MatchesQuery(const Package& package, const std::string& lowered_query) {
  if (lowered_query.empty()) {
    return true;
  }
  return ToLowerAscii(package.name).find(lowered_query) != std::string::npos ||
         ToLowerAscii(package.description).find(lowered_query) != std::string::npos;
}

}  // namespace

Catalog::Catalog(std::vector<Package> packages) : packages_(std::move(packages)) {}

void Catalog::SetPackages(std::vector<Package> packages) {
  packages_ = std::move(packages);
}

std::vector<const Package*> Catalog::Visible(View view, const std::string& query,
                                             Sort sort) const {
  const std::string lowered_query = ToLowerAscii(query);

  std::vector<const Package*> visible;
  for (const Package& package : packages_) {
    if (MatchesView(package, view) && MatchesQuery(package, lowered_query)) {
      visible.push_back(&package);
    }
  }

  if (sort == Sort::SizeDescending) {
    std::sort(visible.begin(), visible.end(), [](const Package* a, const Package* b) {
      return a->install_size_bytes > b->install_size_bytes;
    });
  } else {
    std::sort(visible.begin(), visible.end(),
              [](const Package* a, const Package* b) { return a->name < b->name; });
  }
  return visible;
}

std::vector<const Package*> Catalog::VisibleInSet(const std::vector<std::string>& names,
                                                  const std::string& query, Sort sort) const {
  const std::string lowered_query = ToLowerAscii(query);
  const std::unordered_set<std::string> wanted(names.begin(), names.end());

  std::vector<const Package*> visible;
  for (const Package& package : packages_) {
    if (wanted.count(package.name) != 0 && MatchesQuery(package, lowered_query)) {
      visible.push_back(&package);
    }
  }

  if (sort == Sort::SizeDescending) {
    std::sort(visible.begin(), visible.end(), [](const Package* a, const Package* b) {
      return a->install_size_bytes > b->install_size_bytes;
    });
  } else {
    std::sort(visible.begin(), visible.end(),
              [](const Package* a, const Package* b) { return a->name < b->name; });
  }
  return visible;
}

Catalog::Membership Catalog::MembershipCounts(const std::vector<std::string>& names) const {
  const std::unordered_set<std::string> wanted(names.begin(), names.end());
  Membership counts{0, 0};
  for (const Package& package : packages_) {
    if (wanted.count(package.name) == 0) {
      continue;
    }
    ++counts.available;
    if (package.installed) {
      ++counts.installed;
    }
  }
  return counts;
}

int Catalog::CountForView(View view) const {
  int count = 0;
  for (const Package& package : packages_) {
    if (MatchesView(package, view)) {
      ++count;
    }
  }
  return count;
}

int Catalog::InstalledCountForRepo(Repo repo) const {
  int count = 0;
  for (const Package& package : packages_) {
    if (package.installed && package.repo == repo) {
      ++count;
    }
  }
  return count;
}

int64_t Catalog::MaxInstallSizeBytes() const {
  int64_t max_bytes = 1;  // floor of 1 keeps bar math division-safe
  for (const Package& package : packages_) {
    max_bytes = std::max(max_bytes, package.install_size_bytes);
  }
  return max_bytes;
}

int64_t Catalog::TotalInstalledBytes() const {
  int64_t total = 0;
  for (const Package& package : packages_) {
    if (package.installed) {
      total += package.install_size_bytes;
    }
  }
  return total;
}

int Catalog::InstalledCount() const {
  return CountForView(View::Installed);
}

std::vector<RepoFootprint> Catalog::InstalledFootprintByRepo() const {
  constexpr std::array<Repo, 5> kOrderedRepos = {Repo::Core, Repo::Extra, Repo::Aur,
                                                 Repo::Multilib, Repo::Flatpak};
  std::vector<RepoFootprint> footprints;
  for (Repo repo : kOrderedRepos) {
    int64_t bytes = 0;
    for (const Package& package : packages_) {
      if (package.installed && package.repo == repo) {
        bytes += package.install_size_bytes;
      }
    }
    if (bytes > 0) {
      footprints.push_back({repo, bytes});
    }
  }
  return footprints;
}

}  // namespace pacseek::model
