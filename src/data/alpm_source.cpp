#include "data/alpm_source.hpp"

#include <alpm.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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

// Formats an alpm timestamp (seconds since epoch) as local "YYYY-MM-DD HH:MM",
// or "" when it is zero / cannot be rendered.
std::string FormatTimestamp(alpm_time_t when) {
  if (when == 0) {
    return {};
  }
  const std::time_t seconds = static_cast<std::time_t>(when);
  std::tm calendar;
  if (localtime_r(&seconds, &calendar) == nullptr) {
    return {};
  }
  char buffer[32];
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &calendar) == 0) {
    return {};
  }
  return std::string(buffer);
}

// Collects an alpm_list of C strings into a vector.
std::vector<std::string> StringList(alpm_list_t* list) {
  std::vector<std::string> out;
  for (alpm_list_t* node = list; node != nullptr; node = node->next) {
    out.push_back(SafeString(static_cast<const char*>(node->data)));
  }
  return out;
}

// Renders a dependency list (alpm_depend_t) as "name<op>version" strings; for
// optdepends, alpm appends ": <reason>" automatically.
std::vector<std::string> DependList(alpm_list_t* list) {
  std::vector<std::string> out;
  for (alpm_list_t* node = list; node != nullptr; node = node->next) {
    char* rendered = alpm_dep_compute_string(static_cast<alpm_depend_t*>(node->data));
    out.push_back(SafeString(rendered));
    std::free(rendered);
  }
  return out;
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

// True when a local package is an orphan in pacman's `-Qdt` sense: installed as
// a dependency, yet no installed package still requires it - neither as a hard
// dependency (compute_requiredby) nor an optional one (compute_optionalfor).
// Both computed lists are freshly allocated and must be FREELIST'd.
bool IsOrphanPackage(alpm_pkg_t* local_package) {
  if (alpm_pkg_get_reason(local_package) != ALPM_PKG_REASON_DEPEND) {
    return false;
  }
  alpm_list_t* required_by = alpm_pkg_compute_requiredby(local_package);
  const bool has_requirer = required_by != nullptr;
  FREELIST(required_by);
  if (has_requirer) {
    return false;
  }
  alpm_list_t* optional_for = alpm_pkg_compute_optionalfor(local_package);
  const bool has_optional = optional_for != nullptr;
  FREELIST(optional_for);
  return !has_optional;
}

// The installed picture for one package, gathered from the local database.
struct InstalledRecord {
  std::string version;
  int64_t install_size_bytes;
  bool explicit_install;  // reason EXPLICIT (vs. pulled in as a dependency)
  bool is_orphan;         // the `pacman -Qdt` predicate (see IsOrphanPackage)
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
                                      static_cast<int64_t>(alpm_pkg_get_isize(package)),
                                      alpm_pkg_get_reason(package) == ALPM_PKG_REASON_EXPLICIT,
                                      IsOrphanPackage(package)});
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
    const InstalledRecord& record = installed_it->second;
    package.has_update = alpm_pkg_vercmp(package.version.c_str(), record.version.c_str()) > 0;
    package.explicit_install = record.explicit_install;
    package.is_orphan = record.is_orphan;
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
    // This handle is the local package itself, so compute the reason/orphan
    // state directly rather than looking it up.
    package.explicit_install =
        alpm_pkg_get_reason(local_package) == ALPM_PKG_REASON_EXPLICIT;
    package.is_orphan = IsOrphanPackage(local_package);
    packages.push_back(std::move(package));
  }
}

// Traces why an installed dependency exists: the shortest chain of packages from
// an explicitly-installed root down to `start`, found by a breadth-first walk up
// the reverse-dependency graph (required-by edges). Returns the chain ordered
// root -> ... -> start (both inclusive), or an empty vector when no explicit root
// is reachable (an all-dependency chain, a required-by not in the local db, or a
// pathological graph that hits the depth cap). Robust to null handles.
std::vector<std::string> TraceInstallChain(alpm_db_t* local_db, alpm_pkg_t* start) {
  // Depth cap so a cyclic or pathological graph can never loop forever; combined
  // with the visited set this bounds the walk.
  constexpr int kMaxChainDepth = 32;
  if (local_db == nullptr || start == nullptr) {
    return {};
  }
  const std::string start_name = SafeString(alpm_pkg_get_name(start));
  if (start_name.empty()) {
    return {};
  }

  // parent[child] = the package `child` is required-by-reached *through*, i.e.
  // the node one step closer to `start`. Following parents from a root back to
  // `start` reconstructs the path.
  std::unordered_map<std::string, std::string> parent;
  std::unordered_set<std::string> visited{start_name};
  std::deque<std::pair<std::string, int>> frontier{{start_name, 0}};

  while (!frontier.empty()) {
    const std::string name = frontier.front().first;
    const int depth = frontier.front().second;
    frontier.pop_front();
    if (depth >= kMaxChainDepth) {
      continue;
    }
    alpm_pkg_t* node = alpm_db_get_pkg(local_db, name.c_str());
    if (node == nullptr) {
      continue;
    }
    // compute_requiredby allocates a fresh list of strdup'd names; FREELIST
    // releases both the nodes and the strings.
    alpm_list_t* required_by = alpm_pkg_compute_requiredby(node);
    std::string found_root;
    for (alpm_list_t* it = required_by; it != nullptr; it = it->next) {
      const std::string req_name = SafeString(static_cast<const char*>(it->data));
      if (req_name.empty() || visited.count(req_name) != 0) {
        continue;
      }
      visited.insert(req_name);
      parent.emplace(req_name, name);
      alpm_pkg_t* req_pkg = alpm_db_get_pkg(local_db, req_name.c_str());
      if (req_pkg != nullptr && alpm_pkg_get_reason(req_pkg) == ALPM_PKG_REASON_EXPLICIT) {
        found_root = req_name;
        break;  // BFS: the first explicit root reached sits on a shortest path.
      }
      frontier.emplace_back(req_name, depth + 1);
    }
    FREELIST(required_by);

    if (!found_root.empty()) {
      std::vector<std::string> chain;  // root -> ... -> start
      for (std::string cursor = found_root;;) {
        chain.push_back(cursor);
        if (cursor == start_name) {
          break;
        }
        auto step = parent.find(cursor);
        if (step == parent.end()) {
          return {};  // broken linkage; treat as untraceable
        }
        cursor = step->second;
      }
      return chain;
    }
  }
  return {};
}

// The set of installed package names, scanned straight from the local database.
// Lighter than ReadLocalDatabase when only membership (not sizes/reasons) is
// wanted, e.g. deciding which sync satisfiers are already present.
std::unordered_set<std::string> LocalPackageNames(alpm_handle_t* handle) {
  std::unordered_set<std::string> names;
  alpm_db_t* local_db = alpm_get_localdb(handle);
  if (local_db == nullptr) {
    return names;
  }
  for (alpm_list_t* node = alpm_db_get_pkgcache(local_db); node != nullptr; node = node->next) {
    names.insert(SafeString(alpm_pkg_get_name(static_cast<alpm_pkg_t*>(node->data))));
  }
  return names;
}

// The estimated disk cost of adding a not-installed package: the target's size
// plus that of every dependency in its sync closure not already present.
struct MarginalCost {
  int new_dep_count = 0;    // dependencies pulled in that aren't installed yet
  int64_t install_bytes = 0;  // isize of the target + those new dependencies
  int64_t download_bytes = 0; // download size to fetch the target + new deps
};

// Walks the target's dependency graph over the SYNC databases, resolving each
// depend string to a satisfier and collecting those not already installed. Only
// not-installed satisfiers extend the frontier: an installed package's own
// dependencies are, by definition, already present, so the closure stays bounded
// to what a fresh install would actually download. Visited-by-name plus a size
// cap guarantee termination. Null-safe; frees every string it computes.
MarginalCost ComputeMarginalCost(alpm_handle_t* handle, alpm_pkg_t* target,
                                 const std::unordered_set<std::string>& installed) {
  // Hard ceiling on the closure so a pathological graph always terminates.
  constexpr size_t kMaxClosure = 4000;
  MarginalCost cost;
  if (handle == nullptr || target == nullptr) {
    return cost;
  }
  cost.install_bytes = static_cast<int64_t>(alpm_pkg_get_isize(target));
  cost.download_bytes = static_cast<int64_t>(alpm_pkg_get_size(target));

  alpm_list_t* syncdbs = alpm_get_syncdbs(handle);
  std::unordered_set<std::string> visited{SafeString(alpm_pkg_get_name(target))};
  std::deque<alpm_pkg_t*> frontier{target};

  while (!frontier.empty() && visited.size() < kMaxClosure) {
    alpm_pkg_t* node = frontier.front();
    frontier.pop_front();
    for (alpm_list_t* dep = alpm_pkg_get_depends(node); dep != nullptr; dep = dep->next) {
      char* depstring = alpm_dep_compute_string(static_cast<alpm_depend_t*>(dep->data));
      if (depstring == nullptr) {
        continue;
      }
      // Points into a sync db cache - not owned, so nothing to free here.
      alpm_pkg_t* satisfier = alpm_find_dbs_satisfier(handle, syncdbs, depstring);
      std::free(depstring);
      if (satisfier == nullptr) {
        continue;
      }
      const std::string name = SafeString(alpm_pkg_get_name(satisfier));
      if (name.empty() || visited.count(name) != 0) {
        continue;
      }
      visited.insert(name);
      if (installed.count(name) != 0) {
        continue;  // already present: no marginal cost, and its deps are met too
      }
      cost.new_dep_count += 1;
      cost.install_bytes += static_cast<int64_t>(alpm_pkg_get_isize(satisfier));
      cost.download_bytes += static_cast<int64_t>(alpm_pkg_get_size(satisfier));
      frontier.push_back(satisfier);
    }
  }
  return cost;
}

// Trims leading/trailing ASCII whitespace, the only cleanup a typed path or
// command name needs before it is matched against the stored filelists.
std::string TrimWhitespace(const std::string& text) {
  const auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
  auto begin = std::find_if(text.begin(), text.end(), not_space);
  auto end = std::find_if(text.rbegin(), text.rend(), not_space).base();
  return begin < end ? std::string(begin, end) : std::string();
}

// The relative paths (alpm stores names WITHOUT a leading slash) a query might
// resolve to. A path is matched verbatim (leading slash stripped); a bare name
// is resolved through the usual command directories, mirroring a $PATH lookup.
std::vector<std::string> FileOwnerCandidates(const std::string& trimmed) {
  if (trimmed.find('/') != std::string::npos) {
    size_t start = 0;
    while (start < trimmed.size() && trimmed[start] == '/') {
      ++start;
    }
    return {trimmed.substr(start)};
  }
  return {"usr/bin/" + trimmed, "bin/" + trimmed, "usr/sbin/" + trimmed,
          "sbin/" + trimmed, "usr/local/bin/" + trimmed};
}

// True when at least one `<repo>.files` database is already present under
// db_path/sync. Gate for the -F scan: without one, the files database has never
// been `-Fy` synced, so we refuse to fetch it and just hint the user instead.
bool SyncFilesDbPresent(const std::string& db_path) {
  const fs::path sync_dir = fs::path(db_path) / kSyncSubdir;
  std::error_code error;
  for (const fs::directory_entry& entry : fs::directory_iterator(sync_dir, error)) {
    if (entry.path().extension() == ".files") {
      return true;
    }
  }
  return false;
}

// True when any file in `package`'s filelist matches one of the candidate
// relative paths. The filelist is owned by libalpm - read only, never freed.
bool PackageOwnsCandidate(alpm_pkg_t* package,
                          const std::vector<std::string>& candidates) {
  const alpm_filelist_t* filelist = alpm_pkg_get_files(package);
  if (filelist == nullptr) {
    return false;
  }
  for (size_t i = 0; i < filelist->count; ++i) {
    const char* name = filelist->files[i].name;
    if (name == nullptr) {
      continue;
    }
    for (const std::string& candidate : candidates) {
      if (candidate == name) {
        return true;
      }
    }
  }
  return false;
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

model::PackageDetail AlpmSource::Describe(const model::Package& package) {
  // Echo the row's identity first so the pane is never blank, even if the lookup
  // below fails (an un-built AUR result has no database entry).
  model::PackageDetail detail;
  detail.name = package.name;
  detail.version = package.version;
  detail.description = package.description;
  detail.repo = package.repo;
  detail.install_size_bytes = package.install_size_bytes;

  AlpmHandle guard;
  alpm_errno_t init_error = ALPM_ERR_OK;
  guard.handle = alpm_initialize(root_.c_str(), db_path_.c_str(), &init_error);
  if (guard.handle == nullptr) {
    detail.note = std::string("libalpm unavailable: ") + alpm_strerror(init_error);
    return detail;
  }
  alpm_handle_t* handle = guard.handle;

  for (const std::string& repo_name : DiscoverSyncRepoNames(db_path_)) {
    alpm_register_syncdb(handle, repo_name.c_str(), ALPM_SIG_USE_DEFAULT);
  }

  // Prefer the installed copy - only it carries files, install date, and reason.
  // Fall back to the sync databases for available-but-not-installed packages.
  alpm_pkg_t* found = nullptr;
  bool from_local = false;
  if (alpm_db_t* local_db = alpm_get_localdb(handle)) {
    found = alpm_db_get_pkg(local_db, package.name.c_str());
    from_local = found != nullptr;
  }
  for (alpm_list_t* db_node = alpm_get_syncdbs(handle);
       found == nullptr && db_node != nullptr; db_node = db_node->next) {
    found = alpm_db_get_pkg(static_cast<alpm_db_t*>(db_node->data), package.name.c_str());
  }
  if (found == nullptr) {
    detail.note = "no database entry - details appear once the package is built/installed";
    detail.files_note = "file list is available only after installation";
    return detail;
  }

  detail.available = true;
  detail.url = SafeString(alpm_pkg_get_url(found));
  detail.packager = SafeString(alpm_pkg_get_packager(found));
  detail.build_date = FormatTimestamp(alpm_pkg_get_builddate(found));
  detail.licenses = StringList(alpm_pkg_get_licenses(found));
  detail.install_size_bytes = static_cast<int64_t>(alpm_pkg_get_isize(found));
  detail.depends = DependList(alpm_pkg_get_depends(found));
  detail.optdepends = DependList(alpm_pkg_get_optdepends(found));
  detail.provides = DependList(alpm_pkg_get_provides(found));
  detail.conflicts = DependList(alpm_pkg_get_conflicts(found));
  detail.replaces = DependList(alpm_pkg_get_replaces(found));

  if (from_local) {
    detail.install_date = FormatTimestamp(alpm_pkg_get_installdate(found));
    const bool explicitly_installed =
        alpm_pkg_get_reason(found) == ALPM_PKG_REASON_EXPLICIT;
    detail.install_reason = explicitly_installed ? "explicit" : "dependency";

    // Answer "why is this installed?" purely from the local graph.
    if (explicitly_installed) {
      detail.why_installed = "explicitly installed";
    } else {
      const std::vector<std::string> chain =
          TraceInstallChain(alpm_get_localdb(handle), found);
      // chain is root -> ... -> this; drop the trailing self so the value reads
      // as the ancestry that pulled the package in (e.g. "gnome → gvfs").
      if (chain.size() >= 2) {
        std::string ancestry;
        for (size_t i = 0; i + 1 < chain.size(); ++i) {
          if (i != 0) {
            ancestry += " → ";
          }
          ancestry += chain[i];
        }
        detail.why_installed = ancestry;
      } else {
        detail.why_installed = "pulled in as a dependency";
      }
    }

    // compute_requiredby allocates a fresh list of strdup'd names; FREELIST
    // releases both the nodes and the strings.
    alpm_list_t* required_by = alpm_pkg_compute_requiredby(found);
    detail.required_by = StringList(required_by);
    FREELIST(required_by);

    if (const alpm_filelist_t* filelist = alpm_pkg_get_files(found)) {
      detail.files.reserve(filelist->count);
      for (size_t i = 0; i < filelist->count; ++i) {
        // libalpm stores paths relative to the root; present them absolute.
        detail.files.push_back("/" + SafeString(filelist->files[i].name));
      }
    }
  } else {
    detail.files_note = "file list is available only after installation";
    // Not installed but available from a sync repo: estimate what adding it
    // actually costs on disk over its sync dependency closure. Rendered as the
    // INSTALL COST section, labelled an estimate.
    const std::unordered_set<std::string> installed = LocalPackageNames(handle);
    const MarginalCost cost = ComputeMarginalCost(handle, found, installed);
    detail.marginal_computed = true;
    detail.new_dep_count = cost.new_dep_count;
    detail.marginal_install_bytes = cost.install_bytes;
    detail.marginal_download_bytes = cost.download_bytes;
  }
  return detail;
}

RemovalPreview AlpmSource::PreviewRemoval(const model::Package& package) {
  // -Rs closure over the LOCAL database only; no sync dbs or network needed.
  RemovalPreview preview;
  AlpmHandle guard;
  alpm_errno_t init_error = ALPM_ERR_OK;
  guard.handle = alpm_initialize(root_.c_str(), db_path_.c_str(), &init_error);
  if (guard.handle == nullptr) {
    return preview;  // unavailable: the UI simply skips the cascade prompt
  }
  alpm_db_t* local_db = alpm_get_localdb(guard.handle);
  if (local_db == nullptr) {
    return preview;
  }
  alpm_pkg_t* target = alpm_db_get_pkg(local_db, package.name.c_str());
  if (target == nullptr) {
    return preview;  // not installed: nothing to remove
  }
  const std::string target_name = SafeString(alpm_pkg_get_name(target));
  if (target_name.empty()) {
    return preview;
  }

  // The removal set, grown to a fixed point: seed with the target, then keep
  // absorbing installed dependencies that nothing OUTSIDE the set still requires
  // (and that were pulled in as dependencies, matching -Rs, which never removes
  // an explicitly-installed package). A dependency rejected on one pass can
  // qualify on a later one once its external requirer joins the set, so we
  // re-scan until a full pass adds nothing. The size cap guarantees termination.
  constexpr size_t kMaxRemovalSet = 4000;
  std::unordered_set<std::string> set{target_name};
  alpm_list_t* pkgcache = alpm_db_get_pkgcache(local_db);

  bool changed = true;
  while (changed && set.size() < kMaxRemovalSet) {
    changed = false;
    // Snapshot the members so growing the set mid-pass is safe.
    const std::vector<std::string> members(set.begin(), set.end());
    for (const std::string& member_name : members) {
      alpm_pkg_t* member = alpm_db_get_pkg(local_db, member_name.c_str());
      if (member == nullptr) {
        continue;
      }
      for (alpm_list_t* dep = alpm_pkg_get_depends(member); dep != nullptr; dep = dep->next) {
        char* depstring = alpm_dep_compute_string(static_cast<alpm_depend_t*>(dep->data));
        if (depstring == nullptr) {
          continue;
        }
        // Points into the local cache - not owned, nothing to free.
        alpm_pkg_t* satisfier = alpm_find_satisfier(pkgcache, depstring);
        std::free(depstring);
        if (satisfier == nullptr) {
          continue;
        }
        // -Rs leaves explicitly-installed packages in place, even when unneeded.
        if (alpm_pkg_get_reason(satisfier) == ALPM_PKG_REASON_EXPLICIT) {
          continue;
        }
        const std::string dep_name = SafeString(alpm_pkg_get_name(satisfier));
        if (dep_name.empty() || set.count(dep_name) != 0) {
          continue;
        }
        // Every package still requiring this dep must already be in the set,
        // i.e. nothing outside the removal keeps it alive.
        alpm_list_t* required_by = alpm_pkg_compute_requiredby(satisfier);
        bool all_within = true;
        for (alpm_list_t* req = required_by; req != nullptr; req = req->next) {
          if (set.count(SafeString(static_cast<const char*>(req->data))) == 0) {
            all_within = false;
            break;
          }
        }
        FREELIST(required_by);
        if (all_within) {
          set.insert(dep_name);
          changed = true;
          if (set.size() >= kMaxRemovalSet) {
            break;
          }
        }
      }
      if (set.size() >= kMaxRemovalSet) {
        break;
      }
    }
  }

  // Sum reclaimed install size over the whole set, and present the names sorted
  // with the target pinned first.
  int64_t reclaimed = 0;
  std::vector<std::string> names;
  names.reserve(set.size());
  for (const std::string& name : set) {
    if (alpm_pkg_t* member = alpm_db_get_pkg(local_db, name.c_str())) {
      reclaimed += static_cast<int64_t>(alpm_pkg_get_isize(member));
    }
    names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  names.erase(std::remove(names.begin(), names.end(), target_name), names.end());
  names.insert(names.begin(), target_name);

  preview.available = true;
  preview.packages = std::move(names);
  preview.reclaimed_bytes = reclaimed;
  return preview;
}

std::vector<model::Collection> AlpmSource::Groups() {
  AlpmHandle guard;
  alpm_errno_t init_error = ALPM_ERR_OK;
  guard.handle = alpm_initialize(root_.c_str(), db_path_.c_str(), &init_error);
  if (guard.handle == nullptr) {
    return {};  // no handle: no groups, matching the other best-effort loads
  }
  alpm_handle_t* handle = guard.handle;

  for (const std::string& repo_name : DiscoverSyncRepoNames(db_path_)) {
    alpm_register_syncdb(handle, repo_name.c_str(), ALPM_SIG_USE_DEFAULT);
  }

  // Merge groups across every sync db: one entry per distinct group name, its
  // members deduped but kept in first-seen order. `order` preserves discovery
  // order for the members; the collections themselves are sorted by name below.
  // The groupcache and its packages are owned by libalpm - read only, never
  // freed here.
  std::vector<std::string> order;
  std::unordered_map<std::string, std::vector<std::string>> members;
  std::unordered_map<std::string, std::unordered_set<std::string>> seen_members;

  for (alpm_list_t* db_node = alpm_get_syncdbs(handle); db_node != nullptr; db_node = db_node->next) {
    auto* sync_db = static_cast<alpm_db_t*>(db_node->data);
    for (alpm_list_t* grp_node = alpm_db_get_groupcache(sync_db); grp_node != nullptr;
         grp_node = grp_node->next) {
      auto* group = static_cast<alpm_group_t*>(grp_node->data);
      if (group == nullptr) {
        continue;
      }
      const std::string group_name = SafeString(group->name);
      if (group_name.empty()) {
        continue;
      }
      if (members.find(group_name) == members.end()) {
        order.push_back(group_name);
      }
      std::vector<std::string>& list = members[group_name];
      std::unordered_set<std::string>& seen = seen_members[group_name];
      for (alpm_list_t* pkg_node = group->packages; pkg_node != nullptr; pkg_node = pkg_node->next) {
        const std::string pkg_name =
            SafeString(alpm_pkg_get_name(static_cast<alpm_pkg_t*>(pkg_node->data)));
        if (!pkg_name.empty() && seen.insert(pkg_name).second) {
          list.push_back(pkg_name);
        }
      }
    }
  }

  std::vector<model::Collection> collections;
  collections.reserve(order.size());
  for (const std::string& name : order) {
    std::vector<std::string>& list = members[name];
    model::Collection collection;
    collection.id = "group:" + name;
    collection.name = name;
    collection.icon = "▦";
    collection.description = "pacman group · " + std::to_string(list.size()) + " packages";
    collection.packages = std::move(list);
    collection.origin = model::CollectionOrigin::PacmanGroup;
    collection.manager = model::CollectionManager::Pacman;
    collections.push_back(std::move(collection));
  }
  std::sort(collections.begin(), collections.end(),
            [](const model::Collection& a, const model::Collection& b) { return a.name < b.name; });
  return collections;
}

int64_t AlpmSource::LastSyncSeconds() {
  // `pacman -Sy` rewrites the <repo>.db files, so their newest mtime is the
  // moment the sync data was last refreshed. stat() keeps this a plain POSIX
  // read with no time_point conversion gymnastics.
  const fs::path sync_dir = fs::path(db_path_) / kSyncSubdir;
  int64_t newest = 0;
  std::error_code error;
  for (const fs::directory_entry& entry : fs::directory_iterator(sync_dir, error)) {
    if (entry.path().extension() != kSyncDbExtension) {
      continue;
    }
    struct stat info;
    if (::stat(entry.path().c_str(), &info) == 0) {
      newest = std::max(newest, static_cast<int64_t>(info.st_mtime));
    }
  }
  return newest;
}

FileOwnerResult AlpmSource::FindFileOwner(const std::string& query) {
  FileOwnerResult result;
  const std::string trimmed = TrimWhitespace(query);
  if (trimmed.empty()) {
    result.available = true;
    result.note = "type a file path or command name";
    return result;
  }
  // The display form keeps the leading slash for a path, so the echoed query
  // reads like an absolute path even though the candidates drop it.
  const bool is_path = trimmed.find('/') != std::string::npos;
  result.query = is_path && trimmed.front() != '/' ? "/" + trimmed : trimmed;
  const std::vector<std::string> candidates = FileOwnerCandidates(trimmed);

  AlpmHandle guard;
  alpm_errno_t init_error = ALPM_ERR_OK;
  guard.handle = alpm_initialize(root_.c_str(), db_path_.c_str(), &init_error);
  if (guard.handle == nullptr) {
    result.note = std::string("libalpm unavailable: ") + alpm_strerror(init_error);
    return result;  // available stays false: the source couldn't answer
  }
  alpm_handle_t* handle = guard.handle;
  result.available = true;

  // Local ownership (`pacman -Qo`): scan every installed package's filelist for
  // a candidate. This is a full local-files walk, which is exactly what -Qo
  // does; fine as an on-demand, one-off lookup.
  if (alpm_db_t* local_db = alpm_get_localdb(handle)) {
    for (alpm_list_t* node = alpm_db_get_pkgcache(local_db); node != nullptr; node = node->next) {
      auto* package = static_cast<alpm_pkg_t*>(node->data);
      if (PackageOwnsCandidate(package, candidates)) {
        result.owners.push_back(SafeString(alpm_pkg_get_name(package)));
      }
    }
  }
  if (!result.owners.empty()) {
    std::sort(result.owners.begin(), result.owners.end());
    return result;  // from_files_db stays false: these are installed owners
  }

  // No installed owner. Fall back to the files database (`pacman -F`), but only
  // if it is already on disk - we never trigger a `-Fy` sync ourselves.
  if (!SyncFilesDbPresent(db_path_)) {
    result.note = "files database not synced · run `pacman -Fy` to enable provider lookup";
    return result;
  }

  // A SECOND handle whose sync dbs load `<repo>.files` instead of `<repo>.db`:
  // set_dbext must be called BEFORE registering the repos so the files variant
  // is the one opened.
  AlpmHandle files_guard;
  files_guard.handle = alpm_initialize(root_.c_str(), db_path_.c_str(), &init_error);
  if (files_guard.handle == nullptr) {
    result.note = std::string("files database unreadable: ") + alpm_strerror(init_error);
    return result;
  }
  alpm_handle_t* files_handle = files_guard.handle;
  alpm_option_set_dbext(files_handle, ".files");
  for (const std::string& repo_name : DiscoverSyncRepoNames(db_path_)) {
    alpm_register_syncdb(files_handle, repo_name.c_str(), ALPM_SIG_USE_DEFAULT);
  }

  for (alpm_list_t* db_node = alpm_get_syncdbs(files_handle); db_node != nullptr;
       db_node = db_node->next) {
    auto* sync_db = static_cast<alpm_db_t*>(db_node->data);
    for (alpm_list_t* pkg_node = alpm_db_get_pkgcache(sync_db); pkg_node != nullptr;
         pkg_node = pkg_node->next) {
      auto* package = static_cast<alpm_pkg_t*>(pkg_node->data);
      if (PackageOwnsCandidate(package, candidates)) {
        result.owners.push_back(SafeString(alpm_pkg_get_name(package)));
      }
    }
  }
  if (!result.owners.empty()) {
    // A package can appear in more than one repo's files db; dedupe by name.
    std::sort(result.owners.begin(), result.owners.end());
    result.owners.erase(std::unique(result.owners.begin(), result.owners.end()),
                        result.owners.end());
    result.from_files_db = true;
    result.note = "provided by an available (not installed) package";
  }
  return result;
}

}  // namespace pacseek::data
