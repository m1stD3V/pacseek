#include "ui/components.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <ftxui/screen/terminal.hpp>

#include "theme.hpp"

namespace pacseek::ui {

using namespace ftxui;
namespace palette = theme::color;
namespace layout = theme::layout;
using model::Package;
using model::Repo;
using model::View;

namespace {

// --- Small leaf builders ----------------------------------------------------

// A run of background-colored blank cells — the primitive for solid bars.
Element SolidBlock(int cells, Color background) {
  if (cells <= 0) {
    return text("");
  }
  return text(std::string(static_cast<size_t>(cells), ' ')) | bgcolor(background);
}

// A pill-style badge: dark text on a solid color, the design's repo badge.
Element Badge(const std::string& label, Color background, Color foreground) {
  return text(" " + label + " ") | color(foreground) | bgcolor(background) | bold;
}

Element RepoBadge(Repo repo) {
  return Badge(model::RepoBadgeLabel(repo), model::RepoColor(repo), palette::BgVoid);
}

// Right-aligns a single element inside a fixed-width column.
Element RightCell(Element element, int width) {
  return hbox({filler(), std::move(element)}) | size(WIDTH, EQUAL, width);
}

// --- Storage impact bar -----------------------------------------------------

int PercentOfMax(int64_t install_size_bytes, int64_t max_size_bytes) {
  const double ratio = static_cast<double>(install_size_bytes) / static_cast<double>(max_size_bytes);
  const int percent = static_cast<int>(std::lround(ratio * 100.0));
  return std::clamp(percent, 0, 100);
}

Element StorageBar(int percent, bool heavy) {
  const int width = layout::kStorageBarWidth;
  const int filled = std::clamp(static_cast<int>(std::lround(percent / 100.0 * width)), 0, width);
  const Color fill = heavy ? palette::Accent : palette::BarNormal;
  return hbox({SolidBlock(filled, fill), SolidBlock(width - filled, palette::BarTrack)});
}

// --- Action button ----------------------------------------------------------

Element ActionButton(bool installed) {
  if (installed) {
    return text(" REMOVE ") | color(palette::TextMuted) | bgcolor(palette::RemoveBg) | bold;
  }
  return text(" INSTALL ") | color(palette::BgVoid) | bgcolor(palette::Accent) | bold;
}

// --- Sidebar pieces ---------------------------------------------------------

struct NavEntry {
  View view;
  const char* icon;
  const char* label;
};

const std::vector<NavEntry> kNavEntries = {
    {View::Browse, "◈", "Browse"},
    {View::Installed, "▣", "Installed"},
    {View::Updates, "↑", "Updates"},
    {View::Aur, "✦", "AUR"},
};

Element NavRow(const NavEntry& entry, int count, bool active, bool highlight_count) {
  const Color accent_or_blank = active ? palette::Accent : palette::Sidebar;
  const Color label_color = active ? palette::Text : palette::TextDim;
  const Color count_color = highlight_count ? palette::Update : palette::TextFaint;

  Element row = hbox({
      SolidBlock(1, accent_or_blank),  // left accent bar when active
      text(" "),
      text(entry.icon) | color(label_color),
      text(" "),
      text(entry.label) | color(label_color) | flex,
      text(std::to_string(count)) | color(count_color),
      text("  "),
  });
  if (active) {
    row = row | bgcolor(palette::NavActiveBg);
  }
  return row;
}

Element LegendRow(Repo repo, int installed_count) {
  return hbox({
      text("  "),
      text("█") | color(model::RepoColor(repo)),
      text(" "),
      text(model::RepoBadgeLabel(repo)) | color(palette::TextMuted) | flex,
      text(std::to_string(installed_count)) | color(palette::TextFaint),
      text("  "),
  });
}

// Splits a byte count into the big number and its unit, for the footprint card.
std::pair<std::string, std::string> SplitFootprintTotal(int64_t total_bytes) {
  const std::string formatted = model::FormatSize(total_bytes);
  const size_t space = formatted.find(' ');
  if (space == std::string::npos) {
    return {formatted, ""};
  }
  return {formatted.substr(0, space), formatted.substr(space + 1)};
}

// Horizontal inset for sidebar cards: two leading cells of padding plus two
// trailing, matching the "  " prefix the footprint rows are drawn with.
constexpr int kSidebarContentInset = 4;

Element FootprintStackBar(const model::Catalog& catalog) {
  const int bar_width = layout::kSidebarWidth - kSidebarContentInset;
  const int64_t total = catalog.TotalInstalledBytes();
  if (total <= 0) {
    return SolidBlock(bar_width, palette::BarTrack);
  }

  Elements segments;
  int used = 0;
  for (const model::RepoFootprint& footprint : catalog.InstalledFootprintByRepo()) {
    const double ratio = static_cast<double>(footprint.install_size_bytes) / static_cast<double>(total);
    int cells = static_cast<int>(std::lround(ratio * bar_width));
    cells = std::clamp(cells, 0, bar_width - used);
    segments.push_back(SolidBlock(cells, model::RepoColor(footprint.repo)));
    used += cells;
  }
  if (used < bar_width) {
    segments.push_back(SolidBlock(bar_width - used, palette::BarTrack));
  }
  return hbox(std::move(segments));
}

// Formats the installed footprint as a share of total drive capacity, e.g.
// "0.2% of disk". Falls back to a package count when capacity is unknown.
std::string FootprintScaleNote(const model::Catalog& catalog, int64_t disk_total_bytes) {
  const std::string packages = std::to_string(catalog.InstalledCount()) + " packages";
  if (disk_total_bytes <= 0) {
    return packages + " installed";
  }
  const double percent =
      100.0 * static_cast<double>(catalog.TotalInstalledBytes()) / static_cast<double>(disk_total_bytes);
  char note[24];
  std::snprintf(note, sizeof(note), "%.1f%% of disk", percent);
  return packages + " · " + note;
}

// Compact, fixed-height card pinned to the foot of the sidebar. Kept tight so it
// survives short terminals where the nav/legend above it must clip instead. The
// headline reads installed-size / whole-drive capacity, and the bar below breaks
// the installed footprint down by repository color.
Element FootprintCard(const model::Catalog& catalog, int64_t disk_total_bytes) {
  const auto [number, unit] = SplitFootprintTotal(catalog.TotalInstalledBytes());

  Elements headline = {text("  "), text(number) | bold | color(palette::Text), text(" "),
                       text(unit) | color(palette::TextFaint)};
  if (disk_total_bytes > 0) {
    headline.push_back(text(" / " + model::FormatSize(disk_total_bytes)) | color(palette::TextFaint));
  }

  return vbox({
             text(""),
             text("  DISK FOOTPRINT") | color(palette::Label),
             hbox(std::move(headline)),
             hbox({text("  "), FootprintStackBar(catalog)}),
             text("  " + FootprintScaleNote(catalog, disk_total_bytes)) | color(palette::TextFaint),
         }) |
         bgcolor(palette::Window);
}

Element Sidebar(const app::AppState& state, const model::Catalog& catalog) {
  Elements top;
  top.push_back(text(""));
  top.push_back(text("  LIBRARY") | color(palette::Label));
  top.push_back(text(""));

  for (const NavEntry& entry : kNavEntries) {
    const int count = catalog.CountForView(entry.view);
    const bool active = state.view == entry.view;
    const bool highlight = entry.view == View::Updates && count > 0;
    top.push_back(NavRow(entry, count, active, highlight));
  }

  top.push_back(text(""));
  top.push_back(text("  REPOSITORIES") | color(palette::Label));
  top.push_back(text(""));
  for (Repo repo : {Repo::Core, Repo::Extra, Repo::Aur, Repo::Multilib}) {
    top.push_back(LegendRow(repo, catalog.InstalledCountForRepo(repo)));
  }

  // The nav/legend block flexes to push the footprint card to the foot of the
  // sidebar (and clips its lowest rows on a very short terminal); the card and
  // its rule stay pinned at the bottom. This relies on the package list being
  // virtualized so the body height is bounded to the screen — see PackageList.
  return vbox({
             vbox(std::move(top)) | flex,
             separator() | color(palette::Seam),
             FootprintCard(catalog, state.disk_total_bytes),
         }) |
         bgcolor(palette::Sidebar);
}

// --- Title bar --------------------------------------------------------------

Element TitleBar() {
  return hbox({
      text(" ◆ PACSEEK ") | bold | color(palette::BgVoid) | bgcolor(palette::Accent),
      text("  PACKAGE MANAGER · PACMAN + AUR") | color(palette::TextFaint) | flex,
      text("▢ ▢ ") | color(palette::ChromeDim),
      text("▣ ") | color(palette::CloseTint),
  });
}

// --- Search + controls ------------------------------------------------------

Element SearchBar(const app::AppState& state, int result_count) {
  Element query_field;
  if (state.query.empty() && !state.search_focused) {
    query_field = text("Search pacman + AUR…") | color(palette::Label) | flex;
  } else {
    Elements parts = {text(state.query) | color(palette::Text)};
    if (state.search_focused) {
      parts.push_back(text("█") | color(palette::Accent) | blink);
    }
    parts.push_back(filler());
    query_field = hbox(std::move(parts)) | flex;
  }

  const std::string sort_label =
      state.sort == model::Sort::SizeDescending ? "SIZE ↓" : "NAME ↓";

  return hbox({
             text(" ❯ ") | color(palette::Accent),
             query_field,
             text(std::to_string(result_count) + " RESULTS  ") | color(palette::Label),
             text(" SORT ") | color(palette::TextFaint) | bgcolor(palette::Sidebar),
             text(sort_label + " ") | color(palette::TextMuted) | bgcolor(palette::Sidebar),
         }) |
         bgcolor(palette::Sidebar);
}

Element ColumnHeader() {
  return hbox({
      text(" "),  // selection gutter
      text(" PACKAGE") | color(palette::Label) | flex,
      text("REPO") | color(palette::Label) | size(WIDTH, EQUAL, layout::kRepoColumnWidth),
      text("STORAGE IMPACT") | color(palette::Label) | size(WIDTH, EQUAL, layout::kStorageColumnWidth),
      RightCell(text("SIZE") | color(palette::Label), layout::kSizeColumnWidth),
      RightCell(text("ACTION ") | color(palette::Label), layout::kActionColumnWidth),
  });
}

// --- Package row ------------------------------------------------------------

Element UpdateBadge(bool has_update) {
  if (!has_update) {
    return text("");
  }
  return text(" UPDATE ") | color(palette::Update) | bgcolor(palette::UpdateBadgeBg);
}

Element PackageNameColumn(const Package& package) {
  Element title_line = hbox({
      text(package.name) | bold | color(palette::Text),
      text("  "),
      text(package.version) | color(palette::Label),
      text(" "),
      UpdateBadge(package.has_update),
  });
  Element description_line = text(package.description) | color(palette::TextDim);
  return vbox({title_line, description_line}) | flex;
}

Element StorageColumn(const Package& package, int64_t max_size_bytes) {
  const int percent = PercentOfMax(package.install_size_bytes, max_size_bytes);
  Element caption = hbox({
      text("DL " + model::FormatSize(package.download_size_bytes)) | color(palette::Label),
      filler(),
      text(std::to_string(percent) + "% OF MAX") | color(palette::Label),
  });
  return vbox({StorageBar(percent, model::IsHeavy(package)), caption}) |
         size(WIDTH, EQUAL, layout::kStorageColumnWidth);
}

Element SizeColumn(const Package& package) {
  const Color size_color = model::IsHeavy(package) ? palette::Accent : palette::Text;
  Element value = text(model::FormatSize(package.install_size_bytes)) | color(size_color) | bold;
  return vbox({RightCell(std::move(value), layout::kSizeColumnWidth), text("")});
}

Element ActionColumn(const Package& package) {
  return vbox({RightCell(ActionButton(package.installed), layout::kActionColumnWidth), text("")});
}

Element PackageRow(const Package& package, int64_t max_size_bytes, bool selected) {
  Element gutter = SolidBlock(1, selected ? palette::Accent : palette::Window);
  Element row = hbox({
      gutter,
      PackageNameColumn(package),
      vbox({RepoBadge(package.repo), text("")}) | size(WIDTH, EQUAL, layout::kRepoColumnWidth),
      StorageColumn(package, max_size_bytes),
      SizeColumn(package),
      ActionColumn(package),
  });
  if (selected) {
    row = row | bgcolor(palette::RowSelectedBg);
  }
  return row;
}

Element EmptyState(const std::string& query) {
  return vbox({
             filler(),
             text("⊘") | color(palette::ChromeDim) | hcenter,
             text(""),
             text("NO PACKAGES MATCH \"" + query + "\"") | color(palette::Label) | hcenter,
             filler(),
         }) |
         flex;
}

// Number of rows each package occupies: a two-line cell plus its divider.
constexpr int kRowsPerPackage = 3;
// Fixed chrome above and below the list (title, search, column header, footer,
// and their separator rules) that does not scroll with the packages.
constexpr int kListChromeRows = 8;

Element PackageList(const app::AppState& state, const std::vector<const Package*>& visible,
                    int64_t max_size_bytes) {
  if (visible.empty()) {
    return EmptyState(state.query);
  }

  // Virtualize: render only the window of packages that fits the terminal. A full
  // catalog is 15k+ packages; building an element for each makes the list taller
  // than the screen, which defeats FTXUI's height clamping and pushes the footer
  // and sidebar off-screen. Windowing keeps the list's height bounded (and is far
  // cheaper to lay out each frame).
  const int count = static_cast<int>(visible.size());
  const int viewport_rows = std::max(1, ftxui::Terminal::Size().dimy - kListChromeRows);
  const int capacity = std::max(1, viewport_rows / kRowsPerPackage);

  // Center the window on the selection, clamped to the ends of the list.
  int start = std::clamp(state.selected_index - capacity / 2, 0, std::max(0, count - capacity));
  const int end = std::min(count, start + capacity);

  Elements rows;
  for (int index = start; index < end; ++index) {
    const bool selected = index == state.selected_index;
    rows.push_back(PackageRow(*visible[index], max_size_bytes, selected));
    rows.push_back(separator() | color(palette::RowDivider));
  }
  return vbox(std::move(rows)) | flex;
}

// --- Footer -----------------------------------------------------------------

Element Footer(const app::AppState& state, const model::Catalog& catalog) {
  const int updates = catalog.CountForView(View::Updates);
  const std::string updates_line =
      updates > 0 ? std::to_string(updates) + " UPDATES AVAILABLE" : "SYSTEM UP TO DATE";

  Element center;
  if (!state.status_message.empty()) {
    center = text(state.status_message) | color(palette::Update) | bold;
  } else {
    center = text("1-4 view · / search · j/k move · s sort · q quit") |
             color(palette::Label);
  }

  const std::string source_badge = state.source_name + (state.read_only ? " · READ-ONLY " : " ");

  return hbox({
             text(" ●") | color(palette::Ok),
             text(" SYNCED") | color(palette::TextFaint),
             text("   "),
             text(updates_line) | color(updates > 0 ? palette::Update : palette::TextFaint),
             filler(),
             center,
             filler(),
             text(source_badge) | color(palette::TextFaint),
         }) |
         bgcolor(palette::Sidebar);
}

}  // namespace

Element RenderApp(const app::AppState& state, const model::Catalog& catalog,
                  const std::vector<const Package*>& visible) {
  const int64_t max_size_bytes = catalog.MaxInstallSizeBytes();

  Element main_column = vbox({
                            SearchBar(state, static_cast<int>(visible.size())),
                            separator() | color(palette::Seam),
                            ColumnHeader(),
                            separator() | color(palette::Seam),
                            PackageList(state, visible, max_size_bytes),
                            separator() | color(palette::Seam),
                            Footer(state, catalog),
                        }) |
                        flex;

  Element body = hbox({
                     Sidebar(state, catalog) | size(WIDTH, EQUAL, layout::kSidebarWidth),
                     separator() | color(palette::Seam),
                     main_column,
                 }) |
                 flex;

  return vbox({
             TitleBar(),
             separator() | color(palette::Seam),
             body,
         }) |
         bgcolor(palette::Window);
}

}  // namespace pacseek::ui
