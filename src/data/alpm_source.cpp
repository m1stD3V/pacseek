#include "data/alpm_source.hpp"

#include <alpm.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace pacseek::data {

namespace {

namespace fs = std::filesystem;

// Where pacman keeps its sync database files; each "<repo>.db" is one repo.
const fs::path kSyncSubdir = "sync";
const std::string kSyncDbExtension = ".db";

// Preferred display order for the well-known repos; anything else follows.
const std::vector<std::string> kPreferredRepoOrder = {"core", "extra", "multilib"};

// RAII guard so the libalpm handle is always released, even on exceptions.
struct AlpmHandle {
  alpm_handle_t* handle = nullptr;
  ~AlpmHandle() {
    if (handle != nullptr) {
      alpm_release(handle);
    }
  }
};

std::string SafeString(const char* text) {
  return text != nullptr ? std::string(text) : std::string();
}

// Discovers sync repo names from the *.db files on disk, returning them with the
// well-known repos first so the UI ordering is stable and familiar.
std::vector<std::string> DiscoverSyncRepoNames(const std::string& db_path) {
  const fs::path sync_dir = fs::path(db_path) / kSyncSubdir;
  std::vector<std::string> repo_names;
  std::error_code error;
  for (const fs::directory_entry& entry : fs::directory_iterator(sync_dir, error)) {
    const fs::path& file = entry.path();
    if (file.extension() == kSyncDbExtension) {
      repo_names.push_back(file.stem().string());
    }
  }

  std::sort(repo_names.begin(), repo_names.end(), [](const std::string& a, const std::string& b) {
    auto rank = [](const std::string& name) {
      auto it = std::find(kPreferredRepoOrder.begin(), kPreferredRepoOrder.end(), name);
      return it == kPreferredRepoOrder.end()
                 ? kPreferredRepoOrder.size()
                 : static_cast<size_t>(it - kPreferredRepoOrder.begin());
    };
    const size_t rank_a = rank(a);
    const size_t rank_b = rank(b);
    return rank_a != rank_b ? rank_a < rank_b : a < b;
  });
  return repo_names;
}

// The installed picture for one package, gathered from the local database.
struct InstalledRecord {
  std::string version;
  int64_t install_size_bytes;
};

std::unordered_map<std::string, InstalledRecord> ReadLocalDatabase(alpm_handle_t* handle) {
  std::unordered_map<std::string, InstalledRecord> installed;
  alpm_db_t* local_db = alpm_get_localdb(handle);
  if (local_db == nullptr) {
    return installed;
  }
  for (alpm_list_t* node = alpm_db_get_pkgcache(local_db); node != nullptr; node = node->next) {
    auto* package = static_cast<alpm_pkg_t*>(node->data);
    installed.emplace(SafeString(alpm_pkg_get_name(package)),
                      InstalledRecord{SafeString(alpm_pkg_get_version(package)),
                                      static_cast<int64_t>(alpm_pkg_get_isize(package))});
  }
  return installed;
}

// The fields shared by every package, regardless of where it was found.
model::Package BasePackage(alpm_pkg_t* alpm_package) {
  model::Package package;
  package.name = SafeString(alpm_pkg_get_name(alpm_package));
  package.version = SafeString(alpm_pkg_get_version(alpm_package));
  package.description = SafeString(alpm_pkg_get_desc(alpm_package));
  package.install_size_bytes = static_cast<int64_t>(alpm_pkg_get_isize(alpm_package));
  return package;
}

// Builds a Package from a sync-database entry, joining in installed state and
// flagging an update when the installed version is older than the sync version.
model::Package PackageFromSyncEntry(alpm_pkg_t* sync_package, const std::string& repo_name,
                                    const std::unordered_map<std::string, InstalledRecord>& installed) {
  model::Package package = BasePackage(sync_package);
  package.repo = model::RepoFromName(repo_name);
  package.download_size_bytes = static_cast<int64_t>(alpm_pkg_get_size(sync_package));

  auto installed_it = installed.find(package.name);
  package.installed = installed_it != installed.end();
  if (package.installed) {
    const std::string& installed_version = installed_it->second.version;
    package.has_update = alpm_pkg_vercmp(package.version.c_str(), installed_version.c_str()) > 0;
  }
  return package;
}

// Installed packages absent from every sync repo are "foreign" (AUR or built by
// hand). Appends them so they still appear, sized from the local database and
// labelled AUR. `seen` holds the names already taken from the sync repos.
void AppendForeignPackages(alpm_handle_t* handle, const std::unordered_set<std::string>& seen,
                           std::vector<model::Package>& packages) {
  alpm_db_t* local_db = alpm_get_localdb(handle);
  if (local_db == nullptr) {
    return;
  }
  for (alpm_list_t* node = alpm_db_get_pkgcache(local_db); node != nullptr; node = node->next) {
    auto* local_package = static_cast<alpm_pkg_t*>(node->data);
    model::Package package = BasePackage(local_package);
    if (seen.count(package.name) != 0) {
      continue;
    }
    package.repo = model::Repo::Aur;
    package.installed = true;
    packages.push_back(std::move(package));
  }
}

}  // namespace

AlpmSource::AlpmSource(std::string root, std::string db_path)
    : root_(std::move(root)), db_path_(std::move(db_path)) {}

std::vector<model::Package> AlpmSource::LoadPackages() {
  AlpmHandle guard;
  alpm_errno_t init_error = ALPM_ERR_OK;
  guard.handle = alpm_initialize(root_.c_str(), db_path_.c_str(), &init_error);
  if (guard.handle == nullptr) {
    throw std::runtime_error(std::string("libalpm init failed: ") + alpm_strerror(init_error));
  }
  alpm_handle_t* handle = guard.handle;

  const std::unordered_map<std::string, InstalledRecord> installed = ReadLocalDatabase(handle);

  // Register each on-disk sync repo so its package cache becomes readable.
  for (const std::string& repo_name : DiscoverSyncRepoNames(db_path_)) {
    alpm_register_syncdb(handle, repo_name.c_str(), ALPM_SIG_USE_DEFAULT);
  }

  std::vector<model::Package> packages;
  std::unordered_set<std::string> seen_names;

  for (alpm_list_t* db_node = alpm_get_syncdbs(handle); db_node != nullptr; db_node = db_node->next) {
    auto* sync_db = static_cast<alpm_db_t*>(db_node->data);
    const std::string repo_name = SafeString(alpm_db_get_name(sync_db));
    for (alpm_list_t* pkg_node = alpm_db_get_pkgcache(sync_db); pkg_node != nullptr;
         pkg_node = pkg_node->next) {
      auto* sync_package = static_cast<alpm_pkg_t*>(pkg_node->data);
      model::Package package = PackageFromSyncEntry(sync_package, repo_name, installed);
      // A package can appear in more than one repo; keep the first (highest
      // priority) listing and skip duplicates.
      if (seen_names.insert(package.name).second) {
        packages.push_back(std::move(package));
      }
    }
  }

  AppendForeignPackages(handle, seen_names, packages);
  return packages;
}

}  // namespace pacseek::data
