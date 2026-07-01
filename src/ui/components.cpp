#include "ui/components.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>

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
    {View::Collections, "◆", "Collections"},
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
    // Collections aren't packages, so their nav count is the number of curated
    // sets rather than a catalog view count.
    const int count = entry.view == View::Collections
                          ? static_cast<int>(model::Collections().size())
                          : catalog.CountForView(entry.view);
    const bool active = state.view == entry.view;
    const bool highlight = entry.view == View::Updates && count > 0;
    top.push_back(NavRow(entry, count, active, highlight));
  }

  top.push_back(text(""));
  top.push_back(text("  REPOSITORIES") | color(palette::Label));
  top.push_back(text(""));
  for (Repo repo : {Repo::Core, Repo::Extra, Repo::Aur, Repo::Multilib, Repo::Flatpak}) {
    top.push_back(LegendRow(repo, catalog.InstalledCountForRepo(repo)));
  }

  // The nav/legend block flexes to push the footprint card to the foot of the
  // sidebar (and clips its lowest rows on a very short terminal); the card and
  // its rule stay pinned at the bottom. This relies on the package list being
  // virtualized so the body height is bounded to the screen - see PackageList.
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
      state.sort == model::Sort::SizeDescending ? "SIZE ↓" : "NAME ↓";

  return hbox({
             text(" ❯ ") | color(palette::Accent),
             query_field,
             text(std::to_string(result_count) + " " + noun + "  ") | color(palette::Label),
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

Element PackageNameColumn(const Package& package, bool marked) {
  // A fixed two-cell marker slot keeps names aligned whether or not marked.
  Element marker = marked ? (text("✓ ") | color(palette::Accent) | bold) : text("  ");
  Element title_line = hbox({
      marker,
      text(package.name) | bold | color(palette::Text),
      text("  "),
      text(package.version) | color(palette::Label),
      text(" "),
      UpdateBadge(package.has_update),
  });
  Element description_line = hbox({text("  "), text(package.description) | color(palette::TextDim)});
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

Element PackageRow(const Package& package, int64_t max_size_bytes, bool selected, bool marked) {
  // The gutter signals selection (accent) and, when not selected, marked state.
  const Color gutter_color =
      selected ? palette::Accent : (marked ? palette::AccentDeep : palette::Window);
  Element row = hbox({
      SolidBlock(1, gutter_color),
      PackageNameColumn(package, marked),
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
  // An empty query means the slice itself is empty (e.g. a collection whose
  // members aren't in the local databases), not a search miss; word it for that.
  const std::string message =
      query.empty() ? "NO PACKAGES TO SHOW" : "NO PACKAGES MATCH \"" + query + "\"";
  return vbox({
             filler(),
             text("⊘") | color(palette::ChromeDim) | hcenter,
             text(""),
             text(message) | color(palette::Label) | hcenter,
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

// One collection card: icon + name + description on the left, an availability /
// installed readout on the right, gutter-accented when selected like a package.
Element CollectionRow(const model::Collection& collection, const model::Catalog& catalog,
                      bool selected) {
  const model::Catalog::Membership counts = catalog.MembershipCounts(collection.packages);
  const int total = static_cast<int>(collection.packages.size());
  const Color gutter = selected ? palette::Accent : palette::Window;
  const Color icon_color = selected ? palette::Accent : palette::TextDim;

  Element title = hbox({
      text("  "),
      text(collection.icon) | color(icon_color),
      text("  "),
      text(collection.name) | bold | color(palette::Text),
  });
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
  Elements rows;
  for (int index = 0; index < static_cast<int>(collections.size()); ++index) {
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
// (used to clamp scrolling) and the rendered window stay in lockstep.
struct DetailLine {
  enum class Kind { Description, Section, KeyValue, Bullet, Note, Blank };
  Kind kind;
  std::string a;  // description / section title / key / bullet / note text
  std::string b;  // value, for KeyValue
};

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
  if (!detail.note.empty()) {
    lines.push_back({Kind::Note, detail.note, ""});
  }
  blank();

  lines.push_back({Kind::Section, "DEPENDENCIES (" + std::to_string(detail.depends.size()) + ")", ""});
  if (detail.depends.empty()) {
    lines.push_back({Kind::Note, "none", ""});
  } else {
    for (const std::string& dep : detail.depends) {
      lines.push_back({Kind::Bullet, dep, ""});
    }
  }

  if (!detail.optdepends.empty()) {
    blank();
    lines.push_back(
        {Kind::Section, "OPTIONAL DEPENDENCIES (" + std::to_string(detail.optdepends.size()) + ")", ""});
    for (const std::string& dep : detail.optdepends) {
      lines.push_back({Kind::Bullet, dep, ""});
    }
  }

  if (!detail.required_by.empty()) {
    blank();
    lines.push_back(
        {Kind::Section, "REQUIRED BY (" + std::to_string(detail.required_by.size()) + ")", ""});
    for (const std::string& name : detail.required_by) {
      lines.push_back({Kind::Bullet, name, ""});
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

Element RenderDetailLine(const DetailLine& line) {
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
      return hbox({text("  · ") | color(palette::ChromeDim), text(line.a) | color(palette::TextDim)});
    case DetailLine::Kind::Note:
      return text("  " + line.a) | color(palette::TextFaint);
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
    rendered.push_back(RenderDetailLine(lines[i]));
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

// The centered overlay listing every binding, opened with '?'. A static legend
// so the footer can stay a single hint instead of a wall of keys.
//
// Every line is a full-width opaque bar (content + filler over a solid bg): a
// short row would otherwise only paint its own cells, letting the list behind it
// bleed through the gaps. This mirrors how the footer and headers stay opaque.
Element HelpModal() {
  // FTXUI's bgcolor only paints cells that carry a glyph, so size/filler padding
  // stays transparent and the list behind bleeds through. Each line is therefore
  // drawn over a SolidBlock - a run of real space cells - which fills every
  // column opaquely; the text is layered on top with dbox.
  constexpr int kHelpWidth = 58;
  auto line = [&](Element content, Color background) {
    return dbox({SolidBlock(kHelpWidth, background), std::move(content)});
  };
  auto blank = [&] { return line(text(""), palette::Sidebar); };
  auto section = [&](const std::string& title) {
    return line(text(" " + title) | color(palette::Label) | bold, palette::Sidebar);
  };
  auto row = [&](const std::string& keys, const std::string& description) {
    return line(hbox({
                    text("  " + keys) | color(palette::Accent) | size(WIDTH, EQUAL, 18),
                    text(description) | color(palette::Text),
                }),
                palette::Sidebar);
  };

  return vbox({
             line(text(" CONTROLS ") | bold | color(palette::BgVoid), palette::Accent),
             section("NAVIGATE"),
             row("j / k", "move selection (also ↓ / ↑)"),
             row("1 – 5", "switch the five library views"),
             row("enter / esc", "collections · open / back out"),
             row("/", "focus search (esc / enter to leave)"),
             blank(),
             section("INSPECT"),
             row("d", "open the package detail pane"),
             row("s", "toggle sort: size ↓ / name ↓"),
             blank(),
             section("ACT"),
             row("space", "mark / unmark for a batch"),
             row("enter", "apply marked (or act on selection)"),
             row("enter", "AUR search box · live AUR search"),
             row("u", "full system update (-Syu)"),
             blank(),
             section("GENERAL"),
             row("?", "toggle this controls panel"),
             row("q / esc", "close a pane · quit"),
         }) |
         border | color(palette::ChromeDim);
}

// --- Footer -----------------------------------------------------------------

Element Footer(const app::AppState& state, const model::Catalog& catalog) {
  const int updates = catalog.CountForView(View::Updates);
  const std::string updates_line =
      updates > 0 ? std::to_string(updates) + " UPDATES AVAILABLE" : "SYSTEM UP TO DATE";

  Element center;
  if (state.help_open) {
    center = text("esc close") | color(palette::Label);
  } else if (state.detail_open) {
    center = text("j/k scroll · esc close") | color(palette::Label);
  } else if (!state.status_message.empty()) {
    center = text(state.status_message) | color(palette::Update) | bold;
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
  } else {
    // A single legend rather than every binding; '?' opens the full controls
    // popout. The accented key tells the eye where to look.
    center = hbox({
        text("? ") | color(palette::Accent) | bold,
        text("controls") | color(palette::Label),
    });
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

int ClampDetailScroll(const model::PackageDetail& detail, int desired_scroll) {
  const int total = static_cast<int>(DetailLines(detail).size());
  return std::clamp(desired_scroll, 0, std::max(0, total - DetailViewportRows()));
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
  if (state.help_open) {
    root = dbox({Scrim(root), HelpModal() | center});
  }
  return root;
}

}  // namespace pacseek::ui
