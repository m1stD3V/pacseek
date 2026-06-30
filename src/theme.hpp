// theme.hpp — every PacSeek design token as a named constant.
// Pillar: no magic numbers. Nothing downstream should hardcode a hex, a pixel,
// or a threshold; it should reference a name from here.
#pragma once

#include <cstdint>

#include <ftxui/screen/color.hpp>

namespace pacseek::theme {

using ftxui::Color;

// --- Palette (hex values straight from the design handoff) ------------------
namespace color {
// Surfaces
inline const Color BgVoid = Color::RGB(0x0a, 0x0a, 0x0b);   // behind window
inline const Color Window = Color::RGB(0x0e, 0x0e, 0x10);   // app body
inline const Color Sidebar = Color::RGB(0x0b, 0x0b, 0x0d);  // sidebar / inset / footer
inline const Color Line = Color::RGB(0x05, 0x05, 0x06);     // borders, seams
inline const Color RowDivider = Color::RGB(0x0b, 0x0b, 0x0d);

// Brand accent (Braun orange)
inline const Color Accent = Color::RGB(0xde, 0x54, 0x2c);
inline const Color AccentBright = Color::RGB(0xe8, 0x64, 0x3a);  // gradient light end
inline const Color AccentDeep = Color::RGB(0xcf, 0x47, 0x1d);    // gradient dark end

// Text ramp
inline const Color Text = Color::RGB(0xe9, 0xe7, 0xe2);       // primary
inline const Color TextDim = Color::RGB(0x9a, 0x9a, 0x95);    // descriptions
inline const Color TextFaint = Color::RGB(0x6a, 0x6a, 0x66);  // mono values
inline const Color Label = Color::RGB(0x5d, 0x5d, 0x5a);      // section labels
inline const Color TextMuted = Color::RGB(0xb8, 0xb6, 0xb0);  // repo names / remove btn

// Repo identity colors (badges, legend dots, stacked bar)
inline const Color RepoCore = Color::RGB(0xde, 0x54, 0x2c);      // orange
inline const Color RepoExtra = Color::RGB(0x8a, 0x8f, 0x9a);     // grey
inline const Color RepoAur = Color::RGB(0x7f, 0xae, 0x8b);       // sage
inline const Color RepoMultilib = Color::RGB(0xe0, 0xb3, 0x41);  // amber

// Status
inline const Color Update = Color::RGB(0xe0, 0xb3, 0x41);  // amber update badge/count
inline const Color Ok = Color::RGB(0x7f, 0xae, 0x8b);      // sage "SYNCED" dot

// Storage bar — non-heavy fill (the grey gradient flattened to one tone)
inline const Color BarNormal = Color::RGB(0x8a, 0x8f, 0x9a);

// Disabled / inert chrome
inline const Color ChromeDim = Color::RGB(0x3a, 0x3a, 0x3d);
inline const Color CloseTint = Color::RGB(0x5a, 0x2a, 0x24);

// Seams between regions. The design uses near-black borders; on a terminal we
// lift them a touch so the structure stays legible.
inline const Color Seam = Color::RGB(0x1c, 0x1c, 0x20);

// Empty storage-bar track and active-nav background tint.
inline const Color BarTrack = Color::RGB(0x1a, 0x1a, 0x1d);
inline const Color NavActiveBg = Color::RGB(0x24, 0x16, 0x12);
inline const Color UpdateBadgeBg = Color::RGB(0x2a, 0x22, 0x10);
inline const Color RemoveBg = Color::RGB(0x16, 0x16, 0x1a);
inline const Color RowSelectedBg = Color::RGB(0x15, 0x15, 0x18);
}  // namespace color

// --- Sizing thresholds (data-driven styling rules) --------------------------
namespace size {
// A package is "heavy" at or above this installed size; drives orange size text
// and the orange storage bar.
inline constexpr int64_t kHeavyThresholdBytes = static_cast<int64_t>(300) * 1024 * 1024;
// Below this, format as MiB; at/above, format as GiB with two decimals.
inline constexpr int64_t kGibCutoffBytes = static_cast<int64_t>(1024) * 1024 * 1024;
// At/above this, format as TiB — used for drive-capacity scale values.
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
