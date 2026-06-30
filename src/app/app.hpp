// app.hpp — owns the run loop: holds the catalog and UI state, translates key
// presses into state transitions, and drives FTXUI. The cli.py analogue.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <ftxui/component/screen_interactive.hpp>

#include "app/app_state.hpp"
#include "data/package_source.hpp"
#include "model/catalog.hpp"
#include "system/transaction.hpp"

namespace pacseek::app {

class App {
 public:
  // Loads packages from the source up front; the source's read-only flag decides
  // whether the action keys apply real transactions or just show a notice.
  App(data::PackageSource& source);

  // Runs the fullscreen TUI until the user quits.
  void Run();

 private:
  // A transaction queued by the event handler, applied after the loop suspends
  // so pacman can run in the restored terminal.
  struct PendingTransaction {
    system::Action action;
    std::string package_name;
    std::string command_line;
  };

  std::vector<const model::Package*> CurrentVisible() const;
  bool HandleEvent(const ftxui::Event& event);
  bool HandleSearchEvent(const ftxui::Event& event);
  void MoveSelection(int delta);
  void SwitchView(model::View view);
  void ToggleSort();
  void TriggerActionOnSelection();
  void ApplyPendingTransaction();
  void ResetSelection();

  data::PackageSource& source_;
  AppState state_;
  model::Catalog catalog_;
  system::Tools tools_;
  std::optional<PendingTransaction> pending_transaction_;
  ftxui::ScreenInteractive screen_;
};

}  // namespace pacseek::app
