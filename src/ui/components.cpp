#include "ui/components.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>

#include "config/keybindings.hpp"
#include "model/collection.hpp"
#include "theme.hpp"

namespace pacseek::ui {

using namespace ftxui;
namespace palette = theme::color;
namespace layout = theme::layout;
using model::Package;
using model::Repo;
using model::View;

namespace {

// The collection the user has drilled into, or nullptr while the picker (or any
// other view) is showing. Shared by the search bar, footer, and main column.
const model::Collection* ActiveCollection(const app::AppState& state) {
  if (state.view != View::Collections || state.active_collection < 0) {
    return nullptr;
  }
  const std::vector<model::Collection>& collections = model::Collections();
  if (state.active_collection >= static_cast<int>(collections.size())) {
    return nullptr;
  }
  return &collections[state.active_collection];
}

// --- Small leaf builders ----------------------------------------------------

// A run of background-colored blank cells - the primitive for solid bars.
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

// Truncates to `cells` display columns with a trailing ellipsis, walking UTF-8
// sequences so a multibyte glyph is never split. Codepoints count as one column
// each - right for the ASCII-and-punctuation strings this UI feeds it. Used
// wherever FTXUI would otherwise clip text mid-word (package rows, footer
// status): a deliberate "…" reads as design, a hard cut reads as a bug.
// Display columns a string occupies, counting UTF-8 codepoints as one cell each.
int CellCount(const std::string& value) {
  int count = 0;
  for (size_t i = 0; i < value.size(); ++count) {
    const unsigned char lead = static_cast<unsigned char>(value[i]);
    i += lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
  }
  return count;
}

std::string TruncateWithEllipsis(const std::string& value, int cells) {
  if (cells <= 0) {
    return "";
  }
  std::vector<size_t> starts;  // byte offset of each codepoint
  size_t i = 0;
  while (i < value.size()) {
    starts.push_back(i);
    const unsigned char lead = static_cast<unsigned char>(value[i]);
    i += lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
  }
  if (static_cast<int>(starts.size()) <= cells) {
    return value;
  }
  if (cells == 1) {
    return "…";
  }
  return value.substr(0, starts[static_cast<size_t>(cells) - 1]) + "…";
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
  const char* label;
};

const std::vector<NavEntry> kNavEntries = {
    {View::Browse, "Browse"},
    {View::Installed, "Installed"},
    {View::Updates, "Updates"},
    {View::Orphans, "Orphans"},
    {View::Collections, "Collections"},
};

// The nav icon for a view, read from the live glyph set so ASCII mode swaps them
// too. Kept out of kNavEntries (a static built before SetGlyphs runs) so the
// tokens are resolved at render time, not at static-init time.
const char* NavIcon(View view) {
  switch (view) {
    case View::Browse:
      return theme::glyph::nav_browse;
    case View::Installed:
      return theme::glyph::nav_installed;
    case View::Updates:
      return theme::glyph::nav_updates;
    case View::Collections:
      return theme::glyph::nav_collections;
    case View::Orphans:
      return theme::glyph::nav_orphans;
  }
  return theme::glyph::bullet;
}

// `index` is 0-based; the row is tagged with its 1-based [NN] hotkey label so the
// number-row shortcut for each view is visible at a glance.
Element NavRow(const NavEntry& entry, int index, int count, bool active, bool highlight_count) {
  const Color accent_or_blank = active ? palette::Accent : palette::Sidebar;
  const Color label_color = active ? palette::Text : palette::TextDim;
  const Color count_color = highlight_count ? palette::Update : palette::TextFaint;

  char tab_label[8];
  std::snprintf(tab_label, sizeof(tab_label), "[%02d]", index + 1);

  Element row = hbox({
      SolidBlock(1, accent_or_blank),  // left accent bar when active
      text(" "),
      text(tab_label) | color(active ? palette::Accent : palette::TextFaint) | bold,
      text(" "),
      text(NavIcon(entry.view)) | color(label_color),
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

// A SOURCES row: the second nav axis. Interactive like a VIEWS row - it carries
// the active accent bar and highlight, its identity color, and its installed
// count - so the selected source reads the same way the selected view does.
Element SourceRow(model::Source source, int installed_count, bool active) {
  const Color bar = active ? palette::Accent : palette::Sidebar;
  const Color label_color = active ? palette::Text : palette::TextMuted;
  const Color count_color = active ? palette::TextDim : palette::TextFaint;
  Element row = hbox({
      SolidBlock(1, bar),  // left accent bar when active, mirroring NavRow
      text(" "),
      text("█") | color(model::SourceColor(source)),
      text(" "),
      text(model::SourceLabel(source)) | color(label_color) | flex,
      text(std::to_string(installed_count)) | color(count_color),
      text("  "),
  });
  if (active) {
    row = row | bgcolor(palette::NavActiveBg);
  }
  return row;
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
// "0.2% of disk". Falls back to a package count when capacity is unknown. Kept
// terse ("pkgs") so it fits inside the sidebar width even with a five-figure
// package count - see kSidebarWidth and the width guard in FootprintCard.
std::string FootprintScaleNote(const model::Catalog& catalog, int64_t disk_total_bytes) {
  const std::string packages = std::to_string(catalog.InstalledCount()) + " pkgs";
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
Element FootprintCard(const model::Catalog& catalog, const app::AppState& state) {
  const int64_t disk_total_bytes = state.disk_total_bytes;
  const auto [number, unit] = SplitFootprintTotal(catalog.TotalInstalledBytes());

  Elements headline = {text("  "), text(number) | bold | color(palette::Text), text(" "),
                       text(unit) | color(palette::TextFaint)};
  if (disk_total_bytes > 0) {
    headline.push_back(text(" / " + model::FormatSize(disk_total_bytes)) | color(palette::TextFaint));
  }

  // Every card line is capped to the sidebar width (less a one-cell right margin)
  // so a long value clips inside the sidebar instead of running into the border
  // rule between the sidebar and the main view.
  const auto fit = [](Element element) {
    return std::move(element) | size(WIDTH, LESS_THAN, layout::kSidebarWidth - 1);
  };

  Elements card = {
      text(""),
      text("  DISK FOOTPRINT") | color(palette::Label),
      fit(hbox(std::move(headline))),
      hbox({text("  "), FootprintStackBar(catalog)}),
      fit(text("  " + FootprintScaleNote(catalog, disk_total_bytes)) | color(palette::TextFaint)),
  };
  // When orphans exist, add one actionable reclaim line below the scale note,
  // toned like the update count so it reads as something to act on. Just the
  // reclaimable size - the orphan count already lives in the nav - so the line
  // stays short whatever the numbers. Zero orphans adds nothing, keeping the card
  // height stable.
  if (catalog.OrphanCount() > 0) {
    const std::string reclaim_note = "  reclaim " + model::FormatSize(catalog.ReclaimableBytes());
    card.push_back(fit(text(reclaim_note) | color(palette::Update)));
  }
  // The package cache is the other quiet disk sink: one line naming its size,
  // with the clean-cache key as the reclaim verb. Hidden when empty/unreadable.
  if (state.pacman_cache_bytes > 0) {
    const std::string cache_note = "  cache " + model::FormatSize(state.pacman_cache_bytes) +
                                   " · " + std::string(1, state.keys.clean_cache) + " to clean";
    card.push_back(fit(text(cache_note) | color(palette::TextFaint)));
  }

  return vbox(std::move(card)) | bgcolor(palette::Window);
}

Element Sidebar(const app::AppState& state, const model::Catalog& catalog) {
  Elements top;
  top.push_back(text(""));
  top.push_back(text("  VIEWS") | color(palette::Label));
  top.push_back(text(""));

  for (int index = 0; index < static_cast<int>(kNavEntries.size()); ++index) {
    const NavEntry& entry = kNavEntries[static_cast<size_t>(index)];
    // Collections aren't packages, so their nav count is the number of curated
    // sets rather than a catalog view count.
    const int count = entry.view == View::Collections
                          ? static_cast<int>(model::Collections().size())
                          : catalog.CountForView(entry.view);
    const bool active = state.view == entry.view;
    // Updates and Orphans both flag actionable work, so their counts glow.
    const bool highlight =
        (entry.view == View::Updates || entry.view == View::Orphans) && count > 0;
    top.push_back(NavRow(entry, index, count, active, highlight));
  }

  top.push_back(text(""));
  top.push_back(text("  SOURCES") | color(palette::Label));
  top.push_back(text(""));
  // The SOURCES axis: All and pacman are always present; every other source shows
  // only when its manager is surfaced. The active source carries the same accent
  // highlight as the active view, and each row is clickable (see SourceEntryAt).
  for (model::Source source : app::EnabledSources(state.managers)) {
    const bool active = state.source_filter == source;
    top.push_back(SourceRow(source, catalog.InstalledCountForSource(source), active));
  }

  // The nav/legend block flexes to push the footprint card to the foot of the
  // sidebar (and clips its lowest rows on a very short terminal); the card and
  // its rule stay pinned at the bottom. This relies on the package list being
  // virtualized so the body height is bounded to the screen - see PackageList.
  return vbox({
             vbox(std::move(top)) | flex,
             separator() | color(palette::Seam),
             FootprintCard(catalog, state),
         }) |
         bgcolor(palette::Sidebar);
}

// --- Title bar --------------------------------------------------------------

Element TitleBar() {
  return hbox({
      text(std::string(" ") + theme::glyph::logo + " PACSEEK ") | bold | color(palette::BgVoid) |
          bgcolor(palette::Accent),
      text("  PACKAGE MANAGER · PACMAN + AUR") | color(palette::TextFaint) | flex,
      text(std::string(theme::glyph::chrome_box) + " " + theme::glyph::chrome_box + " ") |
          color(palette::ChromeDim),
      text(std::string(theme::glyph::chrome_close) + " ") | color(palette::CloseTint),
  });
}

// --- Search + controls ------------------------------------------------------

Element SearchBar(const app::AppState& state, int result_count, const char* noun) {
  // The placeholder reflects what a search would scope to: a drilled-into
  // collection narrows to its members, the picker browses collections, and every
  // other view searches the whole catalog.
  std::string placeholder = "Search pacman + AUR…";
  if (const model::Collection* collection = ActiveCollection(state)) {
    placeholder = "Search " + collection->name + "…";
  } else if (state.view == View::Collections) {
    placeholder = "Browse collections…";
  }

  Element query_field;
  if (state.query.empty() && !state.search_focused) {
    query_field = text(placeholder) | color(palette::Label) | flex;
  } else {
    Elements parts = {text(state.query) | color(palette::Text)};
    if (state.search_focused) {
      parts.push_back(text("█") | color(palette::Accent) | blink);
    }
    parts.push_back(filler());
    query_field = hbox(std::move(parts)) | flex;
  }

  const std::string sort_label =
      std::string(state.sort == model::Sort::SizeDescending ? "SIZE " : "NAME ") +
      theme::glyph::sort_desc;

  Elements controls = {
      text(std::string(" ") + theme::glyph::prompt + " ") | color(palette::Accent),
      query_field,
      text(std::to_string(result_count) + " " + noun + "  ") | color(palette::Label),
  };
  // A non-All source filter reads as a chip beside SORT: a faint SOURCE label and
  // the source's own label in its identity color, on the sidebar-tinted track.
  if (state.source_filter != model::Source::All) {
    controls.push_back(text(" SOURCE ") | color(palette::TextFaint) | bgcolor(palette::Sidebar));
    controls.push_back(text(model::SourceLabel(state.source_filter) + " ") |
                       color(model::SourceColor(state.source_filter)) | bgcolor(palette::Sidebar));
  }
  controls.push_back(text(" SORT ") | color(palette::TextFaint) | bgcolor(palette::Sidebar));
  controls.push_back(text(sort_label + " ") | color(palette::TextMuted) | bgcolor(palette::Sidebar));
  return hbox(std::move(controls)) | bgcolor(palette::Sidebar);
}

// The STORAGE IMPACT column is the widest fixed column; on a narrow terminal it
// starves the flexible name column (names crush to a couple of characters and the
// header collides), so it is dropped below a width breakpoint. Row and header read
// the same flag so their geometry always matches.
bool ShowStorageColumn() {
  return ftxui::Terminal::Size().dimx >= layout::kStorageColumnBreakpoint;
}

Element ColumnHeader() {
  Elements cells = {
      text(" "),  // selection gutter
      text(" PACKAGE") | color(palette::Label) | flex,
      text("REPO") | color(palette::Label) | size(WIDTH, EQUAL, layout::kRepoColumnWidth),
  };
  if (ShowStorageColumn()) {
    cells.push_back(text("STORAGE IMPACT") | color(palette::Label) |
                    size(WIDTH, EQUAL, layout::kStorageColumnWidth));
  }
  cells.push_back(RightCell(text("SIZE") | color(palette::Label), layout::kSizeColumnWidth));
  cells.push_back(RightCell(text("ACTION ") | color(palette::Label), layout::kActionColumnWidth));
  return hbox(std::move(cells));
}

// --- Package row ------------------------------------------------------------

Element UpdateBadge(bool has_update) {
  if (!has_update) {
    return text("");
  }
  return text(" UPDATE ") | color(palette::Update) | bgcolor(palette::UpdateBadgeBg);
}

// A solid amber pill - dark text on the same amber that makes the nav Orphans
// count glow - so an orphan row is easy to pick out at a glance. Filled where the
// UPDATE badge is an outline, so the two stay distinguishable when both appear.
Element OrphanBadge(bool is_orphan) {
  if (!is_orphan) {
    return text("");
  }
  return text(" ORPHAN ") | color(palette::BgVoid) | bgcolor(palette::Update) | bold;
}

// Solid accent pill on a live AUR result the maintainer has flagged as
// out-of-date - the strongest "look closer before building this" cue the RPC
// carries, so it earns the loudest color.
Element OutOfDateBadge(bool flagged) {
  if (!flagged) {
    return text("");
  }
  return text(" OUT-OF-DATE ") | color(palette::BgVoid) | bgcolor(palette::Accent) | bold;
}

// Width available to the flexing name column: the list body minus the fixed
// columns, computed from the terminal like the windowing math. Knowing it up
// front lets long text be truncated deliberately (ellipsis, name over version)
// instead of FTXUI squeezing every cell of an over-wide hbox - live repos have
// rows like intel-oneapi-basekit 2026.0.1.27+... that mangle under that squeeze.
int NameColumnWidth() {
  int width = ftxui::Terminal::Size().dimx - layout::kSidebarWidth - 1 /*seam*/ -
              1 /*gutter*/ - layout::kRepoColumnWidth - layout::kSizeColumnWidth -
              layout::kActionColumnWidth;
  if (ShowStorageColumn()) {
    width -= layout::kStorageColumnWidth;
  }
  return std::max(width, 12);
}

Element PackageNameColumn(const Package& package, bool marked) {
  // A fixed two-cell marker slot keeps names aligned whether or not marked.
  Element marker = marked ? (text(std::string(theme::glyph::mark) + " ") | color(palette::Accent) | bold)
                          : text("  ");

  // Badges are never truncated: the name yields to them, the version yields to
  // the name, and whatever must shrink ends in an ellipsis, not a hard clip.
  int badge_cells = 0;
  if (package.has_update) {
    badge_cells += 1 + CellCount(" UPDATE ");
  }
  if (package.is_orphan) {
    badge_cells += 1 + CellCount(" ORPHAN ");
  }
  if (package.aur_out_of_date) {
    badge_cells += 1 + CellCount(" OUT-OF-DATE ");
  }
  const int budget = NameColumnWidth() - 2;  // minus the marker slot
  const int name_budget = budget - badge_cells;
  const std::string name = TruncateWithEllipsis(package.name, name_budget);

  Elements title_cells = {marker, text(name) | bold | color(palette::Text)};
  const int version_budget = name_budget - CellCount(name) - 2;
  if (version_budget >= 5 && !package.version.empty()) {
    title_cells.push_back(text("  "));
    title_cells.push_back(text(TruncateWithEllipsis(package.version, version_budget)) |
                          color(palette::Label));
  }
  if (package.has_update) {
    title_cells.push_back(text(" "));
    title_cells.push_back(UpdateBadge(true));
  }
  if (package.is_orphan) {
    title_cells.push_back(text(" "));
    title_cells.push_back(OrphanBadge(true));
  }
  if (package.aur_out_of_date) {
    title_cells.push_back(text(" "));
    title_cells.push_back(OutOfDateBadge(true));
  }

  Element description_line =
      hbox({text("  "),
            text(TruncateWithEllipsis(package.description, budget)) | color(palette::TextDim)});
  return vbox({hbox(std::move(title_cells)), description_line}) | flex;
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
  // A live AUR result has no size until it is built, but it does carry votes -
  // the column earns its keep as a trust signal instead of a meaningless "0 B".
  if (package.aur_votes >= 0 && package.install_size_bytes == 0) {
    Element votes = text(std::string(theme::glyph::vote) + " " + std::to_string(package.aur_votes)) |
                    color(palette::TextMuted) | bold;
    return vbox({RightCell(std::move(votes), layout::kSizeColumnWidth), text("")});
  }
  const Color size_color = model::IsHeavy(package) ? palette::Accent : palette::Text;
  Element value = text(model::FormatSize(package.install_size_bytes)) | color(size_color) | bold;
  return vbox({RightCell(std::move(value), layout::kSizeColumnWidth), text("")});
}

Element ActionColumn(const Package& package) {
  return vbox({RightCell(ActionButton(package.installed), layout::kActionColumnWidth), text("")});
}

Element PackageRow(const Package& package, int64_t max_size_bytes, bool selected, bool marked) {
  // The gutter signals selection (accent) and, when not selected, marked state.
  const Color gutter_color =
      selected ? palette::Accent : (marked ? palette::AccentDeep : palette::Window);
  Elements cells = {
      SolidBlock(1, gutter_color),
      PackageNameColumn(package, marked),
      vbox({RepoBadge(package.repo), text("")}) | size(WIDTH, EQUAL, layout::kRepoColumnWidth),
  };
  if (ShowStorageColumn()) {
    cells.push_back(StorageColumn(package, max_size_bytes));
  }
  cells.push_back(SizeColumn(package));
  cells.push_back(ActionColumn(package));
  Element row = hbox(std::move(cells));
  if (selected) {
    row = row | bgcolor(palette::RowSelectedBg);
  }
  return row;
}

// How the UI talks about sync freshness: the age of the newest sync db,
// rendered as a compact "SYNCED <age> AGO", turning amber once the data is old
// enough that the update counts shouldn't be trusted at a glance. Shared by the
// footer badge and the Updates view's empty state.
constexpr int64_t kSyncStaleSeconds = 7 * 24 * 60 * 60;

struct SyncFreshness {
  std::string label;  // "SYNCED" / "SYNCED 3d AGO"; empty when age is unknown
  bool stale = false;
};

SyncFreshness DescribeSyncAge(int64_t last_sync_seconds) {
  SyncFreshness freshness;
  if (last_sync_seconds <= 0) {
    return freshness;  // unknown (mock source): footer keeps the plain badge
  }
  const int64_t age = std::max<int64_t>(0, std::time(nullptr) - last_sync_seconds);
  freshness.stale = age >= kSyncStaleSeconds;
  if (age < 60 * 60) {
    freshness.label = "SYNCED";  // under an hour: fresh enough to stay quiet
  } else if (age < 48 * 60 * 60) {
    freshness.label = "SYNCED " + std::to_string(age / (60 * 60)) + "h AGO";
  } else {
    freshness.label = "SYNCED " + std::to_string(age / (24 * 60 * 60)) + "d AGO";
  }
  return freshness;
}

Element EmptyState(const app::AppState& state) {
  // An empty query means the slice itself is empty (e.g. a collection whose
  // members aren't in the local databases), not a search miss; word it for that.
  const std::string message =
      state.query.empty() ? "NO PACKAGES TO SHOW"
                          : "NO PACKAGES MATCH \"" + state.query + "\"";
  Elements body = {
      filler(),
      text(theme::glyph::empty) | color(palette::ChromeDim) | hcenter,
      text(""),
      text(message) | color(palette::Label) | hcenter,
  };
  // An empty Updates view only means "no updates in the last-synced data" -
  // pacseek never runs -Sy on its own, so an old database quietly reads as "up
  // to date". Say how old the answer is and point at the refresh key; amber once
  // it's stale enough that the emptiness shouldn't be trusted.
  if (state.query.empty() && state.view == View::Updates && state.last_sync_seconds > 0) {
    const SyncFreshness freshness = DescribeSyncAge(state.last_sync_seconds);
    if (!freshness.label.empty()) {
      std::string age = freshness.label;  // "SYNCED" / "SYNCED 3d AGO"
      for (char& c : age) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      if (age == "synced") {
        age += " under an hour ago";
      }
      body.push_back(text(""));
      body.push_back(text("databases " + age + " · " + std::string(1, state.keys.refresh) +
                          " re-checks") |
                     color(freshness.stale ? palette::Update : palette::TextDim) | hcenter);
    }
  }
  body.push_back(filler());
  return vbox(std::move(body)) | flex;
}

// Number of rows each package occupies: a two-line cell plus its divider.
constexpr int kRowsPerPackage = 3;
// Fixed chrome above and below the list (title, search, column header, footer,
// and their separator rules) that does not scroll with the packages.
constexpr int kListChromeRows = 8;

Element PackageList(const app::AppState& state, const std::vector<const Package*>& visible,
                    int64_t max_size_bytes) {
  if (visible.empty()) {
    return EmptyState(state);
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
    const Package& package = *visible[index];
    const bool selected = index == state.selected_index;
    const bool marked = state.marked.count(package.name) != 0;
    rows.push_back(PackageRow(package, max_size_bytes, selected, marked));
    rows.push_back(separator() | color(palette::RowDivider));
  }
  return vbox(std::move(rows)) | flex;
}

// --- Collections picker -----------------------------------------------------

// Width of the right-hand membership column on each collection card.
constexpr int kCollectionStatWidth = 16;

// The column header for the picker, mirroring ColumnHeader's geometry.
Element CollectionHeader() {
  return hbox({
      text(" "),  // selection gutter
      text(" COLLECTION") | color(palette::Label) | flex,
      RightCell(text("PACKAGES ") | color(palette::Label), kCollectionStatWidth),
  });
}

// The identity color for a collection's target manager, reusing the repo colors
// so a collection's badge matches the packages it installs.
Color ManagerColor(model::CollectionManager manager) {
  switch (manager) {
    case model::CollectionManager::Pacman:
      return model::RepoColor(model::Repo::Core);
    case model::CollectionManager::Aur:
      return model::RepoColor(model::Repo::Aur);
    case model::CollectionManager::Flatpak:
      return model::RepoColor(model::Repo::Flatpak);
    case model::CollectionManager::Homebrew:
      return model::RepoColor(model::Repo::Homebrew);
    case model::CollectionManager::Mixed:
      break;
  }
  return palette::TextMuted;
}

// The origin badge (and, for a user collection with an explicit target, a manager
// badge) that tells the three kinds of collection apart in the picker: the
// curated built-ins, the user's own from collections.ini, and pacman's groups.
Elements CollectionBadges(const model::Collection& collection) {
  Color origin_color = palette::TextFaint;
  switch (collection.origin) {
    case model::CollectionOrigin::Builtin:
      origin_color = palette::TextFaint;
      break;
    case model::CollectionOrigin::User:
      origin_color = palette::Accent;
      break;
    case model::CollectionOrigin::PacmanGroup:
      origin_color = model::RepoColor(model::Repo::Core);
      break;
  }
  Elements badges = {
      text(" "),
      text(" " + model::OriginLabel(collection.origin) + " ") | color(palette::BgVoid) |
          bgcolor(origin_color) | bold,
  };
  // Only a user collection carries a meaningful declared manager (built-ins are
  // Mixed, groups are self-evidently pacman); show its badge so an AUR- or
  // Homebrew-targeted collection reads at a glance.
  const std::string manager = model::ManagerLabel(collection.manager);
  if (collection.origin == model::CollectionOrigin::User && !manager.empty()) {
    badges.push_back(text(" "));
    badges.push_back(text(" " + manager + " ") | color(ManagerColor(collection.manager)) |
                     bgcolor(palette::Sidebar) | bold);
  }
  return badges;
}

// One collection card: icon + name + description on the left, an availability /
// installed readout on the right, gutter-accented when selected like a package.
Element CollectionRow(const model::Collection& collection, const model::Catalog& catalog,
                      bool selected) {
  const model::Catalog::Membership counts = catalog.MembershipCounts(collection.packages);
  const int total = static_cast<int>(collection.packages.size());
  const Color gutter = selected ? palette::Accent : palette::Window;
  const Color icon_color = selected ? palette::Accent : palette::TextDim;

  Elements title_cells = {
      text("  "),
      text(theme::SafeIcon(collection.icon)) | color(icon_color),
      text("  "),
      text(collection.name) | bold | color(palette::Text),
  };
  for (Element& badge : CollectionBadges(collection)) {
    title_cells.push_back(std::move(badge));
  }
  Element title = hbox(std::move(title_cells));
  Element description = hbox({text("    "), text(collection.description) | color(palette::TextDim)});

  Element installed_line =
      counts.installed > 0
          ? (text(std::to_string(counts.installed) + " installed ") | color(palette::Update))
          : text("");
  Element stats = vbox({
      RightCell(text(std::to_string(counts.available) + " / " + std::to_string(total) + " avail ") |
                    color(palette::Label),
                kCollectionStatWidth),
      RightCell(std::move(installed_line), kCollectionStatWidth),
  });

  Element row = hbox({
      SolidBlock(1, gutter),
      vbox({title, description}) | flex,
      stats,
  });
  if (selected) {
    row = row | bgcolor(palette::RowSelectedBg);
  }
  return row;
}

Element CollectionPicker(const app::AppState& state, const model::Catalog& catalog) {
  const std::vector<model::Collection>& collections = model::Collections();

  // Virtualized exactly like PackageList (same chrome height, same 3-row cards),
  // both so a picker taller than the terminal scrolls with the selection and so
  // ListIndexAt's shared windowing math maps clicks onto the right card. The
  // official pacman groups fold in here, so 100+ cards is the normal case.
  const int count = static_cast<int>(collections.size());
  const int viewport_rows = std::max(1, ftxui::Terminal::Size().dimy - kListChromeRows);
  const int capacity = std::max(1, viewport_rows / kRowsPerPackage);
  int start = std::clamp(state.selected_index - capacity / 2, 0, std::max(0, count - capacity));
  const int end = std::min(count, start + capacity);

  Elements rows;
  for (int index = start; index < end; ++index) {
    rows.push_back(CollectionRow(collections[index], catalog, index == state.selected_index));
    rows.push_back(separator() | color(palette::RowDivider));
  }
  return vbox(std::move(rows)) | flex;
}

// --- Detail pane ------------------------------------------------------------

// Defined below; the detail column reuses the same footer chrome as the list.
Element Footer(const app::AppState& state, const model::Catalog& catalog);

// Chrome rows around the scrolling detail content (window title bar + its rule,
// the pinned package header + rule, and the footer + rule).
constexpr int kDetailChromeRows = 6;
// Width of the key column in the provenance section.
constexpr int kDetailKeyWidth = 16;
// Upper bound on files listed, so a giant package can't build a vbox of tens of
// thousands of lines; the overflow is summarized instead.
constexpr int kMaxDetailFiles = 2000;

// One rendered line of the detail view, kept as plain data so the line count
// (used to clamp scrolling) and the rendered window stay in lockstep. `link`
// numbers the bullets that resolve to a package name (Tab-cycle targets), in
// order of appearance; -1 for everything else.
struct DetailLine {
  enum class Kind { Description, Section, KeyValue, Bullet, Note, Warning, Blank };
  Kind kind;
  std::string a;  // description / section title / key / bullet / note text
  std::string b;  // value, for KeyValue
  int link = -1;  // index into the pane's link order, -1 = not a link
};

// The package name at the head of a relationship entry: the longest prefix of
// package-name characters, which strips version constraints (">=1.2"), the
// optdepends ": reason" tail, and provides "=version" suffixes in one rule.
std::string LinkTarget(const std::string& entry) {
  size_t end = 0;
  while (end < entry.size()) {
    const char c = entry[end];
    const bool name_char = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '@' || c == '.' || c == '_' ||
                           c == '+' || c == '-';
    if (!name_char) {
      break;
    }
    ++end;
  }
  return entry.substr(0, end);
}

std::string JoinWith(const std::vector<std::string>& parts, const std::string& sep) {
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    out += parts[i];
    if (i + 1 < parts.size()) {
      out += sep;
    }
  }
  return out;
}

// Flattens a PackageDetail into the ordered list of lines the pane shows. The
// pinned header already carries the name + version, so this starts at the
// description.
std::vector<DetailLine> DetailLines(const model::PackageDetail& detail) {
  using Kind = DetailLine::Kind;
  std::vector<DetailLine> lines;
  auto blank = [&] { lines.push_back({Kind::Blank, "", ""}); };
  auto kv = [&](const std::string& key, const std::string& value) {
    if (!value.empty()) {
      lines.push_back({Kind::KeyValue, key, value});
    }
  };
  // A relationship bullet whose head is a package name becomes a Tab-cycle
  // link; the counter keeps the link order identical to DetailLinkNames'.
  int link_count = 0;
  auto linked = [&](const std::string& entry) {
    DetailLine line{Kind::Bullet, entry, "", -1};
    if (!LinkTarget(entry).empty()) {
      line.link = link_count++;
    }
    lines.push_back(std::move(line));
  };

  if (!detail.description.empty()) {
    lines.push_back({Kind::Description, detail.description, ""});
    blank();
  }

  lines.push_back({Kind::Section, "PROVENANCE", ""});
  kv("Repository", model::RepoBadgeLabel(detail.repo));
  kv("Install size", model::FormatSize(detail.install_size_bytes));
  kv("Licenses", JoinWith(detail.licenses, ", "));
  kv("URL", detail.url);
  kv("Packager", detail.packager);
  kv("Built", detail.build_date);
  kv("Installed on", detail.install_date);
  kv("Install reason", detail.install_reason);
  kv("Why installed", detail.why_installed);
  if (!detail.note.empty()) {
    lines.push_back({Kind::Note, detail.note, ""});
  }
  blank();

  // The AUR trust section: what the RPC info record says about this package.
  // Filled in asynchronously - the pane opens instantly on local data and this
  // section resolves from "fetching…" once the worker answers.
  if (detail.repo == model::Repo::Aur) {
    lines.push_back({Kind::Section, "AUR", ""});
    if (detail.aur_info_loaded) {
      if (detail.aur_votes >= 0) {
        lines.push_back({Kind::KeyValue, "Votes", std::to_string(detail.aur_votes)});
      }
      if (detail.aur_popularity >= 0.0) {
        char popularity[24];
        std::snprintf(popularity, sizeof(popularity), "%.2f", detail.aur_popularity);
        lines.push_back({Kind::KeyValue, "Popularity", popularity});
      }
      if (detail.aur_maintainer.empty()) {
        lines.push_back({Kind::Warning, "orphaned on the AUR · no maintainer", ""});
      } else {
        lines.push_back({Kind::KeyValue, "Maintainer", detail.aur_maintainer});
      }
      kv("Last updated", detail.aur_last_modified);
      if (!detail.aur_out_of_date.empty()) {
        lines.push_back({Kind::Warning,
                         std::string(theme::glyph::warning) + " flagged out-of-date since " +
                             detail.aur_out_of_date,
                         ""});
      }
    } else if (detail.aur_info_pending) {
      lines.push_back({Kind::Note, "fetching AUR data…", ""});
    } else if (!detail.aur_info_error.empty()) {
      lines.push_back({Kind::Note, detail.aur_info_error, ""});
    }
    blank();
  }

  // Marginal install cost: only computed for a not-installed sync package, shown
  // just above its dependency list so "what it costs" sits next to "what it pulls
  // in". An estimate over the sync closure, labelled as such.
  if (detail.marginal_computed) {
    lines.push_back({Kind::Section, "INSTALL COST", ""});
    lines.push_back({Kind::KeyValue, "New deps", std::to_string(detail.new_dep_count)});
    lines.push_back({Kind::KeyValue, "Total install", model::FormatSize(detail.marginal_install_bytes)});
    lines.push_back({Kind::KeyValue, "Download", model::FormatSize(detail.marginal_download_bytes)});
    lines.push_back({Kind::Note, "estimated from sync dependencies", ""});
    blank();
  }

  lines.push_back({Kind::Section, "DEPENDENCIES (" + std::to_string(detail.depends.size()) + ")", ""});
  if (detail.depends.empty()) {
    lines.push_back({Kind::Note, "none", ""});
  } else {
    for (const std::string& dep : detail.depends) {
      linked(dep);
    }
  }

  if (!detail.optdepends.empty()) {
    blank();
    lines.push_back(
        {Kind::Section, "OPTIONAL DEPENDENCIES (" + std::to_string(detail.optdepends.size()) + ")", ""});
    for (const std::string& dep : detail.optdepends) {
      linked(dep);
    }
  }

  if (!detail.provides.empty()) {
    blank();
    lines.push_back(
        {Kind::Section, "PROVIDES (" + std::to_string(detail.provides.size()) + ")", ""});
    for (const std::string& entry : detail.provides) {
      linked(entry);
    }
  }

  if (!detail.conflicts.empty()) {
    blank();
    lines.push_back(
        {Kind::Section, "CONFLICTS (" + std::to_string(detail.conflicts.size()) + ")", ""});
    for (const std::string& entry : detail.conflicts) {
      linked(entry);
    }
  }

  if (!detail.replaces.empty()) {
    blank();
    lines.push_back(
        {Kind::Section, "REPLACES (" + std::to_string(detail.replaces.size()) + ")", ""});
    for (const std::string& entry : detail.replaces) {
      linked(entry);
    }
  }

  if (!detail.required_by.empty()) {
    blank();
    lines.push_back(
        {Kind::Section, "REQUIRED BY (" + std::to_string(detail.required_by.size()) + ")", ""});
    for (const std::string& name : detail.required_by) {
      linked(name);
    }
  }

  blank();
  lines.push_back({Kind::Section, "FILES (" + std::to_string(detail.files.size()) + ")", ""});
  if (detail.files.empty()) {
    lines.push_back({Kind::Note, detail.files_note.empty() ? "none" : detail.files_note, ""});
  } else {
    const int shown = std::min(static_cast<int>(detail.files.size()), kMaxDetailFiles);
    for (int i = 0; i < shown; ++i) {
      lines.push_back({Kind::Bullet, detail.files[i], ""});
    }
    if (shown < static_cast<int>(detail.files.size())) {
      lines.push_back(
          {Kind::Note, "… and " + std::to_string(static_cast<int>(detail.files.size()) - shown) +
                           " more (not shown)", ""});
    }
  }
  return lines;
}

Element RenderDetailLine(const DetailLine& line, int selected_link) {
  switch (line.kind) {
    case DetailLine::Kind::Description:
      return text("  " + line.a) | color(palette::TextDim);
    case DetailLine::Kind::Section:
      return text(line.a) | color(palette::Label) | bold;
    case DetailLine::Kind::KeyValue:
      return hbox({
          text("  " + line.a) | color(palette::Label) | size(WIDTH, EQUAL, kDetailKeyWidth),
          text(line.b) | color(palette::Text),
      });
    case DetailLine::Kind::Bullet:
      // The Tab-selected link renders as an accented cursor row, telling the
      // eye where Enter will jump.
      if (line.link >= 0 && line.link == selected_link) {
        return hbox({text(std::string("  ") + theme::glyph::arrow + " ") | color(palette::Accent) | bold,
                     text(line.a) | color(palette::Text) | bold});
      }
      return hbox({text(std::string("  ") + theme::glyph::bullet + " ") | color(palette::ChromeDim),
                   text(line.a) | color(palette::TextDim)});
    case DetailLine::Kind::Note:
      return text("  " + line.a) | color(palette::TextFaint);
    case DetailLine::Kind::Warning:
      return text("  " + line.a) | color(palette::Update);
    case DetailLine::Kind::Blank:
    default:
      return text("");
  }
}

int DetailViewportRows() {
  return std::max(1, ftxui::Terminal::Size().dimy - kDetailChromeRows);
}

// The pinned header: package name + version stay visible while files scroll.
Element DetailHeader(const model::PackageDetail& detail) {
  return hbox({
             text("  "),
             text(detail.name) | color(palette::Text) | bold,
             text(" " + detail.version) | color(palette::TextFaint),
             filler(),
             RepoBadge(detail.repo),
             text("  "),
         }) |
         bgcolor(palette::Sidebar);
}

// The scrolling body: a window of detail lines sized to the terminal.
Element DetailBody(const app::AppState& state) {
  const std::vector<DetailLine> lines = DetailLines(state.detail);
  const int total = static_cast<int>(lines.size());
  const int viewport = DetailViewportRows();
  const int start = std::clamp(state.detail_scroll, 0, std::max(0, total - viewport));
  const int end = std::min(total, start + viewport);

  Elements rendered;
  for (int i = start; i < end; ++i) {
    rendered.push_back(RenderDetailLine(lines[i], state.detail_link));
  }
  return vbox(std::move(rendered)) | flex;
}

Element DetailColumn(const app::AppState& state, const model::Catalog& catalog) {
  return vbox({
             DetailHeader(state.detail),
             separator() | color(palette::Seam),
             DetailBody(state),
             separator() | color(palette::Seam),
             Footer(state, catalog),
         }) |
         flex;
}

// --- Controls popout --------------------------------------------------------

// A backdrop scrim: renders its child, then darkens every pixel toward black.
// FTXUI's color() decorator paints *before* the child draws, so the child's own
// colors win and a plain color override can't dim a colored layer. Doing it as a
// post-render pass over the pixels is the only way to actually push the whole
// background back. 0 = unchanged, 1 = full black.
constexpr float kScrimStrength = 0.84f;

class ScrimNode : public Node {
 public:
  explicit ScrimNode(Element child) : Node(Elements{std::move(child)}) {}

  void ComputeRequirement() override {
    children_[0]->ComputeRequirement();
    requirement_ = children_[0]->requirement();
  }
  void SetBox(Box box) override {
    Node::SetBox(box);
    children_[0]->SetBox(box);
  }
  void Render(Screen& screen) override {
    children_[0]->Render(screen);  // draw the background, then crush it down
    for (int y = box_.y_min; y <= box_.y_max; ++y) {
      for (int x = box_.x_min; x <= box_.x_max; ++x) {
        Pixel& pixel = screen.PixelAt(x, y);
        pixel.foreground_color =
            Color::Interpolate(kScrimStrength, pixel.foreground_color, Color::Black);
        pixel.background_color =
            Color::Interpolate(kScrimStrength, pixel.background_color, Color::Black);
        pixel.dim = true;
        pixel.bold = false;
      }
    }
  }
};

Element Scrim(Element child) { return std::make_shared<ScrimNode>(std::move(child)); }

// Clears a one-cell margin around the child's box to a solid background (plain
// space cells) before drawing it. FTXUI auto-merges box-drawing characters with
// their orthogonal neighbours, so a modal's border merges with the scrimmed
// package separators sitting just outside it into stray ┤/├/┬ junctions all down
// the frame. Blanking the surrounding ring means the border's neighbours carry no
// box glyphs, so it stays clean and cornered - and the ring reads as a tidy gutter
// setting the dialog off the dimmed backdrop.
class OpaquePanelNode : public Node {
 public:
  OpaquePanelNode(Element child, Color background)
      : Node(Elements{std::move(child)}), background_(background) {}

  void ComputeRequirement() override {
    children_[0]->ComputeRequirement();
    requirement_ = children_[0]->requirement();
  }
  void SetBox(Box box) override {
    Node::SetBox(box);
    children_[0]->SetBox(box);
  }
  void Render(Screen& screen) override {
    const int x_from = std::max(0, box_.x_min - 1);
    const int x_to = std::min(screen.dimx() - 1, box_.x_max + 1);
    const int y_from = std::max(0, box_.y_min - 1);
    const int y_to = std::min(screen.dimy() - 1, box_.y_max + 1);
    for (int y = y_from; y <= y_to; ++y) {
      for (int x = x_from; x <= x_to; ++x) {
        Pixel& pixel = screen.PixelAt(x, y);
        pixel = Pixel();
        pixel.background_color = background_;
      }
    }
    children_[0]->Render(screen);
  }

 private:
  Color background_;
};

Element OpaquePanel(Element child, Color background) {
  return std::make_shared<OpaquePanelNode>(std::move(child), background);
}

// Shared width of the centered modal dialogs (controls popout, confirmation), so
// they line up and the opaque-bar math has one source of truth.
constexpr int kModalWidth = 58;

// Greedy word-wrap of `text` to at most `width` columns per line, for the
// confirmation message. ASCII-oriented (the messages are plain English); never
// returns empty so the caller always emits at least one bar.
std::vector<std::string> WrapText(const std::string& text, int width) {
  std::vector<std::string> lines;
  std::istringstream stream(text);
  std::string word;
  std::string current;
  while (stream >> word) {
    if (current.empty()) {
      current = word;
    } else if (static_cast<int>(current.size() + 1 + word.size()) <= width) {
      current += " " + word;
    } else {
      lines.push_back(current);
      current = word;
    }
  }
  if (!current.empty()) {
    lines.push_back(current);
  }
  if (lines.empty()) {
    lines.push_back("");
  }
  return lines;
}

// The centered overlay listing every binding, opened with '?'. A static legend
// so the footer can stay a single hint instead of a wall of keys.
//
// Every line is a full-width opaque bar (content + filler over a solid bg): a
// short row would otherwise only paint its own cells, letting the list behind it
// bleed through the gaps. This mirrors how the footer and headers stay opaque.
Element HelpModal(const config::Keybindings& keys) {
  // Stacked single-column, the popout is ~30 rows tall: on a 24-32 row terminal
  // it clips or collides with the footer. When the terminal is short but wide,
  // the same sections render as two side-by-side columns (~17 rows) instead.
  const bool two_column =
      ftxui::Terminal::Size().dimy < 36 && ftxui::Terminal::Size().dimx >= 116;
  const int column_width = two_column ? 54 : kModalWidth;
  const int key_width = two_column ? 14 : 18;

  // FTXUI's bgcolor only paints cells that carry a glyph, so size/filler padding
  // stays transparent and the list behind bleeds through. Each line is therefore
  // drawn over a SolidBlock - a run of real space cells - which fills every
  // column opaquely; the text is layered on top with dbox.
  auto line = [&](Element content, Color background) {
    return dbox({SolidBlock(column_width, background), std::move(content)});
  };
  auto blank = [&] { return line(text(""), palette::Sidebar); };
  auto section = [&](const std::string& title) {
    return line(text(" " + title) | color(palette::Label) | bold, palette::Sidebar);
  };
  auto row = [&](const std::string& label, const std::string& description) {
    return line(hbox({
                    text("  " + label) | color(palette::Accent) | size(WIDTH, EQUAL, key_width),
                    text(description) | color(palette::Text),
                }),
                palette::Sidebar);
  };
  // Render a rebindable key by its current binding, so the popout stays truthful
  // after a config rebind. Fixed keys (enter / space / esc / digits) keep literals.
  auto key = [](char bound) { return std::string(1, bound); };

  Elements navigate = {
      section("NAVIGATE"),
      row("j / k", "move selection (also ↓ / ↑)"),
      row("1 – 6", "switch the six library views"),
      row("enter / esc", "collections · open / back out"),
      row(key(keys.search), "focus search (esc / enter to leave)"),
  };
  Elements inspect = {
      section("INSPECT"),
      row(key(keys.detail), "open the package detail pane"),
      row("tab / enter", "detail pane · cycle links · jump"),
      row("backspace", "detail pane · back to previous"),
      row(key(keys.file_lookup), "find which package owns a file"),
      row(key(keys.sort), "toggle sort: size ↓ / name ↓"),
      row(key(keys.filter), "cycle source (all/pacman/aur/…)"),
  };
  Elements act = {
      section("ACT"),
      row("space", "mark / unmark for a batch"),
      row(key(keys.mark_all), "mark / unmark everything visible"),
      row("enter", "apply marked (or act on selection)"),
      row("enter", "search box, AUR source · live AUR search"),
      row(key(keys.update), "full system update (-Syu)"),
      row(key(keys.refresh), "refresh the sync databases (-Sy)"),
      row(key(keys.reason), "flip install reason (explicit / dep)"),
      row(key(keys.clean_cache), "clean the package cache (paccache)"),
      row(key(keys.pacdiff), "merge .pacnew configs (pacdiff)"),
      row(key(keys.export_list) + " / " + key(keys.import_list),
          "back up / restore explicit packages"),
  };
  Elements general = {
      section("GENERAL"),
      row(key(keys.help), "toggle this controls panel"),
      row(key(keys.quit) + " / esc", "close a pane · quit"),
  };

  // Joins section groups with a blank spacer bar between them.
  auto stack = [&](std::vector<Elements> groups) {
    Elements out;
    for (Elements& group : groups) {
      if (!out.empty()) {
        out.push_back(blank());
      }
      for (Element& element : group) {
        out.push_back(std::move(element));
      }
    }
    return out;
  };

  int title_width = column_width;
  Element body;
  if (two_column) {
    Elements left = stack({std::move(navigate), std::move(inspect)});
    Elements right = stack({std::move(act), std::move(general)});
    // Equalize heights so both columns stay opaque all the way down, and keep
    // the gutter between them opaque with its own run of solid rows.
    while (left.size() < right.size()) {
      left.push_back(blank());
    }
    while (right.size() < left.size()) {
      right.push_back(blank());
    }
    Elements gutter;
    for (size_t i = 0; i < left.size(); ++i) {
      gutter.push_back(SolidBlock(2, palette::Sidebar));
    }
    title_width = column_width * 2 + 2;
    body = hbox({vbox(std::move(left)), vbox(std::move(gutter)), vbox(std::move(right))});
  } else {
    body = vbox(stack({std::move(navigate), std::move(inspect), std::move(act),
                       std::move(general)}));
  }

  Element title = dbox({SolidBlock(title_width, palette::Accent),
                        text(" CONTROLS ") | bold | color(palette::BgVoid)});
  return vbox({std::move(title), std::move(body)}) | border | color(palette::ChromeDim);
}

// The pre-commit confirmation dialog, shared by the partial-upgrade guard and
// the removal-cascade preview. Same opaque full-width-bar technique as
// HelpModal: an accent title bar, the wrapped message, an optional bulleted item
// list (the cascade packages), and a footnote naming the keys.
Element ConfirmModal(const app::AppState& state) {
  auto line = [&](Element content, Color background) {
    return dbox({SolidBlock(kModalWidth, background), std::move(content)});
  };
  auto blank = [&] { return line(text(""), palette::Sidebar); };

  Elements rows;
  rows.push_back(
      line(text(" " + state.confirm_title + " ") | bold | color(palette::BgVoid), palette::Accent));
  rows.push_back(blank());
  // The leading space keeps text off the border; wrap to the inset width.
  for (const std::string& message_line : WrapText(state.confirm_message, kModalWidth - 2)) {
    rows.push_back(line(text(" " + message_line) | color(palette::Text), palette::Sidebar));
  }
  if (!state.confirm_items.empty()) {
    rows.push_back(blank());
    for (const std::string& item : state.confirm_items) {
      rows.push_back(line(hbox({text("  · ") | color(palette::ChromeDim),
                                text(item) | color(palette::TextDim)}),
                          palette::Sidebar));
    }
  }
  if (!state.confirm_footnote.empty()) {
    rows.push_back(blank());
    rows.push_back(
        line(text(" " + state.confirm_footnote) | color(palette::Label), palette::Sidebar));
  }
  return vbox(std::move(rows)) | border | color(palette::ChromeDim);
}

// The file → package lookup dialog, opened with 'o'. Same opaque full-width-bar
// technique as HelpModal/ConfirmModal: an accent title bar, a live query prompt
// with a blinking cursor (like the search field), then - once a lookup has run -
// the owners tagged OWNS (installed, -Qo) or PROVIDES (available, -F), any note,
// and the key footnote.
Element FileLookupModal(const app::AppState& state) {
  auto line = [&](Element content, Color background) {
    return dbox({SolidBlock(kModalWidth, background), std::move(content)});
  };
  auto blank = [&] { return line(text(""), palette::Sidebar); };

  Elements rows;
  rows.push_back(
      line(text(" FILE → PACKAGE ") | bold | color(palette::BgVoid), palette::Accent));
  rows.push_back(blank());
  rows.push_back(line(hbox({
                          text(std::string(" ") + theme::glyph::prompt + " ") | color(palette::Accent),
                          text(state.file_lookup_query) | color(palette::Text),
                          text("█") | color(palette::Accent) | blink,
                      }),
                      palette::Sidebar));

  if (state.file_lookup_has_result) {
    rows.push_back(blank());
    if (!state.file_lookup_shown_query.empty()) {
      rows.push_back(line(text(" " + state.file_lookup_shown_query) | color(palette::TextDim),
                          palette::Sidebar));
    }
    if (state.file_lookup_owners.empty()) {
      const std::string subject =
          state.file_lookup_shown_query.empty() ? "that file" : state.file_lookup_shown_query;
      rows.push_back(
          line(text(" no package owns " + subject) | color(palette::Label), palette::Sidebar));
    } else {
      // OWNS for an installed owner (-Qo), PROVIDES for an available one (-F).
      const std::string tag = state.file_lookup_from_files_db ? "PROVIDES" : "OWNS";
      for (const std::string& owner : state.file_lookup_owners) {
        rows.push_back(line(hbox({
                                text("  " + tag + " ") | color(palette::Accent) | bold,
                                text(owner) | color(palette::Text),
                            }),
                            palette::Sidebar));
      }
    }
    if (!state.file_lookup_note.empty()) {
      rows.push_back(line(text(" " + state.file_lookup_note) | color(palette::TextFaint),
                          palette::Sidebar));
    }
  }

  rows.push_back(blank());
  rows.push_back(
      line(text(" enter look up · esc close") | color(palette::Label), palette::Sidebar));
  return vbox(std::move(rows)) | border | color(palette::ChromeDim);
}

// --- Footer -----------------------------------------------------------------

Element Footer(const app::AppState& state, const model::Catalog& catalog) {
  // On a narrow terminal the three footer sections collide once the fillers
  // collapse, so shed the least-essential chrome: the contextual center hint
  // (`?` still opens the full controls), the "SYNCED" label (the dot stays), and
  // the wordier status/warning labels drop to their bare counts.
  const bool narrow = ftxui::Terminal::Size().dimx < layout::kStorageColumnBreakpoint;

  const int updates = catalog.CountForView(View::Updates);
  const std::string updates_line =
      updates > 0 ? std::to_string(updates) + (narrow ? " UPDATES" : " UPDATES AVAILABLE")
                  : (narrow ? "UP TO DATE" : "SYSTEM UP TO DATE");

  Element center;
  if (state.confirm_open) {
    center = text("enter confirm · esc cancel") | color(palette::Label);
  } else if (state.file_lookup_open) {
    center = text("type a path · enter look up · esc close") | color(palette::Label);
  } else if (state.help_open) {
    center = text("esc close") | color(palette::Label);
  } else if (!state.status_message.empty()) {
    // Status outranks the detail hint so a failed link jump ("xyz not in the
    // catalog") is visible while the pane stays open.
    center = text(state.status_message) | color(palette::Update) | bold;
  } else if (state.detail_open) {
    center = text("j/k scroll · tab links · backspace back · esc close") | color(palette::Label);
  } else if (!state.marked.empty()) {
    center = hbox({
        text(std::to_string(state.marked.size()) + " marked") | color(palette::Accent) | bold,
        text(" · enter to apply") | color(palette::Label),
    });
  } else if (const model::Collection* collection = ActiveCollection(state)) {
    center = hbox({
        text(collection->name) | color(palette::Accent) | bold,
        text(" · esc back to collections") | color(palette::Label),
    });
  } else if (state.view == View::Collections) {
    center = hbox({
        text("enter ") | color(palette::Accent) | bold,
        text("open collection") | color(palette::Label),
    });
  } else if (state.view == View::Orphans) {
    // The reclaimable total already sits on the footprint card, so the footer
    // just points at the action that frees it.
    center = catalog.OrphanCount() > 0
                 ? hbox({
                       text("space ") | color(palette::Accent) | bold,
                       text("mark · ") | color(palette::Label),
                       text("enter ") | color(palette::Accent) | bold,
                       text("remove") | color(palette::Label),
                   })
                 : hbox({text("no orphans · nothing to reclaim") | color(palette::Label)});
  } else {
    // A single legend rather than every binding; '?' opens the full controls
    // popout. The accented key tells the eye where to look.
    center = hbox({
        text("? ") | color(palette::Accent) | bold,
        text("controls") | color(palette::Label),
    });
  }

  const std::string source_badge = state.source_name + (state.read_only ? " · READ-ONLY " : " ");

  // Unmerged config files left by updates (.pacnew / .pacsave) read as an
  // attention cue after the updates line; nothing shows when the count is zero.
  // The label collapses to just the count on a narrow terminal.
  Element pacnew =
      state.pacnew_count > 0
          ? (text(std::string("   ") + theme::glyph::warning + " " + std::to_string(state.pacnew_count) +
                  (narrow ? "" : std::string(" unmerged config") +
                                     (state.pacnew_count == 1 ? "" : "s"))) |
             color(palette::Update))
          : text("");

  // The sync badge carries its age: a stale database means the updates line
  // beside it can't be trusted, so the dot and label turn amber together.
  const SyncFreshness freshness = DescribeSyncAge(state.last_sync_seconds);
  const Color sync_color = freshness.stale ? palette::Update : palette::Ok;
  Elements left_cells = {text(std::string(" ") + theme::glyph::sync_dot) | color(sync_color)};
  if (!narrow) {
    const std::string sync_label = freshness.label.empty() ? "SYNCED" : freshness.label;
    left_cells.push_back(text(" " + sync_label) |
                         color(freshness.stale ? palette::Update : palette::TextFaint));
  }
  left_cells.push_back(text("   "));
  left_cells.push_back(text(updates_line) |
                       color(updates > 0 ? palette::Update : palette::TextFaint));
  left_cells.push_back(pacnew);
  Element left = hbox(std::move(left_cells));
  Element right = text(source_badge) | color(palette::TextFaint);

  // The contextual center hint is the lowest-priority zone: show it only when the
  // status block, hint, and source badge all fit without colliding (once the
  // fillers collapse, hbox clips text into a run-on). Measure each zone's required
  // width rather than guessing from a fixed breakpoint, since the hint's length
  // varies by view (the Orphans "space mark · enter remove" is far wider than the
  // generic "? controls").
  left->ComputeRequirement();
  center->ComputeRequirement();
  right->ComputeRequirement();
  const int body_width = ftxui::Terminal::Size().dimx - layout::kSidebarWidth - 1;
  bool show_center = left->requirement().min_x + center->requirement().min_x +
                         right->requirement().min_x + 2 <=
                     body_width;

  // A status message is the result of the user's last keypress; dropping it
  // whole makes the action look ignored ("nothing happened when I pressed c").
  // Unlike the decorative hints, it gets shrunk to whatever space remains.
  if (!show_center && !state.status_message.empty() && !state.confirm_open &&
      !state.file_lookup_open && !state.help_open) {
    const int available =
        body_width - left->requirement().min_x - right->requirement().min_x - 4;
    if (available >= 8) {
      center = text(TruncateWithEllipsis(state.status_message, available)) |
               color(palette::Update) | bold;
      show_center = true;
    }
  }

  Elements cells = {left, filler()};
  if (show_center) {
    cells.push_back(center);
    cells.push_back(filler());
  }
  cells.push_back(right);
  return hbox(std::move(cells)) | bgcolor(palette::Sidebar);
}

}  // namespace

int ClampDetailScroll(const model::PackageDetail& detail, int desired_scroll) {
  const int total = static_cast<int>(DetailLines(detail).size());
  return std::clamp(desired_scroll, 0, std::max(0, total - DetailViewportRows()));
}

std::vector<std::string> DetailLinkNames(const model::PackageDetail& detail) {
  // Link indices are assigned in line order by DetailLines, so collecting the
  // linked bullets in order yields exactly the index space the renderer uses.
  std::vector<std::string> names;
  for (const DetailLine& line : DetailLines(detail)) {
    if (line.link >= 0) {
      names.push_back(LinkTarget(line.a));
    }
  }
  return names;
}

int RevealDetailLink(const model::PackageDetail& detail, int scroll, int link_index) {
  const std::vector<DetailLine> lines = DetailLines(detail);
  int line_index = -1;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (lines[i].link == link_index) {
      line_index = static_cast<int>(i);
      break;
    }
  }
  if (line_index < 0) {
    return scroll;
  }
  const int viewport = DetailViewportRows();
  if (line_index < scroll) {
    return line_index;
  }
  if (line_index >= scroll + viewport) {
    return line_index - viewport + 1;
  }
  return scroll;
}

int ListIndexAt(const app::AppState& state, int row_count, int x, int y) {
  // Everything here mirrors PackageList's windowing exactly - same chrome
  // heights, same capacity, same centered start - so a click lands on the row
  // the user sees. Package rows and collection cards are both three rows tall.
  if (row_count <= 0 || x <= layout::kSidebarWidth) {
    return -1;
  }
  // Rows start below: title bar, rule, search bar, rule, column header, rule.
  constexpr int kListTopRows = 6;
  const int viewport_rows = std::max(1, ftxui::Terminal::Size().dimy - kListChromeRows);
  const int capacity = std::max(1, viewport_rows / kRowsPerPackage);
  const int start =
      std::clamp(state.selected_index - capacity / 2, 0, std::max(0, row_count - capacity));
  const int end = std::min(row_count, start + capacity);
  if (y < kListTopRows || y >= kListTopRows + (end - start) * kRowsPerPackage) {
    return -1;
  }
  return start + (y - kListTopRows) / kRowsPerPackage;
}

int NavEntryAt(int x, int y) {
  if (x >= layout::kSidebarWidth) {
    return -1;
  }
  // Above the nav rows: the title bar and its rule, then the sidebar's blank /
  // LIBRARY label / blank header - fixed chrome, so the offset is a constant.
  constexpr int kNavTopRows = 5;
  const int index = y - kNavTopRows;
  return index >= 0 && index < static_cast<int>(kNavEntries.size()) ? index : -1;
}

int SourceEntryAt(const app::AppState& state, int x, int y) {
  if (x >= layout::kSidebarWidth) {
    return -1;
  }
  // Below the VIEWS block: the same fixed chrome NavEntryAt skips (5 rows), then
  // the view rows, then the blank / SOURCES label / blank that opens the list.
  constexpr int kNavTopRows = 5;
  const int sources_top = kNavTopRows + static_cast<int>(kNavEntries.size()) + 3;
  const int index = y - sources_top;
  const int count = static_cast<int>(app::EnabledSources(state.managers).size());
  return index >= 0 && index < count ? index : -1;
}

Element RenderApp(const app::AppState& state, const model::Catalog& catalog,
                  const std::vector<const Package*>& visible) {
  const int64_t max_size_bytes = catalog.MaxInstallSizeBytes();

  // The detail pane is a modal layer over the main column; the sidebar and title
  // bar stay put so the package stays in context. The collections picker swaps
  // the package list for a list of curated sets but keeps the same chrome height
  // (search / header / body / footer) so the virtualization math is unchanged.
  Element main_column;
  if (state.detail_open) {
    main_column = DetailColumn(state, catalog);
  } else if (state.view == View::Collections && state.active_collection < 0) {
    main_column = vbox({
                      SearchBar(state, static_cast<int>(model::Collections().size()),
                                "COLLECTIONS"),
                      separator() | color(palette::Seam),
                      CollectionHeader(),
                      separator() | color(palette::Seam),
                      CollectionPicker(state, catalog),
                      separator() | color(palette::Seam),
                      Footer(state, catalog),
                  }) |
                  flex;
  } else {
    main_column = vbox({
                      SearchBar(state, static_cast<int>(visible.size()), "RESULTS"),
                      separator() | color(palette::Seam),
                      ColumnHeader(),
                      separator() | color(palette::Seam),
                      PackageList(state, visible, max_size_bytes),
                      separator() | color(palette::Seam),
                      Footer(state, catalog),
                  }) |
                  flex;
  }

  Element body = hbox({
                     Sidebar(state, catalog) | size(WIDTH, EQUAL, layout::kSidebarWidth),
                     separator() | color(palette::Seam),
                     main_column,
                 }) |
                 flex;

  Element root = vbox({
                     TitleBar(),
                     separator() | color(palette::Seam),
                     body,
                 }) |
                 bgcolor(palette::Window);

  // Float the controls popout, centered, over a heavily dimmed background so it
  // reads as a focused dialog. Terminals can't blur, so Scrim darkens the whole
  // layer behind as a post-render pass; the modal is a separate dbox layer drawn
  // on top, so it stays full strength.
  if (state.confirm_open) {
    root = dbox({Scrim(root), OpaquePanel(ConfirmModal(state), palette::Sidebar) | center});
  } else if (state.file_lookup_open) {
    root = dbox({Scrim(root), OpaquePanel(FileLookupModal(state), palette::Sidebar) | center});
  } else if (state.help_open) {
    root = dbox({Scrim(root), OpaquePanel(HelpModal(state.keys), palette::Sidebar) | center});
  }
  return root;
}

}  // namespace pacseek::ui
