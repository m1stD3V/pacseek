// app_state.hpp - the mutable UI state, the TUI analogue of the prototype's
// {view, query, sort, ...}. Plain data; the app layer owns transitions.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "config/keybindings.hpp"
#include "model/catalog.hpp"
#include "model/package_detail.hpp"

namespace pacseek::app {

struct AppState {
  model::View view = model::View::Browse;
  model::Sort sort = model::Sort::SizeDescending;
  std::string query;

  // The rebindable single-key actions, seeded from config. Plain data the UI
  // reads so the controls popout stays truthful after a rebind; the app dispatch
  // compares events against these instead of hardcoded characters.
  config::Keybindings keys;

  // Repo filter, cycled with 'f': when active, the package-list views show only
  // rows from `repo_filter`. Off by default so every source is visible; the
  // cycle steps OFF → Core → Extra → Multilib → Aur → Flatpak → OFF.
  bool filter_active = false;
  model::Repo repo_filter = model::Repo::Core;

  // Index into the currently-visible row list; clamped whenever the list changes.
  int selected_index = 0;
  // True while the search field has keyboard focus (typing edits the query).
  bool search_focused = false;

  // Packages marked for a batch install/remove, keyed by name so a mark persists
  // across views, searches, and sorting. Enter applies them; see App::ApplyMarked.
  std::unordered_set<std::string> marked;

  // Detail pane: when open it replaces the package list with the expanded view of
  // `detail`, scrolled by `detail_scroll` lines. The app loads `detail` lazily
  // from the source when the pane is opened. `detail_link` is the index (into
  // ui::DetailLinkNames' order) of the package link Tab has selected, -1 = none;
  // Enter on a selected link jumps to that package's detail.
  bool detail_open = false;
  int detail_scroll = 0;
  int detail_link = -1;
  model::PackageDetail detail;

  // Collections view: a two-level browse. -1 shows the picker (the list of
  // curated collections); >= 0 is the index into model::Collections() (built-ins
  // followed by any user-defined ones) of the open collection, whose member
  // packages then fill the list.
  int active_collection = -1;

  // Controls popout: a centered overlay listing every key binding. Topmost modal;
  // while open it floats over whatever is behind it.
  bool help_open = false;

  // Pre-commit confirmation modal, shared by the partial-upgrade guard and the
  // removal-cascade preview. When open it floats over the list like the help
  // popout and swallows every key except enter (proceed) / esc (cancel); the
  // partial-upgrade case additionally offers 'u' to update first. The strings
  // below drive the render: title bar, one message line, an optional bulleted
  // item list (the cascade packages), and a footnote of the available keys.
  bool confirm_open = false;
  std::string confirm_title;
  std::string confirm_message;
  std::vector<std::string> confirm_items;
  std::string confirm_footnote;

  // File → package lookup modal (opened with 'o'): answers "which package owns
  // this file or command?". While open the query line accepts typed input;
  // Enter runs the lookup and fills the result-display fields below. These stay
  // plain data - the app copies the source's FileOwnerResult into them so
  // AppState never depends on the data/ layer.
  bool file_lookup_open = false;
  std::string file_lookup_query;
  bool file_lookup_has_result = false;      // a lookup has run and populated the fields
  std::vector<std::string> file_lookup_owners;
  bool file_lookup_from_files_db = false;   // owners provide (-F), not own (-Qo)
  std::string file_lookup_note;
  std::string file_lookup_shown_query;      // the normalized query the result is for

  // Transient line shown in the footer, e.g. the read-only action notice.
  std::string status_message;

  // Identity of the active data source, for the footer.
  std::string source_name = "libalpm";
  bool read_only = true;

  // Total capacity of the filesystem packages live on, in bytes. Measured once at
  // startup; the footprint card shows installed size against it. 0 = unknown.
  int64_t disk_total_bytes = 0;

  // Count of unmerged config files (.pacnew / .pacsave) left under /etc after
  // updates. Measured at startup and after each transaction; the footer surfaces
  // it as an attention cue when non-zero, and the pacdiff key acts on it.
  int pacnew_count = 0;

  // When the sync databases were last refreshed (`pacman -Sy`), unix seconds.
  // The footer renders the age so update counts carry their freshness; 0 =
  // unknown (mock source) and the footer stays as plain SYNCED.
  int64_t last_sync_seconds = 0;

  // Total size of pacman's package cache (/var/cache/pacman/pkg), measured at
  // startup and after each transaction. Feeds the footprint card's cache line
  // and the clean-cache confirmation; 0 = empty or unreadable, line hidden.
  int64_t pacman_cache_bytes = 0;
};

}  // namespace pacseek::app
