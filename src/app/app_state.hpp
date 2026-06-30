// app_state.hpp — the mutable UI state, the TUI analogue of the prototype's
// {view, query, sort, ...}. Plain data; the app layer owns transitions.
#pragma once

#include <cstdint>
#include <string>

#include "model/catalog.hpp"

namespace pacseek::app {

struct AppState {
  model::View view = model::View::Browse;
  model::Sort sort = model::Sort::SizeDescending;
  std::string query;

  // Index into the currently-visible row list; clamped whenever the list changes.
  int selected_index = 0;
  // True while the search field has keyboard focus (typing edits the query).
  bool search_focused = false;

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
