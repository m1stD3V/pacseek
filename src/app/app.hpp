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
#include "system/aur_client.hpp"
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
  // Advances the repo filter one step through OFF → Core → Extra → Multilib →
  // Aur → Flatpak → OFF, narrowing the package list to a single source (or, at
  // OFF, showing everything again). Cycled with 'f'.
  void CycleRepoFilter();

  // Detail pane control. OpenDetail loads the selected row's expanded data from
  // the source; ScrollDetail moves the viewport; CloseDetail returns to the list.
  // The pane is navigable: Tab cycles the relationship links (CycleDetailLink),
  // Enter jumps to the selected link's package (OpenLinkedDetail, pushing the
  // current package onto the trail), and Backspace walks the trail back
  // (DetailBack). LoadDetailFor is the shared loader: it Describe()s the package
  // and, for AUR rows, kicks the async info fetch.
  void OpenDetail();
  void CloseDetail();
  void ScrollDetail(int delta);
  void LoadDetailFor(const model::Package& package);
  void CycleDetailLink(int direction);
  void OpenLinkedDetail();
  void DetailBack();
  // Collections picker control. OpenCollection drills from the picker into the
  // selected collection's package list; CloseCollection returns to the picker.
  void OpenCollection();
  void CloseCollection();
  // File → package lookup modal. OpenFileLookup opens it with a cleared query
  // and no prior result; RunFileLookup queries the source for the current query
  // and copies the answer into the AppState display fields.
  void OpenFileLookup();
  void RunFileLookup();

  void TriggerActionOnSelection();
  void TriggerSystemUpdate();
  // Refreshes the sync databases (`pacman -Sy`) so the Updates view reflects
  // upstream again - the update counts are only as fresh as the last -Sy, which
  // pacseek itself never runs implicitly. User-initiated, through the same
  // terminal handoff as any transaction; the partial-upgrade guard covers the
  // classic refresh-then-install hazard.
  void TriggerDatabaseRefresh();
  // System maintenance actions (both from pacman-contrib, both suspend the TUI
  // like any transaction): TriggerCacheClean confirms then runs `paccache -r`;
  // TriggerPacdiff runs `pacdiff` when unmerged .pacnew/.pacsave files exist.
  void TriggerCacheClean();
  void TriggerPacdiff();
  // Re-measures the footer/card indicators that a transaction can change:
  // pacnew count, package-cache size, and the sync-database age.
  void RefreshSystemIndicators();
  // Whole-system backup/restore of the explicit package set (the `pacman -Qqe`
  // sibling of user collections). ExportPackageList writes the sorted explicit
  // names one-per-line to DefaultPackageListPath(); ImportPackageList reads that
  // file and queues a batch install of the names not already present, routed
  // through the same partial-upgrade guard as a marked install. Both are fully
  // local and honour read-only with a "would …" notice.
  void ExportPackageList();
  void ImportPackageList();
  // Confirmation flow. Queues a transaction behind the pre-commit modal instead
  // of running it immediately: OpenConfirm populates the confirm_* state and
  // stashes the transaction; ConfirmProceed commits it (exits the loop to apply);
  // ConfirmCancel discards it. The guarded action decides whether to prompt at
  // all - see TriggerActionOnSelection / ApplyMarked. `consumes_marks` says
  // whether proceeding spends the marked set (package transactions do; a cache
  // clean has nothing to do with marks and leaves them intact).
  void OpenConfirm(PendingTransaction transaction, bool is_partial_upgrade,
                   bool consumes_marks = true);
  // Populates the partial-upgrade confirm text and opens it around `transaction`.
  // Shared by the single-package install and the marked-install batch.
  void OpenPartialUpgradeConfirm(PendingTransaction transaction);
  void ConfirmProceed();
  void ConfirmCancel();
  // Flips the selected installed package's recorded install reason between
  // explicit and dependency (`pacman -D`). No-op with a notice for a package
  // that isn't installed - only installed packages carry a reason.
  void ToggleInstallReason();
  void ApplyPendingTransaction();
  void ResetSelection();

  // Multi-select. ToggleMark adds/removes the selected row from the marked set;
  // ToggleMarkAll marks every visible row (or clears them when all are already
  // marked) - the one-keystroke path for "remove all orphans". ApplyMarked turns
  // that set into a single batch transaction (falling back to the single-row
  // action when nothing is marked). FindPackage resolves a marked name back to
  // its Package across the catalog and any live AUR results.
  void ToggleMark();
  void ToggleMarkAll();
  void ApplyMarked();
  const model::Package* FindPackage(const std::string& name) const;

  // Mouse: wheel scrolls the selection (or the detail pane), left-click selects
  // the row under the cursor or switches sidebar views. Hit-testing lives in
  // ui/components so it shares the renderer's layout math. Takes the event by
  // value because FTXUI's Event::mouse() accessor is non-const.
  bool HandleMouse(ftxui::Event event);

  // Spawns a worker that queries the AUR RPC for the current search term; the
  // worker posts a wake-up event when it finishes. AdoptAurResults then folds the
  // results into aur_results_ on the UI thread. Only fires in the AUR view, and
  // serves repeat terms from aur_cache_ without touching the network.
  void LaunchAurSearch();
  void AdoptAurResults();
  // Same worker pattern for the detail pane's AUR info record: the pane opens
  // instantly on local data, the fetch resolves in the background, and
  // AdoptAurInfo merges it in if that package is still on screen. Answers are
  // memoized per name; fetch errors are not, so a reopen retries.
  void MaybeFetchAurInfo(const std::string& name);
  void AdoptAurInfo();
  void ApplyAurInfoToDetail(const system::AurInfo& info);
  // Tags installed packages, swaps the result set into the view, and updates the
  // status line. Shared by the network path and a cache hit.
  void ShowAurResults(const std::vector<model::Package>& fetched, bool from_cache);

  data::PackageSource& source_;
  AppState state_;
  model::Catalog catalog_;
  system::Tools tools_;
  std::optional<PendingTransaction> pending_transaction_;
  // The transaction awaiting the confirmation modal; committed to
  // pending_transaction_ on proceed, dropped on cancel. confirm_is_partial_
  // distinguishes the partial-upgrade prompt (where 'u' updates first) from the
  // removal-cascade prompt (where 'u' does nothing).
  std::optional<PendingTransaction> confirm_pending_;
  bool confirm_is_partial_upgrade_ = false;
  bool confirm_consumes_marks_ = true;

  // The names walked through via detail links, oldest first; Backspace pops.
  std::vector<std::string> detail_trail_;

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

  // The detail pane's AUR info fetch, mirroring the search worker's handoff:
  // one in-flight fetch at a time, results adopted on the UI thread, answers
  // memoized per package name for the session.
  std::thread aur_info_worker_;
  std::mutex aur_info_mutex_;                 // guards the handoff fields below
  system::AurInfo aur_info_pending_result_;
  std::string aur_info_pending_error_;
  std::string aur_info_pending_name_;
  bool aur_info_ready_ = false;
  bool aur_info_fetching_ = false;
  std::unordered_map<std::string, system::AurInfo> aur_info_cache_;

  ftxui::ScreenInteractive screen_;
};

}  // namespace pacseek::app
