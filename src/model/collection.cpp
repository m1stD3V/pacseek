#include "model/collection.hpp"

#include <utility>

namespace pacseek::model {

namespace {

// The merged, program-lifetime set: built-ins first, then user-defined entries.
// Seeded from the built-ins so Collections() is valid before SetUserCollections.
std::vector<Collection>& ActiveStore() {
  static std::vector<Collection> store = BuiltinCollections();
  return store;
}

}  // namespace

const std::vector<Collection>& BuiltinCollections() {
  // Members are official-repo package names wherever possible so they resolve
  // against the local sync databases without touching the network; a handful of
  // well-known AUR names are included where there is no repo equivalent, and
  // simply show as "unavailable" until the user installs them via the AUR view.
  static const std::vector<Collection> kCollections = {
      {"gaming", "Gaming", "◆",
       "Launchers, compatibility layers, and overlays for play",
       {"steam", "lutris", "wine", "wine-gecko", "wine-mono", "gamemode",
        "lib32-gamemode", "mangohud", "lib32-mangohud", "goverlay", "gamescope",
        "godot", "discord"}},

      {"creative", "Creative Work", "✦",
       "Image, video, audio, and 3D content creation",
       {"gimp", "krita", "inkscape", "blender", "darktable", "kdenlive",
        "audacity", "scribus", "mypaint", "obs-studio"}},

      {"development", "Development", "❯",
       "Editors, toolchains, and version control for coding",
       {"git", "neovim", "vim", "docker", "docker-compose", "nodejs", "npm",
        "python", "rustup", "go", "gcc", "clang", "gdb", "cmake", "lazygit",
        "github-cli", "jq", "tmux"}},

      {"multimedia", "Multimedia", "♪",
       "Players, transcoders, and downloaders for media",
       {"vlc", "mpv", "ffmpeg", "yt-dlp", "audacious", "handbrake",
        "easyeffects"}},

      {"terminal", "System & Terminal", "▣",
       "Modern command-line tools and terminal emulators",
       {"htop", "btop", "fastfetch", "ripgrep", "fd", "bat", "eza", "fzf",
        "zoxide", "starship", "alacritty", "kitty", "tmux", "fish"}},
  };
  return kCollections;
}

const std::vector<Collection>& Collections() { return ActiveStore(); }

void SetUserCollections(std::vector<Collection> user) {
  std::vector<Collection>& store = ActiveStore();
  store = BuiltinCollections();  // reset to a clean copy of the built-ins
  store.reserve(store.size() + user.size());
  for (Collection& collection : user) {
    store.push_back(std::move(collection));
  }
}

}  // namespace pacseek::model
