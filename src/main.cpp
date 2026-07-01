// main.cpp - entry point: pick a data source from the flags, load it, and hand
// off to the App. Keeps argument wiring out of the app and UI layers.
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "app/app.hpp"
#include "config/collections.hpp"
#include "config/config.hpp"
#include "data/alpm_source.hpp"
#include "data/composite_source.hpp"
#include "data/flatpak_source.hpp"
#include "data/mock_source.hpp"
#include "data/package_source.hpp"
#include "model/collection.hpp"
#include "system/transaction.hpp"

namespace {

constexpr const char* kUsage =
    "pacseek - tech-brutalist TUI package manager for pacman + AUR\n"
    "\n"
    "Usage: pacseek [options]\n"
    "  --mock        Use the built-in prototype dataset instead of the live system\n"
    "  -h, --help    Show this help and exit\n"
    "\n"
    "Keys: 1-5 switch view · / search · j/k move · s sort · enter action · q quit\n";

bool HasFlag(int argc, char** argv, const std::string& flag) {
  for (int index = 1; index < argc; ++index) {
    if (flag == argv[index]) {
      return true;
    }
  }
  return false;
}

std::unique_ptr<pacseek::data::PackageSource> SelectSource(bool use_mock) {
  if (use_mock) {
    return std::make_unique<pacseek::data::MockSource>();
  }

  // Live system: libalpm (pacman + AUR), plus flatpak when it is installed.
  std::vector<std::unique_ptr<pacseek::data::PackageSource>> sources;
  sources.push_back(std::make_unique<pacseek::data::AlpmSource>());
  if (pacseek::system::IsToolAvailable("flatpak")) {
    sources.push_back(std::make_unique<pacseek::data::FlatpakSource>());
  }
  if (sources.size() == 1) {
    return std::move(sources.front());
  }
  return std::make_unique<pacseek::data::CompositeSource>(std::move(sources));
}

}  // namespace

int main(int argc, char** argv) {
  if (HasFlag(argc, argv, "-h") || HasFlag(argc, argv, "--help")) {
    std::cout << kUsage;
    return EXIT_SUCCESS;
  }

  const bool use_mock = HasFlag(argc, argv, "--mock");
  std::unique_ptr<pacseek::data::PackageSource> source = SelectSource(use_mock);
  const pacseek::config::Config config = pacseek::config::LoadConfig();

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
