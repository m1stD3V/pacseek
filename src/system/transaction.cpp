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
  for (const char* helper : {"paru", "yay"}) {
    if (OnPath(helper)) {
      tools.aur_helper = helper;
      break;
    }
  }
  return tools;
}

std::string BuildCommandLine(Action action, const std::string& package_name, bool is_aur,
                             const Tools& tools, std::string& error) {
  error.clear();
  if (!IsSafePackageName(package_name)) {
    error = "refusing unusual package name: " + package_name;
    return "";
  }

  // Installing an AUR package goes through the helper, which must run as the
  // normal user (it escalates internally to build and install).
  if (action == Action::Install && is_aur) {
    if (tools.aur_helper.empty()) {
      error = "install an AUR helper (paru or yay) to build " + package_name;
      return "";
    }
    return tools.aur_helper + " -S " + package_name;
  }

  // Everything else is a pacman operation and needs root.
  if (!tools.has_sudo) {
    error = "sudo not found; cannot escalate to modify packages";
    return "";
  }
  if (action == Action::Install) {
    return "sudo pacman -S --needed " + package_name;
  }
  // -Rs also removes dependencies that no other installed package still needs.
  return "sudo pacman -Rs " + package_name;
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
