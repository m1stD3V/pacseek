#include "data/mock_source.hpp"

#include "model/package.hpp"

namespace pacseek::data {

namespace {

// One row of the prototype dataset, sizes expressed in MiB exactly as the
// design file lists them. Kept as a compact table so it reads like the source.
struct MockRow {
  const char* name;
  const char* repo;
  const char* version;
  const char* description;
  int install_size_mib;
  int download_size_mib;
  bool installed;
  bool has_update;
};

constexpr MockRow kPrototypePackages[] = {
    {"firefox", "extra", "141.0-1", "Standalone web browser from Mozilla", 248, 78, true, false},
    {"linux", "core", "6.9.7-1", "The Linux kernel and modules", 142, 48, true, true},
    {"hyprland", "extra", "0.41.2-1", "Dynamic tiling Wayland compositor", 38, 9, true, false},
    {"waybar", "extra", "0.10.4-1", "Customizable Wayland bar for Sway and Wlroots", 12, 3, true, false},
    {"visual-studio-code-bin", "aur", "1.90.2-1", "Microsoft code editor (binary release)", 380, 110, true, false},
    {"spotify", "aur", "1.2.40-1", "A proprietary music streaming service", 310, 96, true, true},
    {"discord", "extra", "0.0.60-1", "All-in-one voice and text chat", 195, 72, true, false},
    {"gimp", "extra", "2.10.38-1", "GNU Image Manipulation Program", 240, 88, true, false},
    {"neovim", "extra", "0.10.0-2", "Hyperextensible Vim-based text editor", 32, 11, true, false},
    {"btop", "extra", "1.4.0-1", "A monitor of system resources", 2, 1, true, false},
    {"wezterm", "extra", "20240203-1", "GPU-accelerated terminal emulator", 78, 24, true, false},
    {"fish", "extra", "3.7.1-1", "Smart and user-friendly command line shell", 9, 3, true, false},
    {"rustup", "extra", "1.27.1-1", "The Rust toolchain installer", 680, 210, true, false},
    {"obsidian", "aur", "1.6.3-1", "Knowledge base on local Markdown files", 290, 92, true, false},
    {"nodejs", "extra", "22.3.0-1", "Evented I/O for V8 javascript", 78, 22, true, false},
    {"python", "core", "3.12.4-1", "Next generation of the python language", 64, 18, true, false},
    {"ffmpeg", "extra", "6.1.1-3", "Record, convert and stream audio and video", 42, 14, true, true},
    {"docker", "extra", "26.1.4-1", "Pack, ship and run any application", 420, 130, false, false},
    {"blender", "extra", "4.1.1-3", "A fully integrated 3D graphics suite", 520, 180, false, false},
    {"steam", "multilib", "1.0.0.79-1", "Valve's digital software delivery system", 380, 120, false, false},
    {"godot", "extra", "4.2.2-1", "Advanced cross-platform game engine", 110, 40, false, false},
    {"krita", "extra", "5.2.2-1", "Edit and paint images, digital painting", 290, 96, false, false},
};

int64_t MibToBytes(int mib) {
  return static_cast<int64_t>(mib) * 1024 * 1024;
}

}  // namespace

std::vector<model::Package> MockSource::LoadPackages() {
  std::vector<model::Package> packages;
  for (const MockRow& row : kPrototypePackages) {
    model::Package package;
    package.name = row.name;
    package.version = row.version;
    package.description = row.description;
    package.repo = model::RepoFromName(row.repo);
    package.install_size_bytes = MibToBytes(row.install_size_mib);
    package.download_size_bytes = MibToBytes(row.download_size_mib);
    package.installed = row.installed;
    package.has_update = row.has_update;
    packages.push_back(std::move(package));
  }
  return packages;
}

}  // namespace pacseek::data
