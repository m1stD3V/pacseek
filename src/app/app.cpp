#include "app/app.hpp"

#include <algorithm>
#include <array>
#include <string>

#include <sys/statvfs.h>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>

#include "ui/components.hpp"

namespace pacseek::app {

using ftxui::Event;

namespace {
// Maps the number-row keys to the LIBRARY nav order.
constexpr std::array<std::pair<char, model::View>, 4> kViewHotkeys = {{
    {'1', model::View::Browse},
    {'2', model::View::Installed},
    {'3', model::View::Updates},
    {'4', model::View::Aur},
}};

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

App::App(data::PackageSource& source)
    : source_(source),
      catalog_(source.LoadPackages()),
      tools_(system::DetectTools()),
      screen_(ftxui::ScreenInteractive::Fullscreen()) {
  state_.source_name = source.Name();
  state_.read_only = source.IsReadOnly();
  state_.disk_total_bytes = QueryDiskCapacityBytes();
}

std::vector<const model::Package*> App::CurrentVisible() const {
  return catalog_.Visible(state_.view, state_.query, state_.sort);
}

void App::ResetSelection() {
  state_.selected_index = 0;
}

void App::MoveSelection(int delta) {
  const int row_count = static_cast<int>(CurrentVisible().size());
  if (row_count == 0) {
    state_.selected_index = 0;
    return;
  }
  state_.selected_index = std::clamp(state_.selected_index + delta, 0, row_count - 1);
}

void App::SwitchView(model::View view) {
  state_.view = view;
  state_.status_message.clear();
  ResetSelection();
}

void App::ToggleSort() {
  state_.sort = state_.sort == model::Sort::SizeDescending ? model::Sort::NameAscending
                                                           : model::Sort::SizeDescending;
  state_.status_message.clear();
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
  const bool is_aur = package.repo == model::Repo::Aur;
  std::string command = system::BuildCommandLine(action, package.name, is_aur, tools_, error);
  if (command.empty()) {
    state_.status_message = error;
    return;
  }

  // Queue it and leave the loop; the work happens in the restored terminal so
  // sudo and pacman can prompt the user. See ApplyPendingTransaction.
  pending_transaction_ = PendingTransaction{action, package.name, std::move(command)};
  screen_.Exit();
}

void App::ApplyPendingTransaction() {
  const PendingTransaction transaction = *pending_transaction_;
  pending_transaction_.reset();

  const bool installing = transaction.action == system::Action::Install;
  const std::string header =
      (installing ? "Installing " : "Removing ") + transaction.package_name;
  const int status = system::RunInTerminal(transaction.command_line, header);

  // The system changed: reload from the source and keep the selection valid.
  catalog_.SetPackages(source_.LoadPackages());
  ResetSelection();

  if (status == 0) {
    state_.status_message =
        (installing ? "installed " : "removed ") + transaction.package_name;
  } else {
    state_.status_message = "command exited " + std::to_string(status) + " · " +
                            transaction.package_name + " unchanged";
  }
}

bool App::HandleSearchEvent(const Event& event) {
  if (event == Event::Escape || event == Event::Return) {
    state_.search_focused = false;
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
  if (state_.search_focused) {
    return HandleSearchEvent(event);
  }

  if (event == Event::Character('q') || event == Event::Escape) {
    screen_.Exit();
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
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    MoveSelection(1);
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    MoveSelection(-1);
    return true;
  }
  if (event == Event::Return || event == Event::Character(' ')) {
    TriggerActionOnSelection();
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
    const int row_count = static_cast<int>(visible.size());
    state_.selected_index = row_count == 0 ? 0 : std::clamp(state_.selected_index, 0, row_count - 1);
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
