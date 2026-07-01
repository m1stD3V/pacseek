#include "system/transaction.hpp"

#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace pacseek::system {

namespace {

// Arch package names use a restricted alphabet. Rejecting anything outside it
// means a catalog name can never smuggle shell syntax into the command line.
bool IsSafePackageName(const std::string& name) {
  if (name.empty()) {
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
  // Probed in preference order; the first one found wins. All take `-S <pkg>`.
  for (const char* helper : {"paru", "yay", "pikaur", "aura", "trizen", "pamac"}) {
    if (OnPath(helper)) {
      tools.aur_helper = helper;
      break;
    }
  }
  tools.has_flatpak = OnPath("flatpak");
  return tools;
}

bool IsToolAvailable(const std::string& name) { return OnPath(name); }

namespace {

// Removing a foreign (AUR) or repo package is the same pacman -Rs operation; only
// installs differ by manager. Shared by the single and batch builders.
std::string PacmanRemove(const std::string& args, const Tools& tools, std::string& error) {
  if (!tools.has_sudo) {
    error = "sudo not found; cannot escalate to modify packages";
    return "";
  }
  // -Rs also removes dependencies that no other installed package still needs.
  return "sudo pacman -Rs" + args;
}

}  // namespace

std::string BuildCommandLine(Action action, const std::string& package_name, Manager manager,
                             const Tools& tools, std::string& error) {
  error.clear();

  // A full system upgrade takes no package name. Prefer the AUR helper when one
  // is present so repo and AUR packages refresh together; otherwise plain pacman
  // (which leaves AUR packages untouched).
  if (action == Action::Update) {
    if (!tools.aur_helper.empty()) {
      return tools.aur_helper + " -Syu";
    }
    if (!tools.has_sudo) {
      error = "sudo not found; cannot escalate to update packages";
      return "";
    }
    return "sudo pacman -Syu";
  }

  if (!IsSafePackageName(package_name)) {
    error = "refusing unusual package name: " + package_name;
    return "";
  }

  // flatpak manages its own privilege escalation (polkit), so no sudo wrapper;
  // it prompts in the restored terminal just like pacman does.
  if (manager == Manager::Flatpak) {
    if (!tools.has_flatpak) {
      error = "flatpak not found on PATH";
      return "";
    }
    return (action == Action::Install ? "flatpak install " : "flatpak uninstall ") + package_name;
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
    return "sudo pacman -S --needed " + package_name;
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
      error = "refusing unusual package name: " + name;
      return "";
    }
    arguments += " " + name;
  }

  if (manager == Manager::Flatpak) {
    if (!tools.has_flatpak) {
      error = "flatpak not found on PATH";
      return "";
    }
    return std::string(action == Action::Install ? "flatpak install" : "flatpak uninstall") +
           arguments;
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
    return "sudo pacman -S --needed" + arguments;
  }
  return PacmanRemove(arguments, tools, error);
}

int RunInTerminal(const std::string& command_line, const std::string& header) {
  std::cout << "\n\033[1;38;2;222;84;44m▶ " << header << "\033[0m\n"
            << "  \033[2m" << command_line << "\033[0m\n\n";
  std::cout.flush();

  const int status = std::system(command_line.c_str());

  std::cout << "\n\033[2m── done (exit " << status << ") · press Enter to return ──\033[0m";
  std::cout.flush();
  std::string discard;
  std::getline(std::cin, discard);
  return status;
}

}  // namespace pacseek::system
