// components.hpp — the brutalist render layer. Pure functions from state to
// FTXUI Elements (the art.py analogue): no input handling, no mutation.
#pragma once

#include <vector>

#include <ftxui/dom/elements.hpp>

#include "app/app_state.hpp"
#include "model/catalog.hpp"
#include "model/package.hpp"

namespace pacseek::ui {

// Composes the whole window: title bar, sidebar + main column, footer.
// `visible` is the already-filtered/sorted row list the app computed, so the
// selection index in `state` lines up with what is drawn.
ftxui::Element RenderApp(const app::AppState& state, const model::Catalog& catalog,
                         const std::vector<const model::Package*>& visible);

}  // namespace pacseek::ui
