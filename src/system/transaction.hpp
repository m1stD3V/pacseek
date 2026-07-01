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

enum class Action { Install, Remove, Update };

// Which package manager owns a package, selecting how a transaction is built.
enum class Manager { Pacman, Aur, Flatpak };

// External tools available for transactions, probed once from PATH.
struct Tools {
  bool has_sudo = false;
  std::string aur_helper;     // "paru", "yay", … or empty when none is installed
  bool has_flatpak = false;   // the `flatpak` CLI is on PATH
};

// Probes PATH for sudo, a known AUR helper, and flatpak.
Tools DetectTools();

// True when `name` is an executable on PATH. Used to validate a configured AUR
// helper before trusting it over what DetectTools probed.
bool IsToolAvailable(const std::string& name);

// Builds the shell command for a transaction, or returns "" and fills `error`
// when it cannot be performed (an unsafe name, missing sudo/helper/flatpak).
// Pure and side-effect free. `manager` selects the tool. Action::Update is a
// full system upgrade and ignores package_name / manager.
std::string BuildCommandLine(Action action, const std::string& package_name, Manager manager,
                             const Tools& tools, std::string& error);

// Builds one command line that applies `action` to every name in `names`, all
// sharing the action (Install or Remove) and the `manager`. Returns "" and fills
// `error` on an unsafe name, an empty set, or a missing tool. Pure.
std::string BuildBatchCommandLine(Action action, const std::vector<std::string>& names,
                                  Manager manager, const Tools& tools, std::string& error);

// Runs a prepared command line in the current terminal, which the caller must
// have already restored from the alternate screen. Prints a header, then waits
// for Enter so the user can read pacman's output. Returns the exit status
// (0 == success).
int RunInTerminal(const std::string& command_line, const std::string& header);

}  // namespace pacseek::system
