#include "data/homebrew_source.hpp"

#include <cstdio>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace pacseek::data {

namespace {

// Installed formulae with their versions, one per line ("pkg 1.2 1.3"). Casks
// are listed separately by `--cask`; we fold both in so a GUI app installed via
// brew shows up next to CLI formulae.
constexpr char kFormulaCommand[] = "brew list --formula --versions 2>/dev/null";
constexpr char kCaskCommand[] = "brew list --cask --versions 2>/dev/null";

// Names with a newer version available, read from brew's locally cached state
// (`brew outdated` reads the last `brew update`, never fetching itself here), so
// this stays inside pacseek's no-implicit-egress policy like flatpak --cached.
constexpr char kUpdatesCommand[] = "brew outdated --quiet 2>/dev/null";

// Runs `command`, returning its stdout. Empty on failure (e.g. brew absent).
std::string RunCapture(const std::string& command) {
  std::string output;
  FILE* pipe = ::popen(command.c_str(), "r");
  if (pipe == nullptr) {
    return output;
  }
  char buffer[4096];
  while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output += buffer;
  }
  ::pclose(pipe);
  return output;
}

// Parses "name ver1 ver2 …" into a package, taking the first token as the name
// and the newest listed version (the last token) as the display version.
void AppendListing(const std::string& listing, bool cask,
                   const std::unordered_set<std::string>& updatable,
                   std::vector<model::Package>& packages) {
  std::istringstream stream(listing);
  std::string line;
  while (std::getline(stream, line)) {
    std::istringstream fields(line);
    std::string name;
    if (!(fields >> name) || name.empty()) {
      continue;
    }
    std::string version;
    std::string token;
    while (fields >> token) {
      version = token;  // keep the last version token listed
    }

    model::Package package;
    package.name = name;
    package.version = version;
    package.description = cask ? "Homebrew cask" : "Homebrew formula";
    package.repo = model::Repo::Homebrew;
    package.installed = true;
    package.has_update = updatable.count(name) != 0;
    packages.push_back(std::move(package));
  }
}

}  // namespace

std::vector<model::Package> HomebrewSource::LoadPackages() {
  std::vector<model::Package> packages;

  std::unordered_set<std::string> updatable;
  {
    std::istringstream update_stream(RunCapture(kUpdatesCommand));
    std::string name;
    while (std::getline(update_stream, name)) {
      if (!name.empty()) {
        updatable.insert(name);
      }
    }
  }

  AppendListing(RunCapture(kFormulaCommand), /*cask=*/false, updatable, packages);
  AppendListing(RunCapture(kCaskCommand), /*cask=*/true, updatable, packages);
  return packages;
}

model::PackageDetail HomebrewSource::Describe(const model::Package& package) {
  // brew formulae are not pacman packages, so there is no libalpm dependency or
  // owned-file graph to walk; surface the identity we already have.
  model::PackageDetail detail;
  detail.available = true;
  detail.name = package.name;
  detail.version = package.version;
  detail.description = package.description;
  detail.repo = package.repo;
  detail.install_size_bytes = package.install_size_bytes;
  detail.note = "Homebrew package - managed by the brew CLI";
  detail.files_note = "run `brew list " + package.name + "` to see its files";
  return detail;
}

}  // namespace pacseek::data
