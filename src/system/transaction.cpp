#include "system/transaction.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace pacseek::system {

namespace {

// Arch package names use a restricted alphabet. Rejecting anything outside it
// means a catalog name can never smuggle shell syntax into the command line.
// The first character additionally may not be '-' or '.' (pacman's own naming
// rule): a leading dash would otherwise let a name from an imported list or a
// remote AUR response be parsed as a flag by the invoked tool.
bool IsSafePackageName(const std::string& name) {
  if (name.empty() || name[0] == '-' || name[0] == '.') {
    return false;
  }
  for (char c : name) {
    const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                         (c >= '0' && c <= '9') || c == '@' || c == '.' || c == '_' ||
                         c == '+' || c == '-';
    if (!allowed) {
      return false;
    }
  }
  return true;
}

// A name safe to echo into a status/error line: control characters (which could
// carry terminal escape sequences) are replaced and the tail is elided so one
// bad entry can't flood or corrupt the display.
std::string DisplayName(const std::string& name) {
  constexpr size_t kMaxShown = 64;
  std::string shown;
  for (char c : name) {
    if (shown.size() >= kMaxShown) {
      shown += "…";
      break;
    }
    shown += (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) ? '?' : c;
  }
  return shown;
}

// True when `name` is an executable found in one of the PATH directories.
bool OnPath(const std::string& name) {
  const char* path_env = std::getenv("PATH");
  if (path_env == nullptr) {
    return false;
  }
  const std::string path = path_env;
  size_t start = 0;
  while (start <= path.size()) {
    const size_t colon = path.find(':', start);
    const size_t end = colon == std::string::npos ? path.size() : colon;
    const std::string dir = path.substr(start, end - start);
    if (!dir.empty() && ::access((dir + "/" + name).c_str(), X_OK) == 0) {
      return true;
    }
    if (colon == std::string::npos) {
      break;
    }
    start = colon + 1;
  }
  return false;
}

}  // namespace

Tools DetectTools() {
  Tools tools;
  tools.has_sudo = OnPath("sudo");
  // Probed in preference order; the first one found wins. Only helpers that
  // speak pacman's syntax (`-S <pkg>` installs AUR packages, `-Syu` upgrades
  // both worlds) are eligible - aura routes AUR through `-A` and pamac uses
  // `install`, so picking either would run the wrong operation.
  for (const char* helper : {"paru", "yay", "pikaur", "trizen"}) {
    if (OnPath(helper)) {
      tools.aur_helper = helper;
      break;
    }
  }
  tools.has_flatpak = OnPath("flatpak");
  tools.has_brew = OnPath("brew");
  tools.has_paccache = OnPath("paccache");
  tools.has_pacdiff = OnPath("pacdiff");
  return tools;
}

bool IsToolAvailable(const std::string& name) {
  // The name may come from the user's config (aur_helper=...); it becomes the
  // first word of a shell command, so hold it to the same restricted alphabet
  // as package names. That also rejects '/' - a path would resolve against a
  // PATH directory here but against the CWD when the shell runs it.
  return IsSafePackageName(name) && OnPath(name);
}

namespace {

// Removing a foreign (AUR) or repo package is the same pacman -Rs operation; only
// installs differ by manager. Shared by the single and batch builders. The `--`
// (here and in every other builder) ends option parsing before the validated
// names, a second fence behind IsSafePackageName's leading-dash rejection.
std::string PacmanRemove(const std::string& args, const Tools& tools, std::string& error) {
  if (!tools.has_sudo) {
    error = "sudo not found; cannot escalate to modify packages";
    return "";
  }
  // -Rs also removes dependencies that no other installed package still needs.
  return "sudo pacman -Rs --" + args;
}

}  // namespace

std::string BuildCommandLine(Action action, const std::string& package_name, Manager manager,
                             const Tools& tools, std::string& error,
                             bool flatpak_update_pending) {
  error.clear();

  // A full system upgrade takes no package name. Prefer the AUR helper when one
  // is present so repo and AUR packages refresh together; otherwise plain pacman
  // (which leaves AUR packages untouched). When flatpak apps also have pending
  // updates, chain a `flatpak update` behind a successful native upgrade -
  // flatpak escalates itself via polkit, so no sudo wrapper.
  if (action == Action::Update) {
    const std::string chain =
        (flatpak_update_pending && tools.has_flatpak) ? " && flatpak update" : "";
    if (!tools.aur_helper.empty()) {
      return tools.aur_helper + " -Syu" + chain;
    }
    if (!tools.has_sudo) {
      error = "sudo not found; cannot escalate to update packages";
      return "";
    }
    return "sudo pacman -Syu" + chain;
  }

  // Refreshing the sync databases is the one moment pacseek asks pacman to touch
  // the network - always user-initiated, never automatic. Without it the update
  // counts only reflect the last time something else ran -Sy.
  if (action == Action::RefreshDatabases) {
    if (!tools.has_sudo) {
      error = "sudo not found; cannot escalate to refresh the databases";
      return "";
    }
    return "sudo pacman -Sy";
  }

  // Cache clean and config merge are system maintenance, not package ops: both
  // come from pacman-contrib and take no package name.
  if (action == Action::CleanCache || action == Action::MergeConfigs) {
    const bool present = action == Action::CleanCache ? tools.has_paccache : tools.has_pacdiff;
    const char* tool = action == Action::CleanCache ? "paccache" : "pacdiff";
    if (!present) {
      error = std::string(tool) + " not found · install pacman-contrib";
      return "";
    }
    if (!tools.has_sudo) {
      error = "sudo not found; cannot escalate to run " + std::string(tool);
      return "";
    }
    // -r keeps the 3 most recent cached versions of each package; pacdiff walks
    // every unmerged .pacnew/.pacsave interactively.
    return action == Action::CleanCache ? "sudo paccache -r" : "sudo pacdiff";
  }

  if (!IsSafePackageName(package_name)) {
    error = "refusing unusual package name: " + DisplayName(package_name);
    return "";
  }

  // Flipping the install reason is always a local-db op (`pacman -D`); the
  // manager tag is irrelevant since even foreign/AUR packages live in the same
  // local database. Needs sudo to write the db.
  if (action == Action::SetExplicit || action == Action::SetDependency) {
    if (!tools.has_sudo) {
      error = "sudo not found; cannot escalate to change the install reason";
      return "";
    }
    const char* flag = action == Action::SetExplicit ? " --asexplicit -- " : " --asdeps -- ";
    return "sudo pacman -D" + std::string(flag) + package_name;
  }

  // flatpak manages its own privilege escalation (polkit), so no sudo wrapper;
  // it prompts in the restored terminal just like pacman does.
  if (manager == Manager::Flatpak) {
    if (!tools.has_flatpak) {
      error = "flatpak not found on PATH";
      return "";
    }
    return (action == Action::Install ? "flatpak install -- " : "flatpak uninstall -- ") +
           package_name;
  }

  // Homebrew manages its own prefix as the invoking user, so no sudo wrapper.
  // brew's parser has no `--` end-of-options separator, but IsSafePackageName has
  // already rejected any leading-dash name, so the formula name is safe bare.
  if (manager == Manager::Homebrew) {
    if (!tools.has_brew) {
      error = "brew not found on PATH";
      return "";
    }
    return (action == Action::Install ? "brew install " : "brew uninstall ") + package_name;
  }

  // Installing an AUR package goes through the helper, which runs as the normal
  // user (it escalates internally to build and install). Removal is pacman -Rs.
  if (manager == Manager::Aur && action == Action::Install) {
    if (tools.aur_helper.empty()) {
      error = "install an AUR helper (paru, yay, …) to build " + package_name;
      return "";
    }
    return tools.aur_helper + " -S " + package_name;
  }

  if (action == Action::Install) {
    if (!tools.has_sudo) {
      error = "sudo not found; cannot escalate to modify packages";
      return "";
    }
    return "sudo pacman -S --needed -- " + package_name;
  }
  return PacmanRemove(" " + package_name, tools, error);
}

std::string BuildBatchCommandLine(Action action, const std::vector<std::string>& names,
                                  Manager manager, const Tools& tools, std::string& error) {
  error.clear();
  if (names.empty()) {
    error = "nothing marked";
    return "";
  }

  // Validate every name and assemble the argument list. One bad name rejects the
  // whole batch rather than silently dropping it.
  std::string arguments;
  for (const std::string& name : names) {
    if (!IsSafePackageName(name)) {
      error = "refusing unusual package name: " + DisplayName(name);
      return "";
    }
    arguments += " " + name;
  }

  if (manager == Manager::Flatpak) {
    if (!tools.has_flatpak) {
      error = "flatpak not found on PATH";
      return "";
    }
    return std::string(action == Action::Install ? "flatpak install --" : "flatpak uninstall --") +
           arguments;
  }

  if (manager == Manager::Homebrew) {
    if (!tools.has_brew) {
      error = "brew not found on PATH";
      return "";
    }
    return std::string(action == Action::Install ? "brew install" : "brew uninstall") + arguments;
  }

  if (action == Action::Install) {
    if (manager == Manager::Aur) {
      if (tools.aur_helper.empty()) {
        error = "install an AUR helper (paru, yay, …) to build the marked AUR packages";
        return "";
      }
      return tools.aur_helper + " -S" + arguments;
    }
    if (!tools.has_sudo) {
      error = "sudo not found; cannot escalate to modify packages";
      return "";
    }
    return "sudo pacman -S --needed --" + arguments;
  }
  return PacmanRemove(arguments, tools, error);
}

int RunInTerminal(const std::string& command_line, const std::string& header) {
  std::cout << "\n\033[1;38;2;222;84;44m▶ " << header << "\033[0m\n"
            << "  \033[2m" << command_line << "\033[0m\n\n";
  std::cout.flush();

  // std::system returns a raw wait(2) status, not an exit code; decode it so a
  // failing pacman reads "exit 1" rather than "exit 256", and a command killed
  // mid-run (e.g. Ctrl-C at the sudo prompt) is reported as its signal.
  const int status = std::system(command_line.c_str());
  int exit_code;
  std::string outcome;
  if (status == -1) {
    exit_code = 127;
    outcome = "could not start command";
  } else if (WIFSIGNALED(status)) {
    exit_code = 128 + WTERMSIG(status);
    outcome = "killed by signal " + std::to_string(WTERMSIG(status));
  } else {
    exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
    outcome = "exit " + std::to_string(exit_code);
  }

  std::cout << "\n\033[2m── done (" << outcome << ") · press Enter to return ──\033[0m";
  std::cout.flush();
  std::string discard;
  std::getline(std::cin, discard);
  return exit_code;
}

}  // namespace pacseek::system
