// transaction.hpp — applying install / remove to the live system by shelling out
// to pacman (repo packages) or a detected AUR helper. The TUI suspends, the
// command runs in the restored terminal, then the caller reloads the catalog.
//
// This module is deliberately free of model/ and FTXUI dependencies: it speaks
// only in package names and an is-AUR flag, so command building is pure and
// trivially testable.
#pragma once

#include <string>

namespace pacseek::system {

enum class Action { Install, Remove };

// External tools available for transactions, probed once from PATH.
struct Tools {
  bool has_sudo = false;
  std::string aur_helper;  // "paru", "yay", or empty when none is installed
};

// Probes PATH for sudo and a known AUR helper.
Tools DetectTools();

// Builds the shell command for a transaction, or returns "" and fills `error`
// when it cannot be performed (an unsafe name, missing sudo, or an AUR install
// with no helper). Pure and side-effect free.
//   is_aur — the package belongs to the AUR, which changes how installs run.
std::string BuildCommandLine(Action action, const std::string& package_name, bool is_aur,
                             const Tools& tools, std::string& error);

// Runs a prepared command line in the current terminal, which the caller must
// have already restored from the alternate screen. Prints a header, then waits
// for Enter so the user can read pacman's output. Returns the exit status
// (0 == success).
int RunInTerminal(const std::string& command_line, const std::string& header);

}  // namespace pacseek::system
