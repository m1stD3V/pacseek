// app_state.hpp - the mutable UI state, the TUI analogue of the prototype's
// {view, query, sort, ...}. Plain data; the app layer owns transitions.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

#include "model/catalog.hpp"
#include "model/package_detail.hpp"

namespace pacseek::app {

struct AppState {
  model::View view = model::View::Browse;
  model::Sort sort = model::Sort::SizeDescending;
  std::string query;

  // Index into the currently-visible row list; clamped whenever the list changes.
  int selected_index = 0;
  // True while the search field has keyboard focus (typing edits the query).
  bool search_focused = false;

  // Packages marked for a batch install/remove, keyed by name so a mark persists
  // across views, searches, and sorting. Enter applies them; see App::ApplyMarked.
  std::unordered_set<std::string> marked;

  // Detail pane: when open it replaces the package list with the expanded view of
  // `detail`, scrolled by `detail_scroll` lines. The app loads `detail` lazily
  // from the source when the pane is opened.
  bool detail_open = false;
  int detail_scroll = 0;
  model::PackageDetail detail;

  // Collections view: a two-level browse. -1 shows the picker (the list of
  // curated collections); >= 0 is the index into model::Collections() (built-ins
  // followed by any user-defined ones) of the open collection, whose member
  // packages then fill the list.
  int active_collection = -1;

  // Controls popout: a centered overlay listing every key binding. Topmost modal;
  // while open it floats over whatever is behind it.
  bool help_open = false;

  // Transient line shown in the footer, e.g. the read-only action notice.
  std::string status_message;

  // Identity of the active data source, for the footer.
  std::string source_name = "libalpm";
  bool read_only = true;

  // Total capacity of the filesystem packages live on, in bytes. Measured once at
  // startup; the footprint card shows installed size against it. 0 = unknown.
  int64_t disk_total_bytes = 0;
};

}  // namespace pacseek::app
