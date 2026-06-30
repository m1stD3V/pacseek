// app.hpp — owns the run loop: holds the catalog and UI state, translates key
// presses into state transitions, and drives FTXUI. The cli.py analogue.
#pragma once

#include <vector>

#include <ftxui/component/screen_interactive.hpp>

#include "app/app_state.hpp"
#include "data/package_source.hpp"
#include "model/catalog.hpp"

namespace pacseek::app {

class App {
 public:
  // Loads packages from the source up front; `read_only` reflects whether the
  // source can apply transactions (false would enable real install/remove).
  App(data::PackageSource& source);

  // Runs the fullscreen TUI until the user quits.
  void Run();

 private:
  std::vector<const model::Package*> CurrentVisible() const;
  bool HandleEvent(const ftxui::Event& event);
  bool HandleSearchEvent(const ftxui::Event& event);
  void MoveSelection(int delta);
  void SwitchView(model::View view);
  void ToggleSort();
  void TriggerActionOnSelection();
  void ResetSelection();

  AppState state_;
  model::Catalog catalog_;
  ftxui::ScreenInteractive screen_;
};

}  // namespace pacseek::app
