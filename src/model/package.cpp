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
constexpr std::array<RepoName, 9> kRepoNames = {{
    {Repo::Core, "core"},
    {Repo::Extra, "extra"},
    {Repo::Multilib, "multilib"},
    {Repo::Aur, "aur"},
    {Repo::Flatpak, "flatpak"},
    {Repo::Homebrew, "brew"},
    {Repo::Npm, "npm"},
    {Repo::Bun, "bun"},
    {Repo::Pnpm, "pnpm"},
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
    case Repo::Npm:
      return theme::color::RepoNpm;
    case Repo::Bun:
      return theme::color::RepoBun;
    case Repo::Pnpm:
      return theme::color::RepoPnpm;
    case Repo::Unknown:
      break;
  }
  return theme::color::RepoExtra;
}

bool RepoInSource(Repo repo, Source source) {
  switch (source) {
    case Source::All:
      return true;
    case Source::Pacman:
      return repo == Repo::Core || repo == Repo::Extra || repo == Repo::Multilib;
    case Source::Aur:
      return repo == Repo::Aur;
    case Source::Flatpak:
      return repo == Repo::Flatpak;
    case Source::Homebrew:
      return repo == Repo::Homebrew;
    case Source::Npm:
      return repo == Repo::Npm;
    case Source::Bun:
      return repo == Repo::Bun;
    case Source::Pnpm:
      return repo == Repo::Pnpm;
  }
  return false;
}

std::string SourceLabel(Source source) {
  switch (source) {
    case Source::All:
      return "ALL";
    case Source::Pacman:
      return "PACMAN";
    case Source::Aur:
      return "AUR";
    case Source::Flatpak:
      return "FLATPAK";
    case Source::Homebrew:
      return "BREW";
    case Source::Npm:
      return "NPM";
    case Source::Bun:
      return "BUN";
    case Source::Pnpm:
      return "PNPM";
  }
  return "ALL";
}

ftxui::Color SourceColor(Source source) {
  switch (source) {
    case Source::All:
      return theme::color::TextMuted;
    case Source::Pacman:
      return theme::color::RepoCore;
    case Source::Aur:
      return theme::color::RepoAur;
    case Source::Flatpak:
      return theme::color::RepoFlatpak;
    case Source::Homebrew:
      return theme::color::RepoHomebrew;
    case Source::Npm:
      return theme::color::RepoNpm;
    case Source::Bun:
      return theme::color::RepoBun;
    case Source::Pnpm:
      return theme::color::RepoPnpm;
  }
  return theme::color::TextMuted;
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
