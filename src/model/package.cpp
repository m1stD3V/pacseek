#include "model/package.hpp"

#include <array>
#include <cstdio>

#include "theme.hpp"

namespace pacseek::model {

namespace {
struct RepoName {
  Repo repo;
  const char* name;
};

// Single source of truth for repo<->name mapping, kept tiny and explicit.
constexpr std::array<RepoName, 6> kRepoNames = {{
    {Repo::Core, "core"},
    {Repo::Extra, "extra"},
    {Repo::Multilib, "multilib"},
    {Repo::Aur, "aur"},
    {Repo::Flatpak, "flatpak"},
    {Repo::Homebrew, "brew"},
}};

char ToUpperAscii(char c) {
  return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}
}  // namespace

Repo RepoFromName(const std::string& name) {
  for (const auto& entry : kRepoNames) {
    if (name == entry.name) {
      return entry.repo;
    }
  }
  return Repo::Unknown;
}

std::string RepoBadgeLabel(Repo repo) {
  for (const auto& entry : kRepoNames) {
    if (entry.repo == repo) {
      std::string label = entry.name;
      for (char& c : label) {
        c = ToUpperAscii(c);
      }
      return label;
    }
  }
  return "UNKNOWN";
}

ftxui::Color RepoColor(Repo repo) {
  switch (repo) {
    case Repo::Core:
      return theme::color::RepoCore;
    case Repo::Extra:
      return theme::color::RepoExtra;
    case Repo::Multilib:
      return theme::color::RepoMultilib;
    case Repo::Aur:
      return theme::color::RepoAur;
    case Repo::Flatpak:
      return theme::color::RepoFlatpak;
    case Repo::Homebrew:
      return theme::color::RepoHomebrew;
    case Repo::Unknown:
      break;
  }
  return theme::color::RepoExtra;
}

bool IsHeavy(const Package& package) {
  return package.install_size_bytes >= theme::size::kHeavyThresholdBytes;
}

std::string FormatSize(int64_t bytes) {
  char buffer[32];
  if (bytes >= theme::size::kTibCutoffBytes) {
    double tib = static_cast<double>(bytes) / static_cast<double>(theme::size::kBytesPerTiB);
    std::snprintf(buffer, sizeof(buffer), "%.2f TiB", tib);
  } else if (bytes >= theme::size::kGibCutoffBytes) {
    double gib = static_cast<double>(bytes) / static_cast<double>(theme::size::kBytesPerGiB);
    std::snprintf(buffer, sizeof(buffer), "%.2f GiB", gib);
  } else {
    int64_t mib = bytes / theme::size::kBytesPerMiB;
    std::snprintf(buffer, sizeof(buffer), "%lld MiB", static_cast<long long>(mib));
  }
  return buffer;
}

}  // namespace pacseek::model
