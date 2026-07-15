#include "data/node_source.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace pacseek::data {

namespace {

namespace fs = std::filesystem;

// Runs `command`, returning its trimmed first line. Empty on failure (e.g. the
// CLI is absent). Used only for the managers' own `root -g` path queries, which
// read local config and never touch the network.
std::string RunCaptureLine(const std::string& command) {
  FILE* pipe = ::popen(command.c_str(), "r");
  if (pipe == nullptr) {
    return {};
  }
  std::string output;
  char buffer[4096];
  while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output += buffer;
  }
  ::pclose(pipe);
  const auto first = output.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = output.find_last_not_of(" \t\r\n");
  return output.substr(first, last - first + 1);
}

// The global node_modules directory for a manager, or empty when it can't be
// resolved. npm/pnpm answer directly; bun keeps its global install tree under
// $BUN_INSTALL (default ~/.bun), which has no query command, so we derive it.
std::string GlobalDir(model::Repo manager) {
  switch (manager) {
    case model::Repo::Npm:
      return RunCaptureLine("npm root -g 2>/dev/null");
    case model::Repo::Pnpm:
      return RunCaptureLine("pnpm root -g 2>/dev/null");
    case model::Repo::Bun: {
      const char* bun_install = std::getenv("BUN_INSTALL");
      std::string base = (bun_install != nullptr && bun_install[0] != '\0') ? bun_install : "";
      if (base.empty()) {
        const char* home = std::getenv("HOME");
        if (home == nullptr || home[0] == '\0') {
          return {};
        }
        base = std::string(home) + "/.bun";
      }
      return base + "/install/global/node_modules";
    }
    default:
      return {};
  }
}

// Total size in bytes of every regular file under `dir`, resolving `dir` itself
// if it is a symlink (pnpm links each global package to its store entry) but not
// following symlinks encountered inside - so shared store files are counted once
// against the package that links them, never chased out into the wider store.
int64_t DirSizeBytes(const fs::path& dir) {
  std::error_code error;
  const fs::path root = fs::canonical(dir, error);
  if (error) {
    return 0;
  }
  int64_t total = 0;
  fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error);
  if (error) {
    return 0;
  }
  const fs::recursive_directory_iterator end;
  for (; it != end; it.increment(error)) {
    if (error) {
      break;
    }
    if (it->is_symlink(error) || error) {
      continue;  // never chase links out of the package's own tree
    }
    if (it->is_regular_file(error) && !error) {
      const auto size = it->file_size(error);
      if (!error) {
        total += static_cast<int64_t>(size);
      }
    }
  }
  return total;
}

// Reads the "version" string from `package_dir/package.json`, or empty when the
// file is missing or has no version. A deliberately tiny scan - enough for a
// display version, not a JSON parser.
std::string ReadVersion(const fs::path& package_dir) {
  std::ifstream file(package_dir / "package.json");
  if (!file) {
    return {};
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  const std::string text = buffer.str();
  const auto key = text.find("\"version\"");
  if (key == std::string::npos) {
    return {};
  }
  const auto colon = text.find(':', key);
  if (colon == std::string::npos) {
    return {};
  }
  const auto open = text.find('"', colon);
  if (open == std::string::npos) {
    return {};
  }
  const auto close = text.find('"', open + 1);
  if (close == std::string::npos) {
    return {};
  }
  return text.substr(open + 1, close - open - 1);
}

const char* Description(model::Repo manager) {
  switch (manager) {
    case model::Repo::Npm:
      return "npm global package";
    case model::Repo::Bun:
      return "bun global package";
    case model::Repo::Pnpm:
      return "pnpm global package";
    default:
      return "global package";
  }
}

// Turns one package directory into a Package row. `display_name` carries the
// scope (e.g. "@angular/cli") while `dir` points at the on-disk package.
model::Package MakePackage(const std::string& display_name, const fs::path& dir,
                           model::Repo manager) {
  model::Package package;
  package.name = display_name;
  package.version = ReadVersion(dir);
  package.description = Description(manager);
  package.repo = manager;
  package.installed = true;
  package.install_size_bytes = DirSizeBytes(dir);
  return package;
}

// Appends every top-level package in `manager`'s global node_modules. Scope
// directories (@foo) hold their packages one level down, so those are descended
// into; dot-entries (.bin, .pnpm, .package-lock.json, .modules.yaml) are skipped.
void AppendManager(model::Repo manager, std::vector<model::Package>& packages) {
  const std::string dir = GlobalDir(manager);
  if (dir.empty()) {
    return;
  }
  std::error_code error;
  const fs::path root(dir);
  if (!fs::is_directory(root, error) || error) {
    return;
  }
  for (const fs::directory_entry& entry : fs::directory_iterator(
           root, fs::directory_options::skip_permission_denied, error)) {
    const std::string name = entry.path().filename().string();
    if (name.empty() || name[0] == '.') {
      continue;
    }
    if (name[0] == '@') {
      // A scope directory: each child is a package published under that scope.
      std::error_code scope_error;
      for (const fs::directory_entry& scoped : fs::directory_iterator(
               entry.path(), fs::directory_options::skip_permission_denied, scope_error)) {
        const std::string sub = scoped.path().filename().string();
        if (sub.empty() || sub[0] == '.') {
          continue;
        }
        packages.push_back(MakePackage(name + "/" + sub, scoped.path(), manager));
      }
      continue;
    }
    packages.push_back(MakePackage(name, entry.path(), manager));
  }
}

}  // namespace

std::vector<model::Package> NodeSource::LoadPackages() {
  std::vector<model::Package> packages;
  if (npm_) {
    AppendManager(model::Repo::Npm, packages);
  }
  if (bun_) {
    AppendManager(model::Repo::Bun, packages);
  }
  if (pnpm_) {
    AppendManager(model::Repo::Pnpm, packages);
  }
  return packages;
}

model::PackageDetail NodeSource::Describe(const model::Package& package) {
  // Node packages aren't in libalpm, so there's no dependency or owned-file graph
  // to walk; surface the identity and size we already resolved from disk.
  model::PackageDetail detail;
  detail.available = true;
  detail.name = package.name;
  detail.version = package.version;
  detail.description = package.description;
  detail.repo = package.repo;
  detail.install_size_bytes = package.install_size_bytes;
  const char* cli = package.repo == model::Repo::Npm    ? "npm"
                    : package.repo == model::Repo::Bun  ? "bun"
                    : package.repo == model::Repo::Pnpm ? "pnpm"
                                                        : "the package manager";
  // Listing globals: npm/pnpm use `ls -g`, bun uses `pm ls -g`.
  const char* list_command = package.repo == model::Repo::Bun ? "bun pm ls -g"
                             : package.repo == model::Repo::Npm ? "npm ls -g"
                                                                : "pnpm ls -g";
  detail.note = std::string(package.description) + " - managed by the " + cli + " CLI";
  detail.files_note = std::string("run `") + list_command + "` to list global packages";
  return detail;
}

}  // namespace pacseek::data
