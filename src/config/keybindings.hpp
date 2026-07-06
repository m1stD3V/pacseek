// keybindings.hpp - the single-character letter/symbol actions the user can
// rebind from the config file. A dependency-free leaf: plain `char` data with
// the current defaults, so both the config loader and the UI can read it without
// pulling in model/, UI, or FTXUI. Only the letter/symbol keys live here; the
// structural keys (enter / space / esc / arrows / digits 1-5) stay fixed.
#pragma once

namespace pacseek::config {

// The rebindable single-key actions and their built-in defaults. An untouched
// config yields exactly these, so default behaviour is unchanged. Conflicts (two
// actions on one key) are the user's problem - resolution is first-match-wins in
// the app's existing if-chain order.
struct Keybindings {
  char quit = 'q', detail = 'd', search = '/', sort = 's', update = 'u', reason = 'r',
       filter = 'f', file_lookup = 'o', help = '?', collections_back = 'h',
       export_list = 'x', import_list = 'i', mark_all = 'a', clean_cache = 'c',
       pacdiff = 'm', refresh = 'y';
};

}  // namespace pacseek::config
