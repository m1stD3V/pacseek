// theme.hpp - every PacSeek design token as a named constant.
// Pillar: no magic numbers. Nothing downstream should hardcode a hex, a pixel,
// or a threshold; it should reference a name from here.
#pragma once

#include <cstdint>
#include <string>

#include <ftxui/screen/color.hpp>

namespace pacseek::theme {

using ftxui::Color;

// --- Palette ----------------------------------------------------------------
// Every themeable color in one struct. Field names match the historical color::
// tokens so the rest of the codebase keeps referencing color::Accent etc.
// unchanged; SetTheme swaps the active palette at startup and the references
// below read it live.
struct Palette {
  // Surfaces
  Color BgVoid, Window, Sidebar, Line, RowDivider;
  // Brand accent (and its gradient ends)
  Color Accent, AccentBright, AccentDeep;
  // Text ramp: primary, descriptions, mono values, section labels, repo names
  Color Text, TextDim, TextFaint, Label, TextMuted;
  // Repo identity colors (badges, legend dots, stacked bar)
  Color RepoCore, RepoExtra, RepoAur, RepoMultilib, RepoFlatpak;
  // Status
  Color Update, Ok;
  // Storage bar non-heavy fill
  Color BarNormal;
  // Inert chrome
  Color ChromeDim, CloseTint;
  // Seams between regions
  Color Seam;
  // Background tints
  Color BarTrack, NavActiveBg, UpdateBadgeBg, RemoveBg, RowSelectedBg;
};

namespace palettes {

// The original Braun-inspired design (default). Hex values from the handoff.
inline const Palette kBrutalist = {
    Color::RGB(0x0a, 0x0a, 0x0b), Color::RGB(0x0e, 0x0e, 0x10), Color::RGB(0x0b, 0x0b, 0x0d),
    Color::RGB(0x05, 0x05, 0x06), Color::RGB(0x0b, 0x0b, 0x0d),
    Color::RGB(0xde, 0x54, 0x2c), Color::RGB(0xe8, 0x64, 0x3a), Color::RGB(0xcf, 0x47, 0x1d),
    Color::RGB(0xe9, 0xe7, 0xe2), Color::RGB(0x9a, 0x9a, 0x95), Color::RGB(0x6a, 0x6a, 0x66),
    Color::RGB(0x5d, 0x5d, 0x5a), Color::RGB(0xb8, 0xb6, 0xb0),
    Color::RGB(0xde, 0x54, 0x2c), Color::RGB(0x8a, 0x8f, 0x9a), Color::RGB(0x7f, 0xae, 0x8b),
    Color::RGB(0xe0, 0xb3, 0x41), Color::RGB(0x5b, 0x9b, 0xd5),
    Color::RGB(0xe0, 0xb3, 0x41), Color::RGB(0x7f, 0xae, 0x8b),
    Color::RGB(0x8a, 0x8f, 0x9a),
    Color::RGB(0x3a, 0x3a, 0x3d), Color::RGB(0x5a, 0x2a, 0x24),
    Color::RGB(0x1c, 0x1c, 0x20),
    Color::RGB(0x1a, 0x1a, 0x1d), Color::RGB(0x24, 0x16, 0x12), Color::RGB(0x2a, 0x22, 0x10),
    Color::RGB(0x16, 0x16, 0x1a), Color::RGB(0x15, 0x15, 0x18),
};

inline const Palette kTokyoNight = {
    Color::RGB(0x16, 0x16, 0x1e), Color::RGB(0x1a, 0x1b, 0x26), Color::RGB(0x16, 0x16, 0x1e),
    Color::RGB(0x10, 0x10, 0x19), Color::RGB(0x1a, 0x1b, 0x26),
    Color::RGB(0x7a, 0xa2, 0xf7), Color::RGB(0x8d, 0xb0, 0xff), Color::RGB(0x6a, 0x92, 0xe7),
    Color::RGB(0xc0, 0xca, 0xf5), Color::RGB(0xa9, 0xb1, 0xd6), Color::RGB(0x56, 0x5f, 0x89),
    Color::RGB(0x56, 0x5f, 0x89), Color::RGB(0x9a, 0xa5, 0xce),
    Color::RGB(0xff, 0x9e, 0x64), Color::RGB(0x7d, 0xcf, 0xff), Color::RGB(0x9e, 0xce, 0x6a),
    Color::RGB(0xe0, 0xaf, 0x68), Color::RGB(0x7a, 0xa2, 0xf7),
    Color::RGB(0xe0, 0xaf, 0x68), Color::RGB(0x9e, 0xce, 0x6a),
    Color::RGB(0x56, 0x5f, 0x89),
    Color::RGB(0x41, 0x48, 0x68), Color::RGB(0x5a, 0x2a, 0x3a),
    Color::RGB(0x29, 0x2e, 0x42),
    Color::RGB(0x1f, 0x23, 0x35), Color::RGB(0x2a, 0x2e, 0x44), Color::RGB(0x2e, 0x2a, 0x18),
    Color::RGB(0x20, 0x20, 0x2e), Color::RGB(0x29, 0x2e, 0x42),
};

inline const Palette kCatppuccinMocha = {
    Color::RGB(0x18, 0x18, 0x25), Color::RGB(0x1e, 0x1e, 0x2e), Color::RGB(0x18, 0x18, 0x25),
    Color::RGB(0x11, 0x11, 0x1b), Color::RGB(0x1e, 0x1e, 0x2e),
    Color::RGB(0xcb, 0xa6, 0xf7), Color::RGB(0xd6, 0xb8, 0xff), Color::RGB(0xb4, 0x8e, 0xe7),
    Color::RGB(0xcd, 0xd6, 0xf4), Color::RGB(0xa6, 0xad, 0xc8), Color::RGB(0x6c, 0x70, 0x86),
    Color::RGB(0x6c, 0x70, 0x86), Color::RGB(0xba, 0xc2, 0xde),
    Color::RGB(0xf3, 0x8b, 0xa8), Color::RGB(0x89, 0xdc, 0xeb), Color::RGB(0xa6, 0xe3, 0xa1),
    Color::RGB(0xf9, 0xe2, 0xaf), Color::RGB(0x89, 0xb4, 0xfa),
    Color::RGB(0xf9, 0xe2, 0xaf), Color::RGB(0xa6, 0xe3, 0xa1),
    Color::RGB(0x6c, 0x70, 0x86),
    Color::RGB(0x45, 0x47, 0x5a), Color::RGB(0x5a, 0x2a, 0x3a),
    Color::RGB(0x31, 0x32, 0x44),
    Color::RGB(0x29, 0x2c, 0x3c), Color::RGB(0x31, 0x32, 0x44), Color::RGB(0x33, 0x30, 0x1c),
    Color::RGB(0x25, 0x23, 0x2f), Color::RGB(0x31, 0x32, 0x44),
};

inline const Palette kCatppuccinMacchiato = {
    Color::RGB(0x1e, 0x20, 0x30), Color::RGB(0x24, 0x27, 0x3a), Color::RGB(0x1e, 0x20, 0x30),
    Color::RGB(0x18, 0x19, 0x26), Color::RGB(0x24, 0x27, 0x3a),
    Color::RGB(0xc6, 0xa0, 0xf6), Color::RGB(0xd2, 0xb3, 0xff), Color::RGB(0xb0, 0x88, 0xe6),
    Color::RGB(0xca, 0xd3, 0xf5), Color::RGB(0xa5, 0xad, 0xcb), Color::RGB(0x6e, 0x73, 0x8d),
    Color::RGB(0x6e, 0x73, 0x8d), Color::RGB(0xb8, 0xc0, 0xe0),
    Color::RGB(0xed, 0x87, 0x96), Color::RGB(0x91, 0xd7, 0xe3), Color::RGB(0xa6, 0xda, 0x95),
    Color::RGB(0xee, 0xd4, 0x9f), Color::RGB(0x8a, 0xad, 0xf4),
    Color::RGB(0xee, 0xd4, 0x9f), Color::RGB(0xa6, 0xda, 0x95),
    Color::RGB(0x6e, 0x73, 0x8d),
    Color::RGB(0x49, 0x4d, 0x64), Color::RGB(0x5a, 0x2a, 0x3a),
    Color::RGB(0x36, 0x3a, 0x4f),
    Color::RGB(0x2a, 0x2d, 0x40), Color::RGB(0x36, 0x3a, 0x4f), Color::RGB(0x36, 0x32, 0x1f),
    Color::RGB(0x2a, 0x28, 0x38), Color::RGB(0x36, 0x3a, 0x4f),
};

inline const Palette kGruvboxDark = {
    Color::RGB(0x1d, 0x20, 0x21), Color::RGB(0x28, 0x28, 0x28), Color::RGB(0x1d, 0x20, 0x21),
    Color::RGB(0x14, 0x16, 0x17), Color::RGB(0x28, 0x28, 0x28),
    Color::RGB(0xfe, 0x80, 0x19), Color::RGB(0xff, 0x90, 0x30), Color::RGB(0xe0, 0x70, 0x0d),
    Color::RGB(0xeb, 0xdb, 0xb2), Color::RGB(0xbd, 0xae, 0x93), Color::RGB(0x92, 0x83, 0x74),
    Color::RGB(0x7c, 0x6f, 0x64), Color::RGB(0xd5, 0xc4, 0xa1),
    Color::RGB(0xfb, 0x49, 0x34), Color::RGB(0xa8, 0x99, 0x84), Color::RGB(0xb8, 0xbb, 0x26),
    Color::RGB(0xfa, 0xbd, 0x2f), Color::RGB(0x83, 0xa5, 0x98),
    Color::RGB(0xfa, 0xbd, 0x2f), Color::RGB(0xb8, 0xbb, 0x26),
    Color::RGB(0xa8, 0x99, 0x84),
    Color::RGB(0x50, 0x49, 0x45), Color::RGB(0x5a, 0x2a, 0x24),
    Color::RGB(0x3c, 0x38, 0x36),
    Color::RGB(0x32, 0x30, 0x2f), Color::RGB(0x3c, 0x38, 0x36), Color::RGB(0x3a, 0x34, 0x20),
    Color::RGB(0x2a, 0x28, 0x26), Color::RGB(0x3c, 0x38, 0x36),
};

}  // namespace palettes

// The active palette, swapped by SetTheme at startup. The color:: references
// below alias its members, so changing this re-colors the whole UI.
inline Palette active = palettes::kBrutalist;

// --- Color tokens -----------------------------------------------------------
// References into `active` so existing call sites (color(palette::Accent), …)
// read the selected theme without any change.
namespace color {
inline const Color& BgVoid = active.BgVoid;
inline const Color& Window = active.Window;
inline const Color& Sidebar = active.Sidebar;
inline const Color& Line = active.Line;
inline const Color& RowDivider = active.RowDivider;
inline const Color& Accent = active.Accent;
inline const Color& AccentBright = active.AccentBright;
inline const Color& AccentDeep = active.AccentDeep;
inline const Color& Text = active.Text;
inline const Color& TextDim = active.TextDim;
inline const Color& TextFaint = active.TextFaint;
inline const Color& Label = active.Label;
inline const Color& TextMuted = active.TextMuted;
inline const Color& RepoCore = active.RepoCore;
inline const Color& RepoExtra = active.RepoExtra;
inline const Color& RepoAur = active.RepoAur;
inline const Color& RepoMultilib = active.RepoMultilib;
inline const Color& RepoFlatpak = active.RepoFlatpak;
inline const Color& Update = active.Update;
inline const Color& Ok = active.Ok;
inline const Color& BarNormal = active.BarNormal;
inline const Color& ChromeDim = active.ChromeDim;
inline const Color& CloseTint = active.CloseTint;
inline const Color& Seam = active.Seam;
inline const Color& BarTrack = active.BarTrack;
inline const Color& NavActiveBg = active.NavActiveBg;
inline const Color& UpdateBadgeBg = active.UpdateBadgeBg;
inline const Color& RemoveBg = active.RemoveBg;
inline const Color& RowSelectedBg = active.RowSelectedBg;
}  // namespace color

// Selects a theme by name (case-insensitive; spaces/underscores treated as '-').
// Returns false and leaves the active theme unchanged if the name is unknown.
inline bool SetTheme(const std::string& name) {
  std::string key;
  for (char c : name) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
    if (c == ' ' || c == '_') {
      c = '-';
    }
    key += c;
  }

  if (key == "brutalist" || key == "default" || key == "pacseek") {
    active = palettes::kBrutalist;
  } else if (key == "tokyo-night" || key == "tokyonight") {
    active = palettes::kTokyoNight;
  } else if (key == "catppuccin-mocha" || key == "catppuccin" || key == "mocha") {
    active = palettes::kCatppuccinMocha;
  } else if (key == "catppuccin-macchiato" || key == "macchiato") {
    active = palettes::kCatppuccinMacchiato;
  } else if (key == "gruvbox" || key == "gruvbox-dark") {
    active = palettes::kGruvboxDark;
  } else {
    return false;
  }
  return true;
}

// --- Sizing thresholds (data-driven styling rules) --------------------------
namespace size {
// A package is "heavy" at or above this installed size; drives orange size text
// and the orange storage bar.
inline constexpr int64_t kHeavyThresholdBytes = static_cast<int64_t>(300) * 1024 * 1024;
// Below this, format as MiB; at/above, format as GiB with two decimals.
inline constexpr int64_t kGibCutoffBytes = static_cast<int64_t>(1024) * 1024 * 1024;
// At/above this, format as TiB - used for drive-capacity scale values.
inline constexpr int64_t kTibCutoffBytes = static_cast<int64_t>(1024) * 1024 * 1024 * 1024;
inline constexpr int64_t kBytesPerMiB = static_cast<int64_t>(1024) * 1024;
inline constexpr int64_t kBytesPerGiB = static_cast<int64_t>(1024) * 1024 * 1024;
inline constexpr int64_t kBytesPerTiB = static_cast<int64_t>(1024) * 1024 * 1024 * 1024;
}  // namespace size

// --- Layout dimensions (terminal cells, scaled down from the GUI mock) ------
namespace layout {
inline constexpr int kSidebarWidth = 30;        // sidebar column width
inline constexpr int kRepoColumnWidth = 12;     // REPO column
inline constexpr int kStorageColumnWidth = 26;  // STORAGE IMPACT column
inline constexpr int kSizeColumnWidth = 12;     // SIZE column (right aligned)
inline constexpr int kActionColumnWidth = 11;   // ACTION column (right aligned)
inline constexpr int kStorageBarWidth = 18;     // cells of bar track within the column
}  // namespace layout

}  // namespace pacseek::theme
