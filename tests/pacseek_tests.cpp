// pacseek_tests.cpp - dependency-free checks for the pure layers: the command
// builders (the security boundary), the config and collections parsers, and the
// catalog's filtering/sorting. Run via `ctest --test-dir build`.
//
// Deliberately framework-free: a failed CHECK names its file:line and the test
// binary exits non-zero, which is all CTest needs.
#include <iostream>
#include <string>
#include <vector>

#include "config/collections.hpp"
#include "config/config.hpp"
#include "model/catalog.hpp"
#include "system/transaction.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                          \
  do {                                                                            \
    if (!(condition)) {                                                           \
      ++failures;                                                                 \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #condition   \
                << "\n";                                                          \
    }                                                                             \
  } while (0)

#define CHECK_EQ(actual, expected)                                                \
  do {                                                                            \
    const auto check_eq_actual = (actual);                                        \
    const auto check_eq_expected = (expected);                                    \
    if (!(check_eq_actual == check_eq_expected)) {                                \
      ++failures;                                                                 \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #actual      \
                << "\n  got:      " << check_eq_actual                            \
                << "\n  expected: " << check_eq_expected << "\n";                 \
    }                                                                             \
  } while (0)

using namespace pacseek;
using system::Action;
using system::Manager;
using system::Tools;

Tools FullTools() {
  Tools tools;
  tools.has_sudo = true;
  tools.has_flatpak = true;
  tools.has_brew = true;
  tools.has_npm = true;
  tools.has_bun = true;
  tools.has_pnpm = true;
  tools.has_paccache = true;
  tools.has_pacdiff = true;
  tools.aur_helper = "paru";
  return tools;
}

// --- transaction: command building ------------------------------------------

void TestSingleCommands() {
  const Tools tools = FullTools();
  std::string error;

  CHECK_EQ(system::BuildCommandLine(Action::Install, "ripgrep", Manager::Pacman, tools, error),
           "sudo pacman -S --needed -- ripgrep");
  CHECK_EQ(system::BuildCommandLine(Action::Remove, "ripgrep", Manager::Pacman, tools, error),
           "sudo pacman -Rs -- ripgrep");
  CHECK_EQ(system::BuildCommandLine(Action::SetExplicit, "ripgrep", Manager::Pacman, tools, error),
           "sudo pacman -D --asexplicit -- ripgrep");
  CHECK_EQ(system::BuildCommandLine(Action::SetDependency, "ripgrep", Manager::Pacman, tools, error),
           "sudo pacman -D --asdeps -- ripgrep");
  CHECK_EQ(system::BuildCommandLine(Action::Install, "org.gimp.GIMP", Manager::Flatpak, tools, error),
           "flatpak install -- org.gimp.GIMP");
  CHECK_EQ(system::BuildCommandLine(Action::Remove, "org.gimp.GIMP", Manager::Flatpak, tools, error),
           "flatpak uninstall -- org.gimp.GIMP");
  CHECK_EQ(system::BuildCommandLine(Action::Install, "paru-bin", Manager::Aur, tools, error),
           "paru -S paru-bin");
  // AUR removal is plain pacman: the package is in the local db like any other.
  CHECK_EQ(system::BuildCommandLine(Action::Remove, "paru-bin", Manager::Aur, tools, error),
           "sudo pacman -Rs -- paru-bin");
  // Homebrew runs as the invoking user (no sudo) and speaks install/uninstall.
  CHECK_EQ(system::BuildCommandLine(Action::Install, "ripgrep", Manager::Homebrew, tools, error),
           "brew install ripgrep");
  CHECK_EQ(system::BuildCommandLine(Action::Remove, "ripgrep", Manager::Homebrew, tools, error),
           "brew uninstall ripgrep");

  // No brew on PATH: a Homebrew transaction refuses rather than shelling out.
  Tools no_brew = tools;
  no_brew.has_brew = false;
  CHECK_EQ(system::BuildCommandLine(Action::Install, "ripgrep", Manager::Homebrew, no_brew, error),
           "");
  CHECK(!error.empty());
}

void TestUpdateCommands() {
  Tools tools = FullTools();
  std::string error;

  CHECK_EQ(system::BuildCommandLine(Action::Update, "", Manager::Pacman, tools, error),
           "paru -Syu");
  CHECK_EQ(system::BuildCommandLine(Action::Update, "", Manager::Pacman, tools, error,
                                    /*flatpak_update_pending=*/true),
           "paru -Syu && flatpak update");

  tools.aur_helper.clear();
  CHECK_EQ(system::BuildCommandLine(Action::Update, "", Manager::Pacman, tools, error),
           "sudo pacman -Syu");
  CHECK_EQ(system::BuildCommandLine(Action::Update, "", Manager::Pacman, tools, error, true),
           "sudo pacman -Syu && flatpak update");

  // No flatpak on PATH: the chain is dropped even when updates are pending.
  tools.has_flatpak = false;
  CHECK_EQ(system::BuildCommandLine(Action::Update, "", Manager::Pacman, tools, error, true),
           "sudo pacman -Syu");

  tools.has_sudo = false;
  CHECK_EQ(system::BuildCommandLine(Action::Update, "", Manager::Pacman, tools, error), "");
  CHECK(!error.empty());
}

void TestMaintenanceCommands() {
  Tools tools = FullTools();
  std::string error;

  CHECK_EQ(system::BuildCommandLine(Action::CleanCache, "", Manager::Pacman, tools, error),
           "sudo paccache -r");
  CHECK_EQ(system::BuildCommandLine(Action::MergeConfigs, "", Manager::Pacman, tools, error),
           "sudo pacdiff");

  tools.has_paccache = false;
  CHECK_EQ(system::BuildCommandLine(Action::CleanCache, "", Manager::Pacman, tools, error), "");
  CHECK(error.find("pacman-contrib") != std::string::npos);

  tools = FullTools();
  tools.has_pacdiff = false;
  CHECK_EQ(system::BuildCommandLine(Action::MergeConfigs, "", Manager::Pacman, tools, error), "");
  CHECK(error.find("pacman-contrib") != std::string::npos);

  tools = FullTools();
  CHECK_EQ(system::BuildCommandLine(Action::RefreshDatabases, "", Manager::Pacman, tools, error),
           "sudo pacman -Sy");

  tools.has_sudo = false;
  CHECK_EQ(system::BuildCommandLine(Action::CleanCache, "", Manager::Pacman, tools, error), "");
  CHECK(!error.empty());
  CHECK_EQ(system::BuildCommandLine(Action::RefreshDatabases, "", Manager::Pacman, tools, error),
           "");
  CHECK(!error.empty());
}

void TestNodeCommands() {
  const Tools tools = FullTools();
  std::string error;

  // Each JavaScript global manager installs/removes globally as the invoking user
  // (no sudo), with its own verbs.
  CHECK_EQ(system::BuildCommandLine(Action::Install, "typescript", Manager::Npm, tools, error),
           "npm install -g typescript");
  CHECK_EQ(system::BuildCommandLine(Action::Remove, "typescript", Manager::Npm, tools, error),
           "npm uninstall -g typescript");
  CHECK_EQ(system::BuildCommandLine(Action::Install, "vite", Manager::Bun, tools, error),
           "bun add -g vite");
  CHECK_EQ(system::BuildCommandLine(Action::Remove, "vite", Manager::Bun, tools, error),
           "bun remove -g vite");
  CHECK_EQ(system::BuildCommandLine(Action::Install, "eslint", Manager::Pnpm, tools, error),
           "pnpm add -g eslint");
  CHECK_EQ(system::BuildCommandLine(Action::Remove, "eslint", Manager::Pnpm, tools, error),
           "pnpm remove -g eslint");

  // Scoped names (@scope/pkg) carry a '/', which the pacman alphabet rejects but
  // the node validator allows - the whole point of node-specific validation.
  CHECK_EQ(system::BuildCommandLine(Action::Install, "@angular/cli", Manager::Npm, tools, error),
           "npm install -g @angular/cli");
  CHECK_EQ(system::BuildCommandLine(Action::Install, "@angular/cli", Manager::Pacman, tools, error),
           "");
  CHECK(!error.empty());

  // A scoped batch installs together under one manager.
  const std::vector<std::string> names = {"@angular/cli", "typescript"};
  CHECK_EQ(system::BuildBatchCommandLine(Action::Install, names, Manager::Npm, tools, error),
           "npm install -g @angular/cli typescript");

  // Injection vectors are still refused for node managers (leading dash, shell
  // metacharacters, path traversal beyond a single scope separator).
  const std::vector<std::string> bad = {"-g", "--force", "a;rm", "$(x)", "a b", ""};
  for (const std::string& name : bad) {
    CHECK_EQ(system::BuildCommandLine(Action::Install, name, Manager::Npm, tools, error), "");
    CHECK(!error.empty());
  }

  // CLI absent: the transaction refuses rather than shelling out to a missing tool.
  Tools no_pnpm = tools;
  no_pnpm.has_pnpm = false;
  CHECK_EQ(system::BuildCommandLine(Action::Install, "eslint", Manager::Pnpm, no_pnpm, error), "");
  CHECK(!error.empty());
}

void TestNameValidation() {
  const Tools tools = FullTools();
  std::string error;

  // Flag-shaped, dotfile-shaped, and shell-metacharacter names must be refused
  // for every manager - these are the argument-injection vectors.
  const std::vector<std::string> bad = {"--noconfirm", "-Rs",   ".hidden", "a b",
                                        "x;rm",        "$(x)",  "a`b`",    "",
                                        "pkg\nother",  "a|b",   "a>b"};
  for (const std::string& name : bad) {
    CHECK_EQ(system::BuildCommandLine(Action::Install, name, Manager::Pacman, tools, error), "");
    CHECK(!error.empty());
    CHECK_EQ(system::BuildCommandLine(Action::Install, name, Manager::Aur, tools, error), "");
    CHECK_EQ(system::BuildCommandLine(Action::Install, name, Manager::Flatpak, tools, error), "");
  }

  // One bad name rejects a whole batch, and the error never echoes control
  // bytes (a crafted name can't smuggle terminal escapes into the status line).
  std::vector<std::string> names = {"good", std::string("evil\x1b[2J")};
  CHECK_EQ(system::BuildBatchCommandLine(Action::Remove, names, Manager::Pacman, tools, error), "");
  CHECK(error.find('\x1b') == std::string::npos);

  names = {"ripgrep", "fd"};
  CHECK_EQ(system::BuildBatchCommandLine(Action::Remove, names, Manager::Pacman, tools, error),
           "sudo pacman -Rs -- ripgrep fd");
  CHECK_EQ(system::BuildBatchCommandLine(Action::Install, names, Manager::Pacman, tools, error),
           "sudo pacman -S --needed -- ripgrep fd");
  CHECK_EQ(system::BuildBatchCommandLine(Action::Install, names, Manager::Flatpak, tools, error),
           "flatpak install -- ripgrep fd");
  CHECK_EQ(system::BuildBatchCommandLine(Action::Install, names, Manager::Aur, tools, error),
           "paru -S ripgrep fd");
  CHECK_EQ(system::BuildBatchCommandLine(Action::Install, names, Manager::Homebrew, tools, error),
           "brew install ripgrep fd");
  CHECK_EQ(system::BuildBatchCommandLine(Action::Remove, names, Manager::Homebrew, tools, error),
           "brew uninstall ripgrep fd");

  // A configured helper name must be a plain command word: no paths (which the
  // shell would resolve against the CWD), no flags, no spaces.
  CHECK(!system::IsToolAvailable("../../usr/bin/sh"));
  CHECK(!system::IsToolAvailable("-paru"));
  CHECK(!system::IsToolAvailable("pa ru"));
  CHECK(system::IsToolAvailable("sh"));  // POSIX: present on any sane PATH
}

// --- config parser ------------------------------------------------------------

void TestConfigParser() {
  const config::Config defaults = config::ParseConfig("");
  CHECK(defaults.view == model::View::Browse);
  CHECK(defaults.sort == model::Sort::SizeDescending);
  CHECK_EQ(std::string(1, defaults.keys.quit), "q");

  const config::Config parsed = config::ParseConfig(
      "# comment\n"
      "; also a comment\n"
      "view = updates\n"
      "sort = name\n"
      "theme = tokyo-night\n"
      "aur_helper = yay\n"
      "key_quit = Q\n"
      "key_mark_all = A\n"
      "key_clean_cache = C\n"
      "key_pacdiff = M\n"
      "key_refresh = Y\n"
      "key_sort = toolong\n"     // multi-char: ignored, default kept
      "key_bogus = z\n"          // unknown action: ignored
      "not a key value line\n"   // ignored
      "unknown = whatever\n");   // unknown key: ignored
  CHECK(parsed.view == model::View::Updates);
  CHECK(parsed.sort == model::Sort::NameAscending);
  CHECK_EQ(parsed.theme, "tokyo-night");
  CHECK_EQ(parsed.aur_helper, "yay");
  CHECK_EQ(std::string(1, parsed.keys.quit), "Q");
  CHECK_EQ(std::string(1, parsed.keys.mark_all), "A");
  CHECK_EQ(std::string(1, parsed.keys.clean_cache), "C");
  CHECK_EQ(std::string(1, parsed.keys.pacdiff), "M");
  CHECK_EQ(std::string(1, parsed.keys.refresh), "Y");
  CHECK_EQ(std::string(1, parsed.keys.sort), "s");  // default survived

  // Values keep their case; keys and view/sort words don't care about it.
  const config::Config cased = config::ParseConfig("VIEW = OrPhAnS\nSORT = SIZE\n");
  CHECK(cased.view == model::View::Orphans);
  CHECK(cased.sort == model::Sort::SizeDescending);

  // Defaults keep the pre-selection behaviour when no package_managers key is set.
  CHECK(defaults.aur_enabled);
  CHECK(defaults.flatpak_enabled);
  CHECK(!defaults.homebrew_enabled);
  CHECK(!defaults.ascii_glyphs);

  // package_managers is authoritative: only the listed optional managers are on.
  const config::Config managers =
      config::ParseConfig("package_managers = pacman, aur, homebrew\nglyphs = ascii\n");
  CHECK(managers.aur_enabled);
  CHECK(!managers.flatpak_enabled);
  CHECK(managers.homebrew_enabled);
  CHECK(managers.ascii_glyphs);

  // "brew" is accepted as an alias, and glyphs = unicode turns ASCII back off.
  const config::Config brew =
      config::ParseConfig("package_managers = pacman, brew\nglyphs = unicode\n");
  CHECK(!brew.aur_enabled);
  CHECK(brew.homebrew_enabled);
  CHECK(!brew.ascii_glyphs);
}

// --- collections parser ---------------------------------------------------------

void TestCollectionsParser() {
  const config::CollectionsResult good = config::ParseCollections(
      "# comment\n"
      "[dev]\n"
      "name = Development\n"
      "icon = *\n"
      "description = tools\n"
      "packages = git, tmux , ripgrep\n"
      "[media]\n"
      "name = Media\n"
      "packages = mpv\n");
  CHECK(good.errors.empty());
  CHECK_EQ(static_cast<int>(good.collections.size()), 2);
  CHECK_EQ(good.collections[0].id, "dev");
  CHECK_EQ(static_cast<int>(good.collections[0].packages.size()), 3);
  CHECK_EQ(good.collections[0].packages[1], "tmux");
  CHECK(!good.collections[1].icon.empty());  // default glyph filled in
  // Parsed collections are tagged User and default to the Mixed manager.
  CHECK(good.collections[0].origin == model::CollectionOrigin::User);
  CHECK(good.collections[0].manager == model::CollectionManager::Mixed);

  // The manager key routes and tags a collection; a known value parses cleanly.
  const config::CollectionsResult with_manager = config::ParseCollections(
      "[aur-tools]\n"
      "name = AUR Tools\n"
      "manager = aur\n"
      "packages = paru, yay\n");
  CHECK(with_manager.errors.empty());
  CHECK(with_manager.collections[0].manager == model::CollectionManager::Aur);

  // An unknown manager is a hard error, like every other malformation.
  const config::CollectionsResult bad_manager = config::ParseCollections(
      "[oops]\n"
      "name = Oops\n"
      "manager = choco\n"
      "packages = git\n");
  CHECK(!bad_manager.errors.empty());
  CHECK(bad_manager.collections.empty());

  // Each malformation is an error, and any error rejects the whole file.
  const config::CollectionsResult bad = config::ParseCollections(
      "stray = line\n"                    // key before any section
      "[dev]\n"
      "packages = git\n"                  // missing name
      "[dev]\n"                           // duplicate id
      "name = Again\n"
      "packages = git\n"
      "[empty]\n"
      "name = Empty\n"
      "packages = , \n"                   // only stray commas
      "[broken\n");                       // malformed header
  CHECK(!bad.errors.empty());
  CHECK(bad.collections.empty());
}

// --- catalog ---------------------------------------------------------------------

model::Package MakePackage(const std::string& name, model::Repo repo, int64_t size,
                           bool installed, bool update = false, bool orphan = false) {
  model::Package package;
  package.name = name;
  package.description = name + " description";
  package.repo = repo;
  package.install_size_bytes = size;
  package.installed = installed;
  package.has_update = update;
  package.is_orphan = orphan;
  return package;
}

void TestCatalog() {
  std::vector<model::Package> packages = {
      MakePackage("alpha", model::Repo::Core, 300, true, /*update=*/true),
      MakePackage("beta", model::Repo::Extra, 200, false),
      MakePackage("gamma", model::Repo::Aur, 100, true, false, /*orphan=*/true),
      MakePackage("delta-app", model::Repo::Flatpak, 400, true, /*update=*/true),
  };
  const model::Catalog catalog(std::move(packages));

  CHECK_EQ(catalog.CountForView(model::View::Browse), 4);
  CHECK_EQ(catalog.CountForView(model::View::Installed), 3);
  CHECK_EQ(catalog.CountForView(model::View::Updates), 2);
  CHECK_EQ(catalog.CountForView(model::View::Orphans), 1);
  // The partial-upgrade guard must not count the stale flatpak.
  CHECK_EQ(catalog.PacmanUpdateCount(), 1);

  // Query matches name or description, case-insensitively.
  CHECK_EQ(static_cast<int>(
               catalog.Visible(model::View::Browse, "GAMMA", model::Sort::NameAscending).size()),
           1);
  CHECK_EQ(static_cast<int>(
               catalog.Visible(model::View::Browse, "description", model::Sort::NameAscending)
                   .size()),
           4);

  // Size sort is descending; name sort ascending.
  const auto by_size = catalog.Visible(model::View::Browse, "", model::Sort::SizeDescending);
  CHECK_EQ(by_size.front()->name, "delta-app");
  CHECK_EQ(by_size.back()->name, "gamma");
  const auto by_name = catalog.Visible(model::View::Browse, "", model::Sort::NameAscending);
  CHECK_EQ(by_name.front()->name, "alpha");

  // Equal sizes keep insertion order (stable sort) - this is what preserves the
  // AUR popularity ranking under the default size sort, where all sizes are 0.
  std::vector<model::Package> ties = {
      MakePackage("most-popular", model::Repo::Aur, 0, false),
      MakePackage("less-popular", model::Repo::Aur, 0, false),
      MakePackage("least-popular", model::Repo::Aur, 0, false),
  };
  const model::Catalog tie_catalog(std::move(ties));
  const auto tied = tie_catalog.Visible(model::View::Browse, "", model::Sort::SizeDescending);
  CHECK_EQ(tied.front()->name, "most-popular");
  CHECK_EQ(tied.back()->name, "least-popular");

  const model::Catalog::Membership counts =
      catalog.MembershipCounts({"alpha", "beta", "not-present"});
  CHECK_EQ(counts.available, 2);
  CHECK_EQ(counts.installed, 1);
}

}  // namespace

int main() {
  TestSingleCommands();
  TestUpdateCommands();
  TestMaintenanceCommands();
  TestNodeCommands();
  TestNameValidation();
  TestConfigParser();
  TestCollectionsParser();
  TestCatalog();

  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "all checks passed\n";
  return 0;
}
