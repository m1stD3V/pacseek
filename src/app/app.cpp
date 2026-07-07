#include "app/app.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include <utility>

#include <sys/statvfs.h>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>

#include "model/collection.hpp"
#include "model/package.hpp"
#include "system/aur_client.hpp"
#include "theme.hpp"
#include "ui/components.hpp"

namespace pacseek::app {

using ftxui::Event;

namespace {
// Maps the number-row keys to the LIBRARY nav order.
constexpr std::array<std::pair<char, model::View>, 6> kViewHotkeys = {{
    {'1', model::View::Browse},
    {'2', model::View::Installed},
    {'3', model::View::Updates},
    {'4', model::View::Aur},
    {'5', model::View::Collections},
    {'6', model::View::Orphans},
}};

// Custom event an AUR worker thread posts to wake the loop once its results are
// ready; HandleEvent adopts them on the UI thread.
const ftxui::Event kAurResultsReady = ftxui::Event::Special("pacseek-aur-results");

// Same wake-up pattern for the detail pane's async AUR info fetch.
const ftxui::Event kAurInfoReady = ftxui::Event::Special("pacseek-aur-info");

// Which package manager owns a package, derived from its repo tag.
system::Manager ManagerForRepo(model::Repo repo) {
  switch (repo) {
    case model::Repo::Aur:
      return system::Manager::Aur;
    case model::Repo::Flatpak:
      return system::Manager::Flatpak;
    case model::Repo::Homebrew:
      return system::Manager::Homebrew;
    default:
      return system::Manager::Pacman;
  }
}

// Total capacity, in bytes, of the filesystem holding the package tree ("/").
// Returns 0 if the query fails, which the footprint card renders as "unknown".
int64_t QueryDiskCapacityBytes() {
  struct statvfs stats;
  if (statvfs("/", &stats) != 0) {
    return 0;
  }
  return static_cast<int64_t>(stats.f_blocks) * static_cast<int64_t>(stats.f_frsize);
}

// Best-effort count of unmerged config files (.pacnew / .pacsave) left under
// /etc after updates. Essentially all of them live there and /etc is small, so
// a bounded recursive scan stays fast; the whole filesystem is never touched.
// The error_code overloads plus skip_permission_denied keep an unreadable entry
// from throwing, and the try/catch swallows anything else - the indicator is
// informational, so a partial or zero count is always an acceptable result.
int CountPacnewFiles() {
  namespace fs = std::filesystem;
  int count = 0;
  try {
    std::error_code error;
    const fs::directory_options options = fs::directory_options::skip_permission_denied;
    for (fs::recursive_directory_iterator it("/etc", options, error), end; it != end;
         it.increment(error)) {
      if (error) {
        break;  // give up on the first hard error, keeping whatever we counted
      }
      std::error_code file_error;
      if (it->is_regular_file(file_error)) {
        const std::string extension = it->path().extension().string();
        if (extension == ".pacnew" || extension == ".pacsave") {
          ++count;
        }
      }
    }
  } catch (...) {
    // Swallow: best-effort, keep whatever was counted before the failure.
  }
  return count;
}

// Total size of pacman's package cache. The cache directory is flat and
// world-readable on a standard install; any error just leaves the running
// total where it was (0 = the footprint card hides the line).
int64_t QueryPacmanCacheBytes() {
  namespace fs = std::filesystem;
  int64_t total = 0;
  std::error_code error;
  for (fs::directory_iterator it("/var/cache/pacman/pkg", error), end; it != end;
       it.increment(error)) {
    if (error) {
      break;
    }
    std::error_code file_error;
    if (it->is_regular_file(file_error)) {
      const auto size = it->file_size(file_error);
      if (!file_error) {
        total += static_cast<int64_t>(size);
      }
    }
  }
  return total;
}
}  // namespace

App::App(data::PackageSource& source, const config::Config& config)
    : source_(source),
      catalog_(source.LoadPackages()),
      tools_(system::DetectTools()),
      screen_(ftxui::ScreenInteractive::Fullscreen()) {
  state_.source_name = source.Name();
  state_.read_only = source.IsReadOnly();
  state_.disk_total_bytes = QueryDiskCapacityBytes();
  RefreshSystemIndicators();

  // Apply user configuration: seed the initial view/sort, and let an explicit
  // aur_helper override what DetectTools probed from PATH - but only if it is
  // actually installed, so a typo can't leave AUR installs running a missing
  // command. Otherwise keep the detected helper and surface a notice.
  state_.view = config.view;
  state_.sort = config.sort;
  // Seed the rebindable keys so the dispatcher and the controls popout both read
  // the user's bindings; an untouched config leaves the built-in defaults.
  state_.keys = config.keys;
  // Which managers to surface in the legend / filter (main decides the sources).
  state_.managers.aur = config.aur_enabled;
  state_.managers.flatpak = config.flatpak_enabled;
  state_.managers.homebrew = config.homebrew_enabled;
  // Install the glyph set before the first frame so ASCII mode is consistent.
  theme::SetGlyphs(config.ascii_glyphs);
  if (!config.aur_helper.empty()) {
    if (system::IsToolAvailable(config.aur_helper)) {
      tools_.aur_helper = config.aur_helper;
    } else {
      state_.status_message =
          "configured aur_helper '" + config.aur_helper + "' not found on PATH · " +
          (tools_.aur_helper.empty() ? "no AUR helper available"
                                     : "using detected " + tools_.aur_helper);
    }
  }

  // Apply the color theme before the first frame; an unknown name keeps default.
  if (!config.theme.empty() && !theme::SetTheme(config.theme)) {
    state_.status_message = "unknown theme '" + config.theme + "' · using default";
  }
}

App::~App() {
  // The workers capture `this` and post to screen_; never let them outlive it.
  if (aur_worker_.joinable()) {
    aur_worker_.join();
  }
  if (aur_info_worker_.joinable()) {
    aur_info_worker_.join();
  }
}

void App::RefreshSystemIndicators() {
  state_.pacnew_count = CountPacnewFiles();
  state_.pacman_cache_bytes = QueryPacmanCacheBytes();
  state_.last_sync_seconds = source_.LastSyncSeconds();
}

std::vector<const model::Package*> App::CurrentVisible() const {
  std::vector<const model::Package*> visible;
  // In the AUR view, a live search swaps the local foreign-package listing for
  // the remote results. They are all AUR rows, so View::Browse applies no extra
  // view filter while still reusing the catalog's query + sort.
  if (state_.view == model::View::Aur && aur_search_active_) {
    visible = aur_results_.Visible(model::View::Browse, state_.query, state_.sort);
  } else if (state_.view == model::View::Collections) {
    // Collections: the picker shows no package rows (leaving `visible` empty); an
    // open collection shows its members resolved against the catalog.
    const std::vector<model::Collection>& collections = model::Collections();
    if (state_.active_collection >= 0 &&
        state_.active_collection < static_cast<int>(collections.size())) {
      visible = catalog_.VisibleInSet(collections[state_.active_collection].packages,
                                      state_.query, state_.sort);
    }
  } else {
    visible = catalog_.Visible(state_.view, state_.query, state_.sort);
  }

  // Repo filter (toggled with 'f'): narrow the list to a single source. Applied
  // as a post-filter so every path above stays untouched; harmless on the empty
  // picker path. C++17 erase/remove idiom keeps it allocation-free.
  if (state_.filter_active) {
    const model::Repo keep = state_.repo_filter;
    visible.erase(std::remove_if(visible.begin(), visible.end(),
                                 [keep](const model::Package* package) {
                                   return package->repo != keep;
                                 }),
                  visible.end());
  }
  return visible;
}

int App::CurrentRowCount() const {
  // The picker's rows are collections, not packages, so its row count comes from
  // the curated list rather than CurrentVisible (which is empty there).
  if (state_.view == model::View::Collections && state_.active_collection < 0) {
    return static_cast<int>(model::Collections().size());
  }
  return static_cast<int>(CurrentVisible().size());
}

void App::ResetSelection() {
  state_.selected_index = 0;
}

void App::MoveSelection(int delta) {
  const int row_count = CurrentRowCount();
  if (row_count == 0) {
    state_.selected_index = 0;
    return;
  }
  state_.selected_index = std::clamp(state_.selected_index + delta, 0, row_count - 1);
}

void App::SwitchView(model::View view) {
  state_.view = view;
  state_.status_message.clear();
  // Each visit to the AUR view starts on the local foreign packages; a fresh
  // Enter re-runs the live search.
  aur_search_active_ = false;
  // Entering Collections (or leaving it) always starts on the picker.
  state_.active_collection = -1;
  ResetSelection();
}

void App::OpenCollection() {
  const std::vector<model::Collection>& collections = model::Collections();
  if (state_.selected_index < 0 || state_.selected_index >= static_cast<int>(collections.size())) {
    return;
  }
  state_.active_collection = state_.selected_index;
  state_.status_message.clear();
  ResetSelection();
}

void App::CloseCollection() {
  // Return to the picker, re-selecting the collection the user was just in.
  const int previous = state_.active_collection;
  state_.active_collection = -1;
  state_.status_message.clear();
  state_.selected_index = std::max(0, previous);
}

void App::OpenFileLookup() {
  state_.file_lookup_open = true;
  state_.file_lookup_query.clear();
  state_.file_lookup_has_result = false;
  state_.file_lookup_owners.clear();
  state_.file_lookup_from_files_db = false;
  state_.file_lookup_note.clear();
  state_.file_lookup_shown_query.clear();
  state_.status_message.clear();
}

void App::RunFileLookup() {
  // The source does the local (-Qo) then files-db (-F) scan; copy its answer
  // into the plain display fields so AppState never touches the data/ types.
  const data::FileOwnerResult result = source_.FindFileOwner(state_.file_lookup_query);
  state_.file_lookup_has_result = true;
  state_.file_lookup_owners = result.owners;
  state_.file_lookup_from_files_db = result.from_files_db;
  state_.file_lookup_note = result.note;
  state_.file_lookup_shown_query = result.query;
}

void App::ToggleSort() {
  state_.sort = state_.sort == model::Sort::SizeDescending ? model::Sort::NameAscending
                                                           : model::Sort::SizeDescending;
  state_.status_message.clear();
}

void App::CycleRepoFilter() {
  // One step through OFF → each enabled repo stop → OFF. The pacman repos are
  // always present; AUR / Flatpak / Homebrew appear only when the user surfaced
  // them, so the filter never stops on a manager they turned off.
  std::vector<model::Repo> stops = {model::Repo::Core, model::Repo::Extra, model::Repo::Multilib};
  if (state_.managers.aur) {
    stops.push_back(model::Repo::Aur);
  }
  if (state_.managers.flatpak) {
    stops.push_back(model::Repo::Flatpak);
  }
  if (state_.managers.homebrew) {
    stops.push_back(model::Repo::Homebrew);
  }

  if (!state_.filter_active) {
    state_.filter_active = true;
    state_.repo_filter = stops.front();
  } else {
    const auto it = std::find(stops.begin(), stops.end(), state_.repo_filter);
    if (it == stops.end() || std::next(it) == stops.end()) {
      state_.filter_active = false;  // wrap back to OFF
    } else {
      state_.repo_filter = *std::next(it);
    }
  }
  state_.status_message.clear();
  ResetSelection();
}

void App::LoadDetailFor(const model::Package& package) {
  // Lazily query the source; file lists make this too heavy to load per row.
  state_.detail = source_.Describe(package);
  state_.detail_scroll = 0;
  state_.detail_link = -1;
  state_.detail_open = true;
  // AUR rows get the async trust-data fetch; the pane shows "fetching…" until
  // the worker answers (or instantly, from the session cache).
  if (package.repo == model::Repo::Aur) {
    MaybeFetchAurInfo(package.name);
  }
}

void App::OpenDetail() {
  const std::vector<const model::Package*> visible = CurrentVisible();
  if (state_.selected_index < 0 || state_.selected_index >= static_cast<int>(visible.size())) {
    return;
  }
  detail_trail_.clear();
  LoadDetailFor(*visible[state_.selected_index]);
  state_.status_message.clear();
}

void App::CloseDetail() {
  state_.detail_open = false;
  state_.detail_link = -1;
  detail_trail_.clear();
}

void App::CycleDetailLink(int direction) {
  const std::vector<std::string> links = ui::DetailLinkNames(state_.detail);
  if (links.empty()) {
    return;
  }
  const int count = static_cast<int>(links.size());
  state_.detail_link = state_.detail_link < 0
                           ? (direction > 0 ? 0 : count - 1)
                           : (state_.detail_link + direction + count) % count;
  // Keep the selected link on screen; the render loop clamps the rest.
  state_.detail_scroll =
      ui::RevealDetailLink(state_.detail, state_.detail_scroll, state_.detail_link);
  state_.status_message.clear();
}

void App::OpenLinkedDetail() {
  const std::vector<std::string> links = ui::DetailLinkNames(state_.detail);
  if (state_.detail_link < 0 || state_.detail_link >= static_cast<int>(links.size())) {
    return;
  }
  const std::string& name = links[state_.detail_link];
  const model::Package* package = FindPackage(name);
  if (package == nullptr) {
    // Virtual provides ("libfoo.so") and packages outside the databases land
    // here; say so instead of silently doing nothing.
    state_.status_message = name + " · not in the local catalog";
    return;
  }
  detail_trail_.push_back(state_.detail.name);
  LoadDetailFor(*package);
  state_.status_message.clear();
}

void App::DetailBack() {
  // Walk back to the most recent trail entry that still resolves (a jump target
  // may have been removed by an intervening transaction); nowhere to go closes
  // the pane, so Backspace on a fresh detail behaves like Escape.
  while (!detail_trail_.empty()) {
    const std::string previous = detail_trail_.back();
    detail_trail_.pop_back();
    if (const model::Package* package = FindPackage(previous)) {
      LoadDetailFor(*package);
      return;
    }
  }
  CloseDetail();
}

void App::ScrollDetail(int delta) {
  // The viewport-aware upper bound is clamped in the render loop, where the
  // terminal height is known; here we only keep it from going negative.
  state_.detail_scroll = std::max(0, state_.detail_scroll + delta);
}

void App::TriggerActionOnSelection() {
  const std::vector<const model::Package*> visible = CurrentVisible();
  if (state_.selected_index < 0 || state_.selected_index >= static_cast<int>(visible.size())) {
    return;
  }
  const model::Package& package = *visible[state_.selected_index];
  const system::Action action =
      package.installed ? system::Action::Remove : system::Action::Install;

  if (state_.read_only) {
    const char* verb = package.installed ? "remove" : "install";
    state_.status_message = "READ-ONLY · would " + std::string(verb) + " " + package.name;
    return;
  }

  std::string error;
  std::string command =
      system::BuildCommandLine(action, package.name, ManagerForRepo(package.repo), tools_, error);
  if (command.empty()) {
    state_.status_message = error;
    return;
  }

  PendingTransaction transaction{action, package.name, std::move(command)};

  // Partial-upgrade guard: installing a lone package with -S while system
  // updates wait can leave a half-upgraded system. Prompt before committing.
  // Pacman updates only: a stale flatpak carries no partial-upgrade risk.
  if (action == system::Action::Install && catalog_.PacmanUpdateCount() > 0) {
    OpenPartialUpgradeConfirm(std::move(transaction));
    return;
  }

  // Removal-cascade preview: surface what `pacman -Rs` will also drag out (and
  // how much it reclaims) before committing. Only prompts when the closure is
  // more than the target itself; unavailable previews fall through untouched.
  if (action == system::Action::Remove) {
    const data::RemovalPreview preview = source_.PreviewRemoval(package);
    if (preview.available && preview.packages.size() > 1) {
      const int extra = static_cast<int>(preview.packages.size()) - 1;
      state_.confirm_title = "REMOVAL CASCADE";
      state_.confirm_message = "Removing " + package.name + " also removes " +
                               std::to_string(extra) +
                               (extra == 1 ? " dependency no longer needed:"
                                           : " dependencies no longer needed:");
      // List the freed dependencies (the target is named in the message), capped
      // so a huge cascade can't overflow the modal; the remainder is summarized.
      constexpr int kMaxCascadeItems = 40;
      state_.confirm_items.clear();
      const int shown = std::min(extra, kMaxCascadeItems);
      for (int i = 1; i <= shown; ++i) {  // index 0 is the target
        state_.confirm_items.push_back(preview.packages[static_cast<size_t>(i)]);
      }
      if (shown < extra) {
        state_.confirm_items.push_back("… and " + std::to_string(extra - shown) + " more");
      }
      state_.confirm_footnote = "reclaims " + model::FormatSize(preview.reclaimed_bytes) +
                                " · enter proceed · esc cancel";
      OpenConfirm(std::move(transaction), /*is_partial_upgrade=*/false);
      return;
    }
  }

  // Unguarded: queue it and leave the loop; the work happens in the restored
  // terminal so sudo and pacman can prompt the user. See ApplyPendingTransaction.
  pending_transaction_ = std::move(transaction);
  screen_.Exit();
}

void App::OpenConfirm(PendingTransaction transaction, bool is_partial_upgrade,
                      bool consumes_marks) {
  confirm_pending_ = std::move(transaction);
  confirm_is_partial_upgrade_ = is_partial_upgrade;
  confirm_consumes_marks_ = consumes_marks;
  state_.confirm_open = true;
  state_.status_message.clear();
}

void App::OpenPartialUpgradeConfirm(PendingTransaction transaction) {
  const int updates = catalog_.PacmanUpdateCount();
  state_.confirm_title = "PARTIAL-UPGRADE WARNING";
  state_.confirm_message =
      std::to_string(updates) + (updates == 1 ? " update pending. " : " updates pending. ") +
      "Installing without a full upgrade (-Syu) risks a partial-upgrade breakage.";
  state_.confirm_items.clear();
  state_.confirm_footnote = "enter proceed · u update first · esc cancel";
  OpenConfirm(std::move(transaction), /*is_partial_upgrade=*/true);
}

void App::ConfirmProceed() {
  // Commit the stashed transaction and leave the loop to apply it. Any marks are
  // consumed now, not when the prompt opened, so a cancel kept them intact (a
  // no-op for the single-row install/remove prompts, which carry no marks).
  // Maintenance prompts (cache clean) have nothing to do with marks and spare
  // them.
  if (confirm_consumes_marks_) {
    state_.marked.clear();
  }
  pending_transaction_ = std::move(confirm_pending_);
  confirm_pending_.reset();
  state_.confirm_open = false;
  state_.confirm_items.clear();
  screen_.Exit();
}

void App::ConfirmCancel() {
  confirm_pending_.reset();
  state_.confirm_open = false;
  state_.confirm_items.clear();
  state_.status_message = "cancelled";
}

const model::Package* App::FindPackage(const std::string& name) const {
  for (const model::Package& package : catalog_.All()) {
    if (package.name == name) {
      return &package;
    }
  }
  // A marked name might belong to a live AUR result not present in the catalog.
  for (const model::Package& package : aur_results_.All()) {
    if (package.name == name) {
      return &package;
    }
  }
  return nullptr;
}

void App::ToggleMark() {
  const std::vector<const model::Package*> visible = CurrentVisible();
  if (state_.selected_index < 0 || state_.selected_index >= static_cast<int>(visible.size())) {
    return;
  }
  const std::string& name = visible[state_.selected_index]->name;
  if (!state_.marked.insert(name).second) {
    state_.marked.erase(name);
  }
  state_.status_message.clear();
  // Advance so marking a run of packages is a single repeated keystroke.
  MoveSelection(1);
}

void App::ToggleMarkAll() {
  const std::vector<const model::Package*> visible = CurrentVisible();
  if (visible.empty()) {
    return;  // nothing to mark (empty view or the collections picker)
  }
  // If every visible row is already marked, this is the undo; otherwise mark
  // the lot. One keystroke turns the Orphans view into "remove all orphans".
  bool all_marked = true;
  for (const model::Package* package : visible) {
    if (state_.marked.count(package->name) == 0) {
      all_marked = false;
      break;
    }
  }
  for (const model::Package* package : visible) {
    if (all_marked) {
      state_.marked.erase(package->name);
    } else {
      state_.marked.insert(package->name);
    }
  }
  state_.status_message.clear();  // the footer's "N marked" chip takes over
}

void App::TriggerCacheClean() {
  if (state_.read_only) {
    state_.status_message = "READ-ONLY · would clean the package cache";
    return;
  }

  std::string error;
  std::string command = system::BuildCommandLine(system::Action::CleanCache, /*package_name=*/"",
                                                 system::Manager::Pacman, tools_, error);
  if (command.empty()) {
    state_.status_message = error;
    return;
  }

  // Deleting cached packages costs the ability to downgrade to them, so it gets
  // the same pre-commit prompt as a removal cascade. It has nothing to do with
  // the marked set, which survives the confirmation either way.
  state_.confirm_title = "CACHE CLEAN";
  state_.confirm_message =
      "paccache -r removes cached versions of each package, keeping the 3 most recent." +
      (state_.pacman_cache_bytes > 0
           ? " The cache currently holds " + model::FormatSize(state_.pacman_cache_bytes) + "."
           : std::string());
  state_.confirm_items.clear();
  state_.confirm_footnote = "enter proceed · esc cancel";
  OpenConfirm(PendingTransaction{system::Action::CleanCache, "package cache", std::move(command)},
              /*is_partial_upgrade=*/false, /*consumes_marks=*/false);
}

void App::TriggerPacdiff() {
  if (state_.pacnew_count == 0) {
    state_.status_message = "no unmerged config files · nothing for pacdiff";
    return;
  }
  if (state_.read_only) {
    state_.status_message = "READ-ONLY · would merge " +
                            std::to_string(state_.pacnew_count) + " config files";
    return;
  }

  std::string error;
  std::string command = system::BuildCommandLine(system::Action::MergeConfigs, /*package_name=*/"",
                                                 system::Manager::Pacman, tools_, error);
  if (command.empty()) {
    state_.status_message = error;
    return;
  }

  // pacdiff is its own interactive session (view / merge / skip per file), so
  // no extra confirmation here - just the usual suspend-to-terminal dance.
  pending_transaction_ =
      PendingTransaction{system::Action::MergeConfigs, "config files", std::move(command)};
  screen_.Exit();
}

void App::TriggerDatabaseRefresh() {
  if (state_.read_only) {
    state_.status_message = "READ-ONLY · would refresh the package databases";
    return;
  }

  std::string error;
  std::string command = system::BuildCommandLine(system::Action::RefreshDatabases,
                                                 /*package_name=*/"", system::Manager::Pacman,
                                                 tools_, error);
  if (command.empty()) {
    state_.status_message = error;
    return;
  }

  // No confirmation: -Sy changes no packages, and the user asked for exactly
  // this. The catalog reload after the transaction re-diffs installed versions
  // against the fresh databases, so the Updates view fills in on return.
  pending_transaction_ =
      PendingTransaction{system::Action::RefreshDatabases, "databases", std::move(command)};
  screen_.Exit();
}

void App::ApplyMarked() {
  // Enter with nothing marked keeps the original single-row behaviour.
  if (state_.marked.empty()) {
    TriggerActionOnSelection();
    return;
  }

  if (state_.read_only) {
    state_.status_message =
        "READ-ONLY · would apply " + std::to_string(state_.marked.size()) + " marked packages";
    return;
  }

  // Partition the marked set by the action each package implies, tracking which
  // managers are involved. pacman and AUR share one invocation (pacman -Rs for
  // removal, the helper for installs), so the only hard split is flatpak vs the
  // native managers - a batch can't mix those, and can't mix installs + removes.
  std::vector<std::string> installs;
  std::vector<std::string> removes;
  bool has_flatpak = false;
  bool has_native = false;
  bool any_aur = false;
  for (const std::string& name : state_.marked) {
    const model::Package* package = FindPackage(name);
    if (package == nullptr) {
      continue;  // marked something no longer present; skip it
    }
    const system::Manager manager = ManagerForRepo(package->repo);
    if (manager == system::Manager::Flatpak) {
      has_flatpak = true;
    } else {
      has_native = true;
      any_aur = any_aur || manager == system::Manager::Aur;
    }
    (package->installed ? removes : installs).push_back(name);
  }

  if (!installs.empty() && !removes.empty()) {
    state_.status_message = "marked set mixes installs and removes - apply one kind at a time";
    return;
  }
  if (has_flatpak && has_native) {
    state_.status_message = "marked set mixes flatpak and pacman/AUR - apply one manager at a time";
    return;
  }
  if (installs.empty() && removes.empty()) {
    state_.status_message = "marked packages are no longer available";
    return;
  }

  const bool installing = !installs.empty();
  const std::vector<std::string>& names = installing ? installs : removes;
  const system::Action action = installing ? system::Action::Install : system::Action::Remove;
  // An AUR helper installs repo + AUR together; removals and pure-repo installs
  // go through pacman; flatpak has its own tool.
  const system::Manager manager = has_flatpak             ? system::Manager::Flatpak
                                  : (installing && any_aur) ? system::Manager::Aur
                                                          : system::Manager::Pacman;

  std::string error;
  std::string command = system::BuildBatchCommandLine(action, names, manager, tools_, error);
  if (command.empty()) {
    state_.status_message = error;
    return;
  }

  // Marks are consumed only when the batch actually commits (the direct path
  // below, or ConfirmProceed), never when a prompt merely opens - so cancelling
  // a partial-upgrade warning leaves the marked set intact for another attempt.
  const std::string label = std::to_string(names.size()) + " packages";
  PendingTransaction transaction{action, label, std::move(command)};

  // A marked install batch is guarded by the same partial-upgrade prompt as a
  // single install; removals apply straight through (no cascade preview on
  // batches, which are already an explicit multi-package choice).
  if (installing && catalog_.PacmanUpdateCount() > 0) {
    OpenPartialUpgradeConfirm(std::move(transaction));
    return;
  }
  state_.marked.clear();
  pending_transaction_ = std::move(transaction);
  screen_.Exit();
}

void App::ToggleInstallReason() {
  const std::vector<const model::Package*> visible = CurrentVisible();
  if (state_.selected_index < 0 || state_.selected_index >= static_cast<int>(visible.size())) {
    return;
  }
  const model::Package& package = *visible[state_.selected_index];
  if (!package.installed) {
    state_.status_message = "r · only installed packages have an install reason";
    return;
  }

  // Explicit -> mark as dependency, and vice versa; the flip is what makes the
  // orphan set trustworthy (a mislabelled explicit package hides as non-orphan).
  const bool to_dependency = package.explicit_install;
  const system::Action action =
      to_dependency ? system::Action::SetDependency : system::Action::SetExplicit;
  const char* reason = to_dependency ? "dependency" : "explicit";

  if (state_.read_only) {
    state_.status_message =
        "READ-ONLY · would mark " + package.name + " as " + std::string(reason);
    return;
  }

  std::string error;
  // Reason changes are always a local pacman -D op, so the manager is Pacman.
  std::string command =
      system::BuildCommandLine(action, package.name, system::Manager::Pacman, tools_, error);
  if (command.empty()) {
    state_.status_message = error;
    return;
  }

  pending_transaction_ = PendingTransaction{action, package.name, std::move(command)};
  screen_.Exit();
}

void App::TriggerSystemUpdate() {
  if (state_.read_only) {
    state_.status_message = "READ-ONLY · would update system packages";
    return;
  }

  // Flatpak apps update through their own CLI; when any are pending, chain a
  // `flatpak update` behind the native upgrade so "update" means everything.
  const bool flatpak_pending =
      catalog_.CountForView(model::View::Updates) > catalog_.PacmanUpdateCount();
  std::string error;
  std::string command =
      system::BuildCommandLine(system::Action::Update, /*package_name=*/"",
                               system::Manager::Pacman, tools_, error, flatpak_pending);
  if (command.empty()) {
    state_.status_message = error;
    return;
  }

  // Same queue-then-suspend dance as a single-package transaction: the upgrade
  // runs in the restored terminal so pacman / the helper can prompt. The "system"
  // label stands in for the package name in the header and status line.
  pending_transaction_ = PendingTransaction{system::Action::Update, "system", std::move(command)};
  screen_.Exit();
}

void App::ExportPackageList() {
  // Gather the explicitly-installed set - the names `pacman -Qqe` prints - and
  // sort them so the file is stable run to run and diffs cleanly.
  std::vector<std::string> names;
  for (const model::Package& package : catalog_.All()) {
    if (package.installed && package.explicit_install) {
      names.push_back(package.name);
    }
  }
  std::sort(names.begin(), names.end());

  if (state_.read_only) {
    state_.status_message =
        "READ-ONLY · would export " + std::to_string(names.size()) + " packages";
    return;
  }

  const std::string path = config::DefaultPackageListPath();
  // Best-effort create the parent dir; a failure here still surfaces below when
  // the open fails, so no need to branch on the error_code.
  std::error_code error;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);

  std::ofstream out(path);
  if (!out) {
    state_.status_message = "could not write package list to " + path;
    return;
  }
  for (const std::string& name : names) {
    out << name << '\n';
  }
  state_.status_message = "exported " + std::to_string(names.size()) + " packages → " + path;
}

void App::ImportPackageList() {
  const std::string path = config::DefaultPackageListPath();
  std::ifstream file(path);
  if (!file) {
    state_.status_message = "no package list at " + path + " · press " +
                            std::string(1, state_.keys.export_list) + " to create one";
    return;
  }

  // One name per line, skipping blanks and '#' comments (leading/trailing
  // whitespace tolerated). File order is kept; the installer tolerates dupes.
  std::vector<std::string> names;
  std::string line;
  while (std::getline(file, line)) {
    const auto first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
      continue;
    }
    const auto last = line.find_last_not_of(" \t\r\n");
    const std::string name = line.substr(first, last - first + 1);
    if (name[0] == '#') {
      continue;
    }
    names.push_back(name);
  }

  // Candidates are the names not already installed. A name absent from the
  // catalog is still a candidate - the file may predate a fresh install.
  std::unordered_set<std::string> installed;
  for (const model::Package& package : catalog_.All()) {
    if (package.installed) {
      installed.insert(package.name);
    }
  }
  std::vector<std::string> missing;
  for (const std::string& name : names) {
    if (installed.count(name) == 0) {
      missing.push_back(name);
    }
  }

  if (missing.empty()) {
    state_.status_message =
        "all " + std::to_string(names.size()) + " packages already installed";
    return;
  }
  if (state_.read_only) {
    state_.status_message =
        "READ-ONLY · would import " + std::to_string(missing.size()) + " packages";
    return;
  }

  // One batch install for the whole set. With a helper present it installs repo +
  // AUR together (matching ApplyMarked); otherwise pacman handles the repo names.
  const system::Manager manager =
      tools_.aur_helper.empty() ? system::Manager::Pacman : system::Manager::Aur;
  std::string error;
  std::string command =
      system::BuildBatchCommandLine(system::Action::Install, missing, manager, tools_, error);
  if (command.empty()) {
    state_.status_message = error;
    return;
  }

  // Route through the same install path as a marked batch: guard behind the
  // partial-upgrade prompt when updates wait, otherwise queue and leave the loop.
  const std::string label = std::to_string(missing.size()) + " packages";
  PendingTransaction transaction{system::Action::Install, label, std::move(command)};
  if (catalog_.PacmanUpdateCount() > 0) {
    OpenPartialUpgradeConfirm(std::move(transaction));
    return;
  }
  pending_transaction_ = std::move(transaction);
  screen_.Exit();
}

void App::LaunchAurSearch() {
  if (aur_searching_) {
    return;
  }
  // The AUR RPC rejects terms under two characters; mirror that locally so a
  // stray Enter on an empty or single-char box never produces a request.
  if (state_.query.size() < 2) {
    state_.status_message = "type at least 2 characters to search the AUR";
    return;
  }
  aur_term_ = state_.query;

  // Serve a term we've already fetched this session straight from the memo, so
  // re-searching or toggling views never re-hits the AUR.
  const auto cached = aur_cache_.find(aur_term_);
  if (cached != aur_cache_.end()) {
    ShowAurResults(cached->second, /*from_cache=*/true);
    return;
  }

  // A previous worker has already signalled completion if we got here, so this
  // join returns immediately; it just reclaims the std::thread before reuse.
  if (aur_worker_.joinable()) {
    aur_worker_.join();
  }

  aur_searching_ = true;
  state_.status_message = "searching AUR for " + aur_term_ + "…";

  const std::string term = aur_term_;
  aur_worker_ = std::thread([this, term] {
    std::string error;
    std::vector<model::Package> packages = system::SearchAur(term, error);
    {
      std::lock_guard<std::mutex> lock(aur_mutex_);
      aur_pending_ = std::move(packages);
      aur_pending_error_ = std::move(error);
      aur_ready_ = true;
    }
    // Wake the loop so AdoptAurResults runs on the UI thread.
    screen_.PostEvent(kAurResultsReady);
  });
}

void App::AdoptAurResults() {
  std::vector<model::Package> packages;
  std::string error;
  {
    std::lock_guard<std::mutex> lock(aur_mutex_);
    if (!aur_ready_) {
      return;
    }
    packages = std::move(aur_pending_);
    error = std::move(aur_pending_error_);
    aur_ready_ = false;
  }
  aur_searching_ = false;

  if (!error.empty()) {
    state_.status_message = error;
    return;
  }

  // Remember the raw fetch so the same term is free for the rest of the session.
  aur_cache_[aur_term_] = packages;
  ShowAurResults(packages, /*from_cache=*/false);
}

void App::ShowAurResults(const std::vector<model::Package>& fetched, bool from_cache) {
  std::vector<model::Package> packages = fetched;

  // Tag results already present on the system so the action button and any
  // transaction treat them as removable, matching the local views.
  std::unordered_set<std::string> installed;
  for (const model::Package& package : catalog_.All()) {
    if (package.installed) {
      installed.insert(package.name);
    }
  }
  for (model::Package& package : packages) {
    package.installed = installed.count(package.name) != 0;
  }

  const size_t count = packages.size();
  aur_results_.SetPackages(std::move(packages));
  aur_search_active_ = true;
  ResetSelection();

  if (count == 0) {
    state_.status_message = "no AUR packages match " + aur_term_;
    return;
  }
  state_.status_message = std::to_string(count) +
                          (count == 1 ? " AUR result for " : " AUR results for ") + aur_term_ +
                          (from_cache ? " · cached" : "");
}

void App::ApplyAurInfoToDetail(const system::AurInfo& info) {
  state_.detail.aur_info_pending = false;
  if (!info.found) {
    // A locally-built or renamed foreign package: the AUR simply doesn't know it.
    state_.detail.aur_info_error = "not found on the AUR · local or renamed package";
    return;
  }
  state_.detail.aur_info_loaded = true;
  state_.detail.aur_info_error.clear();
  state_.detail.aur_votes = info.votes;
  state_.detail.aur_popularity = info.popularity;
  state_.detail.aur_maintainer = info.maintainer;
  state_.detail.aur_last_modified = info.last_modified;
  state_.detail.aur_out_of_date = info.out_of_date;
}

void App::MaybeFetchAurInfo(const std::string& name) {
  const auto cached = aur_info_cache_.find(name);
  if (cached != aur_info_cache_.end()) {
    ApplyAurInfoToDetail(cached->second);
    return;
  }
  // One fetch at a time. If a different package's fetch is still in flight,
  // leave this pane pending; AdoptAurInfo re-kicks the fetch for whatever pane
  // is open once the current one lands.
  state_.detail.aur_info_pending = true;
  if (aur_info_fetching_) {
    return;
  }
  if (aur_info_worker_.joinable()) {
    aur_info_worker_.join();
  }
  aur_info_fetching_ = true;
  aur_info_worker_ = std::thread([this, name] {
    std::string error;
    system::AurInfo info = system::FetchAurInfo(name, error);
    {
      std::lock_guard<std::mutex> lock(aur_info_mutex_);
      aur_info_pending_result_ = std::move(info);
      aur_info_pending_error_ = std::move(error);
      aur_info_pending_name_ = name;
      aur_info_ready_ = true;
    }
    screen_.PostEvent(kAurInfoReady);
  });
}

void App::AdoptAurInfo() {
  system::AurInfo info;
  std::string error;
  std::string name;
  {
    std::lock_guard<std::mutex> lock(aur_info_mutex_);
    if (!aur_info_ready_) {
      return;
    }
    info = std::move(aur_info_pending_result_);
    error = std::move(aur_info_pending_error_);
    name = std::move(aur_info_pending_name_);
    aur_info_ready_ = false;
  }
  aur_info_fetching_ = false;

  // Memoize answers (including "not on the AUR") but never errors, so a
  // network hiccup retries on the next open instead of sticking for the session.
  if (error.empty()) {
    aur_info_cache_[name] = info;
  }

  if (!state_.detail_open) {
    return;
  }
  if (state_.detail.name == name) {
    if (error.empty()) {
      ApplyAurInfoToDetail(info);
    } else {
      state_.detail.aur_info_pending = false;
      state_.detail.aur_info_error = error;
    }
    return;
  }
  // The user has already jumped to a different AUR package; its fetch was
  // deferred while this one was in flight, so kick it now.
  if (state_.detail.repo == model::Repo::Aur && state_.detail.aur_info_pending &&
      !state_.detail.aur_info_loaded) {
    MaybeFetchAurInfo(state_.detail.name);
  }
}

void App::ApplyPendingTransaction() {
  const PendingTransaction transaction = *pending_transaction_;
  pending_transaction_.reset();

  std::string header;
  std::string success;
  switch (transaction.action) {
    case system::Action::Install:
      header = "Installing " + transaction.package_name;
      success = "installed " + transaction.package_name;
      break;
    case system::Action::Remove:
      header = "Removing " + transaction.package_name;
      success = "removed " + transaction.package_name;
      break;
    case system::Action::Update:
      header = "Updating " + transaction.package_name;
      success = transaction.package_name + " up to date";
      break;
    case system::Action::SetExplicit:
      header = "Marking " + transaction.package_name + " as explicit";
      success = transaction.package_name + " now explicit";
      break;
    case system::Action::SetDependency:
      header = "Marking " + transaction.package_name + " as a dependency";
      success = transaction.package_name + " now a dependency";
      break;
    case system::Action::CleanCache:
      header = "Cleaning the package cache";
      success = "package cache cleaned";
      break;
    case system::Action::MergeConfigs:
      header = "Merging config files (pacdiff)";
      success = "config merge session finished";
      break;
    case system::Action::RefreshDatabases:
      header = "Refreshing the package databases (pacman -Sy)";
      success = "databases refreshed";
      break;
  }
  const int status = system::RunInTerminal(transaction.command_line, header);

  // The system changed: reload from the source, re-measure the footer/card
  // indicators the command may have moved (cache size, pacnew count, sync age),
  // and keep the selection valid.
  catalog_.SetPackages(source_.LoadPackages());
  RefreshSystemIndicators();
  ResetSelection();

  // A refresh exists to answer "what's actually pending?" - say so directly,
  // now that the reloaded catalog has re-diffed against the fresh databases.
  if (transaction.action == system::Action::RefreshDatabases && status == 0) {
    const int updates = catalog_.CountForView(model::View::Updates);
    success = updates > 0 ? "databases refreshed · " + std::to_string(updates) +
                                (updates == 1 ? " update available" : " updates available")
                          : "databases refreshed · system up to date";
  }

  if (status == 0) {
    state_.status_message = success;
  } else {
    state_.status_message = "command exited " + std::to_string(status) + " · " +
                            transaction.package_name + " unchanged";
  }
}

bool App::HandleSearchEvent(const Event& event) {
  if (event == Event::Escape || event == Event::Return) {
    state_.search_focused = false;
    // Enter in the AUR view submits the term to the live RPC search; Escape and
    // the other views just defocus, keeping the as-you-type local filter.
    if (event == Event::Return && state_.view == model::View::Aur) {
      LaunchAurSearch();
    }
    return true;
  }
  if (event == Event::Backspace) {
    if (!state_.query.empty()) {
      state_.query.pop_back();
      ResetSelection();
    }
    return true;
  }
  if (event == Event::ArrowDown) {
    MoveSelection(1);
    return true;
  }
  if (event == Event::ArrowUp) {
    MoveSelection(-1);
    return true;
  }
  if (event.is_character()) {
    state_.query += event.character();
    ResetSelection();
    return true;
  }
  return false;
}

bool App::HandleMouse(Event event) {
  const ftxui::Mouse& mouse = event.mouse();
  const bool wheel_up = mouse.button == ftxui::Mouse::WheelUp;
  const bool wheel_down = mouse.button == ftxui::Mouse::WheelDown;

  // The floating modals swallow clicks like they swallow keys; none of them
  // scrolls, so there is nothing for the wheel to do either.
  if (state_.help_open || state_.confirm_open || state_.file_lookup_open) {
    return true;
  }

  if (state_.detail_open) {
    if (wheel_up || wheel_down) {
      ScrollDetail(wheel_down ? 3 : -3);
    }
    return true;  // clicks inside the pane do nothing (links cycle with Tab)
  }

  if (wheel_up || wheel_down) {
    MoveSelection(wheel_down ? 1 : -1);
    return true;
  }

  if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
    // Sidebar LIBRARY entries first (they share the nav order with the number
    // hotkeys), then the row under the cursor in the main list.
    const int nav = ui::NavEntryAt(mouse.x, mouse.y);
    if (nav >= 0 && nav < static_cast<int>(kViewHotkeys.size())) {
      state_.search_focused = false;
      SwitchView(kViewHotkeys[static_cast<size_t>(nav)].second);
      return true;
    }
    const int row = ui::ListIndexAt(state_, CurrentRowCount(), mouse.x, mouse.y);
    if (row >= 0) {
      state_.search_focused = false;
      state_.selected_index = row;
      return true;
    }
  }
  return false;
}

bool App::HandleEvent(const Event& event) {
  // A background AUR search finished: fold its results in regardless of focus.
  if (event == kAurResultsReady) {
    AdoptAurResults();
    return true;
  }
  // Likewise the detail pane's AUR info record.
  if (event == kAurInfoReady) {
    AdoptAurInfo();
    return true;
  }
  // Mouse events route through their own dispatcher, whatever has focus - a
  // click into the list also steals focus from the search box, like Escape.
  if (event.is_mouse()) {
    return HandleMouse(event);
  }

  // The controls popout is the topmost modal: while open it swallows every key,
  // and '?', Esc, or 'q' dismiss it.
  if (state_.help_open) {
    if (event == Event::Escape || event == Event::Character(state_.keys.quit) ||
        event == Event::Character(state_.keys.help)) {
      state_.help_open = false;
    }
    return true;
  }

  if (state_.search_focused) {
    return HandleSearchEvent(event);
  }

  // The pre-commit confirmation is intercepted early so it swallows every key:
  // enter commits the stashed transaction, esc / q cancels, and (only for the
  // partial-upgrade prompt) u drops the install and runs a full update instead.
  // Nothing else - navigation, views, opening detail/help - can happen while it
  // is up.
  if (state_.confirm_open) {
    if (event == Event::Return) {
      ConfirmProceed();
      return true;
    }
    if (confirm_is_partial_upgrade_ && event == Event::Character(state_.keys.update)) {
      // Drop the queued install and run the full upgrade instead. Any marks that
      // fed the batch are deliberately left intact, so once the catalog reloads
      // the footer's "N marked" prompt reappears and the user can re-apply them.
      confirm_pending_.reset();
      state_.confirm_open = false;
      state_.confirm_items.clear();
      TriggerSystemUpdate();
      return true;
    }
    if (event == Event::Escape || event == Event::Character(state_.keys.quit)) {
      ConfirmCancel();
      return true;
    }
    return true;  // modal swallows all other keys
  }

  // The file → package lookup is its own text-entry modal, intercepted early so
  // it swallows every key. Enter runs the lookup, Backspace edits, and printable
  // characters append to the query. Only Esc closes: 'q' is a legitimate part of
  // a path or command name, so it must stay typable here.
  if (state_.file_lookup_open) {
    if (event == Event::Escape) {
      state_.file_lookup_open = false;
      return true;
    }
    if (event == Event::Return) {
      RunFileLookup();
      return true;
    }
    if (event == Event::Backspace) {
      if (!state_.file_lookup_query.empty()) {
        state_.file_lookup_query.pop_back();
      }
      return true;
    }
    if (event.is_character()) {
      state_.file_lookup_query += event.character();
      return true;
    }
    return true;  // modal swallows everything else
  }

  // The help key opens the controls popout from anywhere outside the search box.
  if (event == Event::Character(state_.keys.help)) {
    state_.help_open = true;
    return true;
  }

  // The detail pane is a modal layer: while open it captures navigation and the
  // close keys, so the list underneath does not move. Tab cycles the
  // relationship links, Enter jumps to the selected one, Backspace walks back.
  if (state_.detail_open) {
    if (event == Event::Escape || event == Event::Character(state_.keys.quit) ||
        event == Event::Character(state_.keys.detail)) {
      CloseDetail();
      return true;
    }
    if (event == Event::Tab) {
      CycleDetailLink(1);
      return true;
    }
    if (event == Event::TabReverse) {
      CycleDetailLink(-1);
      return true;
    }
    if (event == Event::Return) {
      OpenLinkedDetail();
      return true;
    }
    if (event == Event::Backspace || event == Event::ArrowLeft) {
      DetailBack();
      return true;
    }
    if (event == Event::ArrowDown || event == Event::Character('j')) {
      ScrollDetail(1);
      return true;
    }
    if (event == Event::ArrowUp || event == Event::Character('k')) {
      ScrollDetail(-1);
      return true;
    }
    return true;  // fully modal: swallow every other key while the pane is open
  }

  // Collections is a two-level browse layered over the normal keys: the picker
  // drills in with Enter, and an open collection steps back out to the picker.
  if (state_.view == model::View::Collections) {
    if (state_.active_collection < 0) {
      if (event == Event::Return) {
        OpenCollection();
        return true;
      }
    } else if (event == Event::Escape || event == Event::Backspace ||
               event == Event::Character(state_.keys.collections_back) ||
               event == Event::ArrowLeft) {
      CloseCollection();
      return true;
    }
  }

  if (event == Event::Character(state_.keys.quit) || event == Event::Escape) {
    screen_.Exit();
    return true;
  }
  if (event == Event::Character(state_.keys.detail)) {
    OpenDetail();
    return true;
  }
  if (event == Event::Character(state_.keys.file_lookup)) {
    OpenFileLookup();
    return true;
  }
  if (event == Event::Character(state_.keys.search)) {
    state_.search_focused = true;
    state_.status_message.clear();
    return true;
  }
  if (event == Event::Character(state_.keys.sort)) {
    ToggleSort();
    return true;
  }
  if (event == Event::Character(state_.keys.filter)) {
    CycleRepoFilter();
    return true;
  }
  if (event == Event::Character(state_.keys.update)) {
    TriggerSystemUpdate();
    return true;
  }
  if (event == Event::Character(state_.keys.refresh)) {
    TriggerDatabaseRefresh();
    return true;
  }
  if (event == Event::Character(state_.keys.reason)) {
    ToggleInstallReason();
    return true;
  }
  if (event == Event::Character(state_.keys.mark_all)) {
    ToggleMarkAll();
    return true;
  }
  if (event == Event::Character(state_.keys.clean_cache)) {
    TriggerCacheClean();
    return true;
  }
  if (event == Event::Character(state_.keys.pacdiff)) {
    TriggerPacdiff();
    return true;
  }
  if (event == Event::Character(state_.keys.export_list)) {
    ExportPackageList();
    return true;
  }
  if (event == Event::Character(state_.keys.import_list)) {
    ImportPackageList();
    return true;
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    MoveSelection(1);
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    MoveSelection(-1);
    return true;
  }
  if (event == Event::Character(' ')) {
    ToggleMark();
    return true;
  }
  if (event == Event::Return) {
    ApplyMarked();
    return true;
  }
  for (const auto& [key, view] : kViewHotkeys) {
    if (event == Event::Character(key)) {
      SwitchView(view);
      return true;
    }
  }
  return false;
}

void App::Run() {
  auto renderer = ftxui::Renderer([this] {
    std::vector<const model::Package*> visible = CurrentVisible();
    // Keep the selection valid if the visible set shrank since the last frame.
    // The picker selects collections, not packages, so use the row-count helper.
    const int row_count = CurrentRowCount();
    state_.selected_index = row_count == 0 ? 0 : std::clamp(state_.selected_index, 0, row_count - 1);
    // Bound the detail scroll to its content now that the terminal height is known.
    if (state_.detail_open) {
      state_.detail_scroll = ui::ClampDetailScroll(state_.detail, state_.detail_scroll);
    }
    return ui::RenderApp(state_, catalog_, visible);
  });

  auto root = ftxui::CatchEvent(renderer, [this](const Event& event) { return HandleEvent(event); });

  // The loop exits either to quit or to apply a queued transaction. In the
  // latter case the alternate screen is now torn down, so pacman can run in the
  // plain terminal; afterwards we re-enter the loop with the reloaded catalog.
  while (true) {
    screen_.Loop(root);
    if (!pending_transaction_.has_value()) {
      break;
    }
    ApplyPendingTransaction();
  }
}

}  // namespace pacseek::app
