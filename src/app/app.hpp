// app.hpp - owns the run loop: holds the catalog and UI state, translates key
// presses into state transitions, and drives FTXUI. The cli.py analogue.
#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <ftxui/component/screen_interactive.hpp>

#include "app/app_state.hpp"
#include "config/config.hpp"
#include "data/package_source.hpp"
#include "model/catalog.hpp"
#include "system/transaction.hpp"

namespace pacseek::app {

class App {
 public:
  // Loads packages from the source up front; the source's read-only flag decides
  // whether the action keys apply real transactions or just show a notice. The
  // config seeds the initial view/sort and can override the detected AUR helper.
  App(data::PackageSource& source, const config::Config& config = {});

  // Joins any in-flight AUR search worker before the screen it posts to dies.
  ~App();

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
  // Number of selectable rows in the current screen: the collection count while
  // the picker is showing, otherwise the visible package count. Keeps selection
  // clamping correct when the rows aren't packages.
  int CurrentRowCount() const;
  bool HandleEvent(const ftxui::Event& event);
  bool HandleSearchEvent(const ftxui::Event& event);
  void MoveSelection(int delta);
  void SwitchView(model::View view);
  void ToggleSort();

  // Detail pane control. OpenDetail loads the selected row's expanded data from
  // the source; ScrollDetail moves the viewport; CloseDetail returns to the list.
  void OpenDetail();
  void CloseDetail();
  void ScrollDetail(int delta);
  // Collections picker control. OpenCollection drills from the picker into the
  // selected collection's package list; CloseCollection returns to the picker.
  void OpenCollection();
  void CloseCollection();

  void TriggerActionOnSelection();
  void TriggerSystemUpdate();
  void ApplyPendingTransaction();
  void ResetSelection();

  // Multi-select. ToggleMark adds/removes the selected row from the marked set;
  // ApplyMarked turns that set into a single batch transaction (falling back to
  // the single-row action when nothing is marked). FindPackage resolves a marked
  // name back to its Package across the catalog and any live AUR results.
  void ToggleMark();
  void ApplyMarked();
  const model::Package* FindPackage(const std::string& name) const;

  // Spawns a worker that queries the AUR RPC for the current search term; the
  // worker posts a wake-up event when it finishes. AdoptAurResults then folds the
  // results into aur_results_ on the UI thread. Only fires in the AUR view, and
  // serves repeat terms from aur_cache_ without touching the network.
  void LaunchAurSearch();
  void AdoptAurResults();
  // Tags installed packages, swaps the result set into the view, and updates the
  // status line. Shared by the network path and a cache hit.
  void ShowAurResults(const std::vector<model::Package>& fetched, bool from_cache);

  data::PackageSource& source_;
  AppState state_;
  model::Catalog catalog_;
  system::Tools tools_;
  std::optional<PendingTransaction> pending_transaction_;

  // Live AUR search. aur_results_ is a second catalog so the visible-row
  // filtering and sorting are reused; aur_search_active_ swaps it in for the
  // local foreign-package listing while remote results are on screen.
  model::Catalog aur_results_;
  bool aur_search_active_ = false;
  bool aur_searching_ = false;  // a fetch is in flight; gates re-launches
  std::string aur_term_;        // the term being searched / currently shown
  std::thread aur_worker_;
  std::mutex aur_mutex_;                      // guards the handoff fields below
  std::vector<model::Package> aur_pending_;   // worker output, awaiting adoption
  std::string aur_pending_error_;
  bool aur_ready_ = false;
  // Per-session memo of term -> fetched results, so re-searching a term (or
  // toggling away and back) never re-hits the AUR. Holds raw fetched packages;
  // installed-state is re-tagged on each show against the current catalog.
  std::unordered_map<std::string, std::vector<model::Package>> aur_cache_;

  ftxui::ScreenInteractive screen_;
};

}  // namespace pacseek::app
