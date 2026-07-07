// main.cpp - entry point: pick a data source from the flags, load it, and hand
// off to the App. Keeps argument wiring out of the app and UI layers.
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "app/app.hpp"
#include "app/first_run.hpp"
#include "config/collections.hpp"
#include "config/config.hpp"
#include "data/alpm_source.hpp"
#include "data/composite_source.hpp"
#include "data/flatpak_source.hpp"
#include "data/homebrew_source.hpp"
#include "data/mock_source.hpp"
#include "data/package_source.hpp"
#include "model/collection.hpp"
#include "system/transaction.hpp"

// Stamped in by CMake from the project version; fall back to a dev marker so the
// binary still builds outside the CMake toolchain.
#ifndef PACSEEK_VERSION
#define PACSEEK_VERSION "0.0.0-dev"
#endif

namespace {

constexpr const char* kUsage =
    "pacseek - tech-brutalist TUI package manager for pacman + AUR\n"
    "\n"
    "Usage: pacseek [options]\n"
    "  --mock         Use the built-in prototype dataset instead of the live system\n"
    "  --ascii        Use the ASCII glyph set (for terminals without dependable\n"
    "                 ambiguous-width handling, e.g. alacritty, ghostty, the tty)\n"
    "  -V, --version  Show the version and exit\n"
    "  -h, --help     Show this help and exit\n"
    "\n"
    "Keys: 1-6 switch view · / search · j/k move · s sort · enter action · q quit\n";

bool HasFlag(int argc, char** argv, const std::string& flag) {
  for (int index = 1; index < argc; ++index) {
    if (flag == argv[index]) {
      return true;
    }
  }
  return false;
}

std::unique_ptr<pacseek::data::PackageSource> SelectSource(bool use_mock,
                                                          const pacseek::config::Config& config) {
  if (use_mock) {
    return std::make_unique<pacseek::data::MockSource>();
  }

  // Live system: libalpm (pacman + AUR) always, plus the optional managers the
  // user surfaced (and whose CLI is actually installed). Flatpak and Homebrew
  // ride the same PackageSource seam, so the rest of the app is unaffected.
  std::vector<std::unique_ptr<pacseek::data::PackageSource>> sources;
  sources.push_back(std::make_unique<pacseek::data::AlpmSource>());
  if (config.flatpak_enabled && pacseek::system::IsToolAvailable("flatpak")) {
    sources.push_back(std::make_unique<pacseek::data::FlatpakSource>());
  }
  if (config.homebrew_enabled && pacseek::system::IsToolAvailable("brew")) {
    sources.push_back(std::make_unique<pacseek::data::HomebrewSource>());
  }
  if (sources.size() == 1) {
    return std::move(sources.front());
  }
  return std::make_unique<pacseek::data::CompositeSource>(std::move(sources));
}

}  // namespace

int main(int argc, char** argv) {
  if (HasFlag(argc, argv, "-V") || HasFlag(argc, argv, "--version")) {
    std::cout << "pacseek " << PACSEEK_VERSION << "\n";
    return EXIT_SUCCESS;
  }
  if (HasFlag(argc, argv, "-h") || HasFlag(argc, argv, "--help")) {
    std::cout << kUsage;
    return EXIT_SUCCESS;
  }

  // Anything that isn't a known flag is an error: a mistyped `--moc` silently
  // launching against the live system is worse than refusing to start.
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument != "--mock" && argument != "--ascii") {
      std::cerr << "pacseek: unknown argument '" << argument << "'\n\n" << kUsage;
      return EXIT_FAILURE;
    }
  }

  const bool use_mock = HasFlag(argc, argv, "--mock");
  pacseek::config::Config config = pacseek::config::LoadConfig();

  // First run (no config file yet): ask which package managers to surface, then
  // persist the answers so the prompt never shows again. The chooser seeds its
  // toggles from tool detection. Skipped under --mock, which is a stateless
  // preview that shouldn't write config or interrogate the user's setup.
  if (config.first_run && !use_mock) {
    config = pacseek::app::RunFirstRunSetup(config, pacseek::system::DetectTools());
    pacseek::config::PersistFirstRunChoices(config);
  }

  // The --ascii flag forces the compatibility glyph set for this run, overriding
  // whatever the config says; the App installs the set before its first frame.
  if (HasFlag(argc, argv, "--ascii")) {
    config.ascii_glyphs = true;
  }

  std::unique_ptr<pacseek::data::PackageSource> source = SelectSource(use_mock, config);

  // User-defined collections merge in after the built-ins. A malformed file is a
  // hard error: refuse to start and name every offender rather than silently
  // dropping collections or launching with a surprising set.
  const pacseek::config::CollectionsResult collections = pacseek::config::LoadUserCollections();
  if (!collections.errors.empty()) {
    std::cerr << "pacseek: refusing to start - user-defined collections are invalid:\n";
    for (const pacseek::config::CollectionError& error : collections.errors) {
      std::cerr << "  " << pacseek::config::FormatCollectionError(error) << "\n";
    }
    std::cerr << "Fix " << pacseek::config::DefaultCollectionsPath() << " and try again.\n";
    return EXIT_FAILURE;
  }
  pacseek::model::SetUserCollections(collections.collections);

  // Fold pacman's official groups (base-devel, …) in as collections. Re-opens
  // libalpm once, mirroring the other one-shot startup loads; under --mock the
  // source returns none, so the picker is unchanged.
  pacseek::model::SetGroupCollections(source->Groups());

  try {
    pacseek::app::App app(*source, config);
    app.Run();
  } catch (const std::exception& error) {
    std::cerr << "pacseek: " << error.what() << "\n"
              << "Try '--mock' to run against the built-in dataset instead.\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
