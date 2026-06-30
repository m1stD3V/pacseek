#include "app/app.hpp"

#include <algorithm>
#include <array>

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
    : catalog_(source.LoadPackages()), screen_(ftxui::ScreenInteractive::Fullscreen()) {
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
  const char* verb = package.installed ? "remove" : "install";
  if (state_.read_only) {
    state_.status_message = "READ-ONLY · would " + std::string(verb) + " " + package.name;
  } else {
    // Real transactions land here in a later milestone.
    state_.status_message = std::string(verb) + " not yet wired";
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
  screen_.Loop(root);
}

}  // namespace pacseek::app
