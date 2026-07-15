#include "app/first_run.hpp"

#include <algorithm>
#include <array>
#include <string>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "theme.hpp"

namespace pacseek::app {

namespace {

using namespace ftxui;
namespace palette = theme::color;

// One toggleable manager row. Pacman is not here - it is always on and shown as a
// fixed line - so the toggles are exactly the optional managers plus ascii mode.
struct Toggle {
  std::string label;
  std::string hint;  // "detected" / "not found" annotation, may be empty
  bool value;
};

// Kept intentionally ASCII-only so the setup screen renders identically on every
// terminal, before the glyph set the user is about to pick has taken effect.
Element ToggleRow(const Toggle& toggle, bool selected) {
  const Color box_color = toggle.value ? palette::Accent : palette::TextFaint;
  const std::string box = toggle.value ? "[x]" : "[ ]";
  Elements cells = {
      text(selected ? " > " : "   ") | color(palette::Accent) | bold,
      text(box + " ") | color(box_color) | bold,
      text(toggle.label) | color(selected ? palette::Text : palette::TextDim),
  };
  if (!toggle.hint.empty()) {
    cells.push_back(text("  " + toggle.hint) | color(palette::TextFaint));
  }
  Element row = hbox(std::move(cells));
  if (selected) {
    row = row | bgcolor(palette::RowSelectedBg);
  }
  return row;
}

}  // namespace

config::Config RunFirstRunSetup(const config::Config& base, const system::Tools& tools) {
  config::Config config = base;

  // Seed from detection: AUR is always offered on Arch; flatpak and homebrew
  // default on only when their CLI is present, but the user can force either.
  std::array<Toggle, 7> toggles = {{
      {"AUR - Arch User Repository", "", true},
      {"Flatpak", tools.has_flatpak ? "detected" : "not found on PATH", tools.has_flatpak},
      {"Homebrew", tools.has_brew ? "detected" : "not found on PATH", tools.has_brew},
      {"npm - global packages", tools.has_npm ? "detected" : "not found on PATH", tools.has_npm},
      {"bun - global packages", tools.has_bun ? "detected" : "not found on PATH", tools.has_bun},
      {"pnpm - global packages", tools.has_pnpm ? "detected" : "not found on PATH", tools.has_pnpm},
      {"ASCII glyphs - for alacritty / ghostty / tty consoles", "", false},
  }};
  constexpr int kContinueRow = 7;  // one past the toggles
  int selected = 0;

  auto screen = ScreenInteractive::Fullscreen();

  auto renderer = Renderer([&] {
    Elements body = {
        text("  WELCOME TO PACSEEK") | bold | color(palette::Accent),
        text(""),
        text("  Which package managers do you use? These govern which sources")
            | color(palette::TextDim),
        text("  load and which entries appear in the UI. You can change this")
            | color(palette::TextDim),
        text("  later in the config file (package_managers = ...).") | color(palette::TextDim),
        text(""),
        hbox({text("   [x] ") | color(palette::TextFaint) | bold,
              text("Pacman") | color(palette::TextDim),
              text("  always on") | color(palette::TextFaint)}),
    };
    for (int i = 0; i < static_cast<int>(toggles.size()); ++i) {
      body.push_back(ToggleRow(toggles[static_cast<size_t>(i)], selected == i));
    }
    body.push_back(text(""));
    // The continue affordance is just another selectable row.
    Element cont = hbox({
        text(selected == kContinueRow ? " > " : "   ") | color(palette::Accent) | bold,
        text(" Continue ") | bold |
            color(selected == kContinueRow ? palette::BgVoid : palette::Text) |
            bgcolor(selected == kContinueRow ? palette::Accent : palette::Sidebar),
    });
    body.push_back(cont);
    body.push_back(text(""));
    body.push_back(text("  j/k move   space toggle   enter continue") | color(palette::Label));

    Element panel = vbox(std::move(body)) | border | color(palette::ChromeDim) |
                    size(WIDTH, LESS_THAN, 72);
    return vbox({filler(), hbox({filler(), panel, filler()}), filler()}) | bgcolor(palette::Window);
  });

  auto with_events = CatchEvent(renderer, [&](const Event& event) {
    if (event == Event::ArrowDown || event == Event::Character('j')) {
      selected = std::min(selected + 1, kContinueRow);
      return true;
    }
    if (event == Event::ArrowUp || event == Event::Character('k')) {
      selected = std::max(selected - 1, 0);
      return true;
    }
    if (event == Event::Character(' ')) {
      if (selected < kContinueRow) {
        toggles[static_cast<size_t>(selected)].value = !toggles[static_cast<size_t>(selected)].value;
      }
      return true;
    }
    if (event == Event::Return) {
      screen.Exit();  // accept whatever is checked, from any row
      return true;
    }
    return false;
  });

  screen.Loop(with_events);

  config.aur_enabled = toggles[0].value;
  config.flatpak_enabled = toggles[1].value;
  config.homebrew_enabled = toggles[2].value;
  config.npm_enabled = toggles[3].value;
  config.bun_enabled = toggles[4].value;
  config.pnpm_enabled = toggles[5].value;
  config.ascii_glyphs = toggles[6].value;
  return config;
}

}  // namespace pacseek::app
