// components.hpp - the brutalist render layer. Pure functions from state to
// FTXUI Elements (the art.py analogue): no input handling, no mutation.
#pragma once

#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>

#include "app/app_state.hpp"
#include "model/catalog.hpp"
#include "model/package.hpp"
#include "model/package_detail.hpp"

namespace pacseek::ui {

// Composes the whole window: title bar, sidebar + main column, footer.
// `visible` is the already-filtered/sorted row list the app computed, so the
// selection index in `state` lines up with what is drawn.
ftxui::Element RenderApp(const app::AppState& state, const model::Catalog& catalog,
                         const std::vector<const model::Package*>& visible);

// Clamps a desired detail-pane scroll offset to its content and the current
// terminal height, so the view never scrolls past the last line. Call once per
// frame (the terminal size must be valid) before RenderApp.
int ClampDetailScroll(const model::PackageDetail& detail, int desired_scroll);

// The package names the detail pane's relationship bullets resolve to (depends,
// optdepends, provides, conflicts, replaces, required-by), in render order.
// This is the single source of truth for Tab-cycling: the app indexes into this
// list and the renderer highlights the same index, so they can never disagree.
std::vector<std::string> DetailLinkNames(const model::PackageDetail& detail);

// Returns the scroll offset that brings the `link_index`-th link into the
// detail viewport, starting from `scroll` (unchanged when already visible).
int RevealDetailLink(const model::PackageDetail& detail, int scroll, int link_index);

// Mouse hit-testing, sharing the exact layout math the renderer draws with.
// ListIndexAt maps a click to an index into the visible row list (packages or
// collection cards - both are three terminal rows tall), or -1 when the click
// is outside the list body. NavEntryAt maps a click on the sidebar to a LIBRARY
// entry index (the kViewHotkeys order), or -1.
int ListIndexAt(const app::AppState& state, int row_count, int x, int y);
int NavEntryAt(int x, int y);

}  // namespace pacseek::ui
