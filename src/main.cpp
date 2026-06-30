// main.cpp — entry point: pick a data source from the flags, load it, and hand
// off to the App. Keeps argument wiring out of the app and UI layers.
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "app/app.hpp"
#include "data/alpm_source.hpp"
#include "data/mock_source.hpp"
#include "data/package_source.hpp"

namespace {

constexpr const char* kUsage =
    "pacseek — tech-brutalist TUI package manager for pacman + AUR\n"
    "\n"
    "Usage: pacseek [options]\n"
    "  --mock        Use the built-in prototype dataset instead of the live system\n"
    "  -h, --help    Show this help and exit\n"
    "\n"
    "Keys: 1-4 switch view · / search · j/k move · s sort · enter action · q quit\n";

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
  return std::make_unique<pacseek::data::AlpmSource>();
}

}  // namespace

int main(int argc, char** argv) {
  if (HasFlag(argc, argv, "-h") || HasFlag(argc, argv, "--help")) {
    std::cout << kUsage;
    return EXIT_SUCCESS;
  }

  const bool use_mock = HasFlag(argc, argv, "--mock");
  std::unique_ptr<pacseek::data::PackageSource> source = SelectSource(use_mock);

  try {
    pacseek::app::App app(*source);
    app.Run();
  } catch (const std::exception& error) {
    std::cerr << "pacseek: " << error.what() << "\n"
              << "Try '--mock' to run against the built-in dataset instead.\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
