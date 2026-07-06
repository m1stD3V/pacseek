#include "data/flatpak_source.hpp"

#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace pacseek::data {

namespace {

// Tab-separated columns we ask flatpak for, in this order.
constexpr char kListCommand[] =
    "flatpak list --app --columns=application,name,version,size,origin 2>/dev/null";

// App ids with a pending update, read from the LOCALLY CACHED remote summaries
// only (--cached): flatpak refreshes that cache whenever the user touches the
// remotes themselves, so this stays inside pacseek's no-egress-beyond-the-AUR
// policy - exactly like pacman update detection trusting the last -Sy. With no
// cache yet the command fails and no updates are reported, same as before.
constexpr char kUpdatesCommand[] =
    "flatpak remote-ls --updates --app --cached --columns=application 2>/dev/null";

// Runs `command`, returning its stdout. Empty on failure (e.g. flatpak absent).
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

// Parses a human size as printed by flatpak ("1.2 MB", "512 kB", "3.4 GiB") into
// bytes. flatpak uses SI units by default; binary units are handled too. Returns
// 0 when unparseable.
int64_t ParseSize(const std::string& text) {
  double value = 0.0;
  char unit[16] = {0};
  if (std::sscanf(text.c_str(), "%lf %15s", &value, unit) < 1) {
    return 0;
  }
  const std::string u = unit;
  double multiplier = 1.0;
  if (u == "kB" || u == "KB") {
    multiplier = 1e3;
  } else if (u == "MB") {
    multiplier = 1e6;
  } else if (u == "GB") {
    multiplier = 1e9;
  } else if (u == "TB") {
    multiplier = 1e12;
  } else if (u == "KiB") {
    multiplier = 1024.0;
  } else if (u == "MiB") {
    multiplier = 1024.0 * 1024.0;
  } else if (u == "GiB") {
    multiplier = 1024.0 * 1024.0 * 1024.0;
  } else if (u == "TiB") {
    multiplier = 1024.0 * 1024.0 * 1024.0 * 1024.0;
  }
  return static_cast<int64_t>(value * multiplier);
}

std::vector<std::string> SplitTabs(const std::string& line) {
  std::vector<std::string> fields;
  std::string field;
  std::istringstream stream(line);
  while (std::getline(stream, field, '\t')) {
    fields.push_back(field);
  }
  return fields;
}

}  // namespace

std::vector<model::Package> FlatpakSource::LoadPackages() {
  std::vector<model::Package> packages;
  const std::string listing = RunCapture(kListCommand);

  // Ids with a cached-pending update; matched against the installed rows below.
  std::unordered_set<std::string> updatable;
  {
    std::istringstream update_stream(RunCapture(kUpdatesCommand));
    std::string id;
    while (std::getline(update_stream, id)) {
      if (!id.empty()) {
        updatable.insert(id);
      }
    }
  }

  std::istringstream stream(listing);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty()) {
      continue;
    }
    const std::vector<std::string> fields = SplitTabs(line);
    // application[,name,version,size,origin]; the app id is required and is what
    // commands operate on, so skip anything without one.
    if (fields.empty() || fields[0].empty()) {
      continue;
    }

    model::Package package;
    package.name = fields[0];  // application id, e.g. org.gimp.GIMP
    const std::string human = fields.size() > 1 ? fields[1] : "";
    const std::string origin = fields.size() > 4 ? fields[4] : "";
    package.version = fields.size() > 2 ? fields[2] : "";
    package.install_size_bytes = fields.size() > 3 ? ParseSize(fields[3]) : 0;
    // The human name (and remote) read better as the second-line description than
    // the reverse-DNS id shown as the title.
    package.description = human.empty() ? package.name : human;
    if (!origin.empty()) {
      package.description += " · " + origin;
    }
    package.repo = model::Repo::Flatpak;
    package.installed = true;
    package.has_update = updatable.count(package.name) != 0;
    packages.push_back(std::move(package));
  }
  return packages;
}

model::PackageDetail FlatpakSource::Describe(const model::Package& package) {
  // flatpak apps are sandboxed bundles, not pacman packages, so there is no
  // dependency or owned-file list to show; surface the identity we already have.
  model::PackageDetail detail;
  detail.available = true;
  detail.name = package.name;
  detail.version = package.version;
  detail.description = package.description;
  detail.repo = package.repo;
  detail.install_size_bytes = package.install_size_bytes;
  detail.note = "flatpak application - managed by the flatpak CLI";
  detail.files_note = "flatpak apps are sandboxed; an owned-file list is not tracked here";
  return detail;
}

}  // namespace pacseek::data
