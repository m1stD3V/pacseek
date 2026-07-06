#include "model/collection.hpp"

#include <utility>

namespace pacseek::model {

namespace {

// The two mutable slices, each set once at startup. Kept separate so setting one
// never clobbers the other; ActiveStore is rebuilt from both plus the built-ins.
std::vector<Collection>& UserStore() {
  static std::vector<Collection> user;
  return user;
}
std::vector<Collection>& GroupStore() {
  static std::vector<Collection> groups;
  return groups;
}

// The merged, program-lifetime set: built-ins first, then user-defined entries,
// then package groups. Seeded from the built-ins so Collections() is valid
// before either setter runs.
std::vector<Collection>& ActiveStore() {
  static std::vector<Collection> store = BuiltinCollections();
  return store;
}

// Rebuilds ActiveStore from the built-ins plus the current user and group
// slices, in order. Reallocates the backing store, so it must run before any
// pointers into Collections() are handed out (i.e. at startup only).
void RebuildActiveStore() {
  const std::vector<Collection>& user = UserStore();
  const std::vector<Collection>& groups = GroupStore();
  std::vector<Collection>& store = ActiveStore();
  store = BuiltinCollections();  // reset to a clean copy of the built-ins
  store.reserve(store.size() + user.size() + groups.size());
  store.insert(store.end(), user.begin(), user.end());
  store.insert(store.end(), groups.begin(), groups.end());
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
  UserStore() = std::move(user);
  RebuildActiveStore();
}

void SetGroupCollections(std::vector<Collection> groups) {
  GroupStore() = std::move(groups);
  RebuildActiveStore();
}

}  // namespace pacseek::model
