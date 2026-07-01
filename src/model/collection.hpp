// collection.hpp - curated package sets grouped by use case (gaming, creative
// work, development, …). Pure static data, no I/O: the catalog resolves each
// member name against the live package set, so a collection is just an ordered
// list of names with a label. Sits in the model layer like Package and Catalog.
#pragma once

#include <string>
#include <vector>

namespace pacseek::model {

// One curated bundle. `packages` are pacman/AUR package names; whether each is
// available or installed is answered by the Catalog at render time, so this type
// never touches the system.
struct Collection {
  std::string id;           // stable key, e.g. "gaming"
  std::string name;         // display label, e.g. "Gaming"
  std::string icon;         // single-width glyph for the nav/list
  std::string description;  // one-line summary
  std::vector<std::string> packages;
};

// The built-in, hand-curated collections, in display order. Returns a reference
// to a function-local static so callers can hold pointers into it for the life
// of the program.
const std::vector<Collection>& BuiltinCollections();

// The active collection set shown in the UI: the built-ins followed by any
// user-defined collections merged in at startup. Same lifetime guarantee as
// BuiltinCollections() - callers may hold pointers for the life of the program.
// Until SetUserCollections() runs it is exactly BuiltinCollections(). This layer
// still performs no I/O; parsing the user file lives in the config layer.
const std::vector<Collection>& Collections();

// Rebuilds the active set as the built-ins followed by `user`, in order. Call
// once at startup, before anything reads Collections(), because it reallocates
// the backing store and would invalidate previously handed-out pointers.
void SetUserCollections(std::vector<Collection> user);

}  // namespace pacseek::model
