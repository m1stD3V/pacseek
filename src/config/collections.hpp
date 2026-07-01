// collections.hpp - optional user-defined package collections loaded from
// $XDG_CONFIG_HOME/pacseek/collections.ini (or ~/.config/pacseek/collections.ini),
// a sibling of config.ini so a single backed-up config folder carries a user's
// own collections to a fresh installation.
//
// The format is INI: one `[section]` per collection, whose id is the section
// name, with `name`, `icon`, `description`, and `packages` keys (packages is a
// comma-separated list). Parsing is strict where a mistake is unambiguous and
// knowable offline - a malformed collection is a hard error that names the
// offender and rejects the whole file - but tolerant of unknown keys so a newer
// file never breaks an older binary. Whether a listed package actually exists is
// NOT checked here: unresolved names simply render as "unavailable" in the UI,
// exactly like the AUR entries in the built-in collections, so no network is
// touched at startup. This layer depends only on model/, never on UI or FTXUI.
#pragma once

#include <string>
#include <vector>

#include "model/collection.hpp"

namespace pacseek::config {

// A single reason the collections file was rejected. `collection` is the section
// id it occurred in (empty when the problem precedes any section); `line` is the
// 1-based source line, or 0 when the error isn't tied to one line.
struct CollectionError {
  std::string collection;
  int line = 0;
  std::string message;
};

// The outcome of parsing. When `errors` is non-empty the file is malformed and
// `collections` is empty: callers must treat this as a hard failure rather than
// silently dropping the bad entries.
struct CollectionsResult {
  std::vector<model::Collection> collections;
  std::vector<CollectionError> errors;
};

// Resolves the collections file path alongside config.ini. Empty when neither
// XDG_CONFIG_HOME nor HOME is set.
std::string DefaultCollectionsPath();

// Parses an INI collections document. Pure and side-effect free (for testing).
CollectionsResult ParseCollections(const std::string& text);

// Loads collections from DefaultCollectionsPath(). An absent file yields an empty
// result with no errors, and drops a commented template so the format is
// discoverable (never overwriting an existing file). A present-but-malformed file
// yields errors describing every offender.
CollectionsResult LoadUserCollections();

// Renders one error as "collections.ini: <collection> (line N): <message>" for
// printing to stderr before exiting. Omits the parts that aren't known.
std::string FormatCollectionError(const CollectionError& error);

}  // namespace pacseek::config
