#include "app/app.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <unordered_set>
#include <utility>

#include <sys/statvfs.h>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>

#include "model/collection.hpp"
#include "system/aur_client.hpp"
#include "theme.hpp"
#include "ui/components.hpp"

namespace pacseek::app {

using ftxui::Event;

namespace {
// Maps the number-row keys to the LIBRARY nav order.
constexpr std::array<std::pair<char, model::View>, 5> kViewHotkeys = {{
    {'1', model::View::Browse},
    {'2', model::View::Installed},
    {'3', model::View::Updates},
    {'4', model::View::Aur},
    {'5', model::View::Collections},
}};

// Custom event an AUR worker thread posts to wake the loop once its results are
// ready; HandleEvent adopts them on the UI thread.
const ftxui::Event kAurResultsReady = ftxui::Event::Special("pacseek-aur-results");

// Which package manager owns a package, derived from its repo tag.
system::Manager ManagerForRepo(model::Repo repo) {
  switch (repo) {
    case model::Repo::Aur:
      return system::Manager::Aur;
    case model::Repo::Flatpak:
      return system::Manager::Flatpak;
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
}  // namespace

App::App(data::PackageSource& source, const config::Config& config)
    : source_(source),
      catalog_(source.LoadPackages()),
      tools_(system::DetectTools()),
      screen_(ftxui::ScreenInteractive::Fullscreen()) {
  state_.source_name = source.Name();
  state_.read_only = source.IsReadOnly();
  state_.disk_total_bytes = QueryDiskCapacityBytes();

  // Apply user configuration: seed the initial view/sort, and let an explicit
  // aur_helper override what DetectTools probed from PATH - but only if it is
  // actually installed, so a typo can't leave AUR installs running a missing
  // command. Otherwise keep the detected helper and surface a notice.
  state_.view = config.view;
  state_.sort = config.sort;
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
  // The worker captures `this` and posts to screen_; never let it outlive them.
  if (aur_worker_.joinable()) {
    aur_worker_.join();
  }
}

std::vector<const model::Package*> App::CurrentVisible() const {
  // In the AUR view, a live search swaps the local foreign-package listing for
  // the remote results. They are all AUR rows, so View::Browse applies no extra
  // view filter while still reusing the catalog's query + sort.
  if (state_.view == model::View::Aur && aur_search_active_) {
    return aur_results_.Visible(model::View::Browse, state_.query, state_.sort);
  }
  // Collections: the picker shows no package rows; an open collection shows its
  // members resolved against the catalog.
  if (state_.view == model::View::Collections) {
    const std::vector<model::Collection>& collections = model::Collections();
    if (state_.active_collection < 0 ||
        state_.active_collection >= static_cast<int>(collections.size())) {
      return {};
    }
    return catalog_.VisibleInSet(collections[state_.active_collection].packages, state_.query,
                                 state_.sort);
  }
  return catalog_.Visible(state_.view, state_.query, state_.sort);
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

void App::ToggleSort() {
  state_.sort = state_.sort == model::Sort::SizeDescending ? model::Sort::NameAscending
                                                           : model::Sort::SizeDescending;
  state_.status_message.clear();
}

void App::OpenDetail() {
  const std::vector<const model::Package*> visible = CurrentVisible();
  if (state_.selected_index < 0 || state_.selected_index >= static_cast<int>(visible.size())) {
    return;
  }
  // Lazily query the source; file lists make this too heavy to load per row.
  state_.detail = source_.Describe(*visible[state_.selected_index]);
  state_.detail_scroll = 0;
  state_.detail_open = true;
  state_.status_message.clear();
}

void App::CloseDetail() {
  state_.detail_open = false;
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

  // Queue it and leave the loop; the work happens in the restored terminal so
  // sudo and pacman can prompt the user. See ApplyPendingTransaction.
  pending_transaction_ = PendingTransaction{action, package.name, std::move(command)};
  screen_.Exit();
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

  // The marks are consumed by this apply; clear them so re-entry starts clean.
  const std::string label = std::to_string(names.size()) + " packages";
  state_.marked.clear();
  pending_transaction_ = PendingTransaction{action, label, std::move(command)};
  screen_.Exit();
}

void App::TriggerSystemUpdate() {
  if (state_.read_only) {
    state_.status_message = "READ-ONLY · would update system packages";
    return;
  }

  std::string error;
  std::string command =
      system::BuildCommandLine(system::Action::Update, /*package_name=*/"",
                               system::Manager::Pacman, tools_, error);
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
  }
  const int status = system::RunInTerminal(transaction.command_line, header);

  // The system changed: reload from the source and keep the selection valid.
  catalog_.SetPackages(source_.LoadPackages());
  ResetSelection();

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

bool App::HandleEvent(const Event& event) {
  // A background AUR search finished: fold its results in regardless of focus.
  if (event == kAurResultsReady) {
    AdoptAurResults();
    return true;
  }

  // The controls popout is the topmost modal: while open it swallows every key,
  // and '?', Esc, or 'q' dismiss it.
  if (state_.help_open) {
    if (event == Event::Escape || event == Event::Character('q') ||
        event == Event::Character('?')) {
      state_.help_open = false;
    }
    return true;
  }

  if (state_.search_focused) {
    return HandleSearchEvent(event);
  }

  // '?' opens the controls popout from anywhere outside the search box.
  if (event == Event::Character('?')) {
    state_.help_open = true;
    return true;
  }

  // The detail pane is a modal layer: while open it captures navigation and the
  // close keys, so the list underneath does not move.
  if (state_.detail_open) {
    if (event == Event::Escape || event == Event::Character('q') ||
        event == Event::Character('d')) {
      CloseDetail();
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
    return false;
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
               event == Event::Character('h') || event == Event::ArrowLeft) {
      CloseCollection();
      return true;
    }
  }

  if (event == Event::Character('q') || event == Event::Escape) {
    screen_.Exit();
    return true;
  }
  if (event == Event::Character('d')) {
    OpenDetail();
    return true;
  }
  if (event == Event::Character('/')) {
    state_.search_focused = true;
    state_.status_message.clear();
    return true;
  }
  if (event == Event::Character('s')) {
    ToggleSort();
    return true;
  }
  if (event == Event::Character('u')) {
    TriggerSystemUpdate();
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
