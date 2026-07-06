// transaction.hpp - applying install / remove to the live system by shelling out
// to pacman (repo packages), a detected AUR helper, or flatpak. The TUI suspends,
// the command runs in the restored terminal, then the caller reloads the catalog.
//
// This module is deliberately free of model/ and FTXUI dependencies: it speaks
// only in package names and a Manager tag, so command building is pure and
// trivially testable.
#pragma once

#include <string>
#include <vector>

namespace pacseek::system {

// SetExplicit / SetDependency flip a package's recorded install reason via
// `pacman -D`; they take a package name but never touch files, so orphan
// detection can be trusted after the user corrects a reason. CleanCache runs
// `paccache -r` (keeps the 3 most recent versions of each cached package);
// MergeConfigs runs `pacdiff` to walk the unmerged .pacnew/.pacsave files.
// RefreshDatabases runs `pacman -Sy` so the update counts reflect upstream
// again; the app's partial-upgrade guard covers the -Sy-then-install hazard.
// None of the last three takes a package name.
enum class Action {
  Install,
  Remove,
  Update,
  SetExplicit,
  SetDependency,
  CleanCache,
  MergeConfigs,
  RefreshDatabases,
};

// Which package manager owns a package, selecting how a transaction is built.
enum class Manager { Pacman, Aur, Flatpak };

// External tools available for transactions, probed once from PATH.
struct Tools {
  bool has_sudo = false;
  std::string aur_helper;     // "paru", "yay", … or empty when none is installed
  bool has_flatpak = false;   // the `flatpak` CLI is on PATH
  bool has_paccache = false;  // paccache (pacman-contrib), for CleanCache
  bool has_pacdiff = false;   // pacdiff (pacman-contrib), for MergeConfigs
};

// Probes PATH for sudo, a known AUR helper, and flatpak.
Tools DetectTools();

// True when `name` is a plain command name (package-name alphabet, no '/', no
// leading dash) AND an executable on PATH. Used to validate a configured AUR
// helper before trusting it over what DetectTools probed.
bool IsToolAvailable(const std::string& name);

// Builds the shell command for a transaction, or returns "" and fills `error`
// when it cannot be performed (an unsafe name, missing sudo/helper/flatpak).
// Pure and side-effect free. `manager` selects the tool. Action::Update is a
// full system upgrade and ignores package_name / manager; when
// `flatpak_update_pending` is set (and flatpak is present) it chains a
// `flatpak update` after the pacman/helper upgrade so both worlds refresh.
// CleanCache / MergeConfigs likewise ignore package_name / manager.
std::string BuildCommandLine(Action action, const std::string& package_name, Manager manager,
                             const Tools& tools, std::string& error,
                             bool flatpak_update_pending = false);

// Builds one command line that applies `action` to every name in `names`, all
// sharing the action (Install or Remove) and the `manager`. Returns "" and fills
// `error` on an unsafe name, an empty set, or a missing tool. Pure.
std::string BuildBatchCommandLine(Action action, const std::vector<std::string>& names,
                                  Manager manager, const Tools& tools, std::string& error);

// Runs a prepared command line in the current terminal, which the caller must
// have already restored from the alternate screen. Prints a header, then waits
// for Enter so the user can read pacman's output. Returns the command's decoded
// exit code (0 == success; 128+signal when killed; 127 when it couldn't start).
int RunInTerminal(const std::string& command_line, const std::string& header);

}  // namespace pacseek::system
