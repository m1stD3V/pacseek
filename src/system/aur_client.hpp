// aur_client.hpp - live search against the AUR's RPC interface. Like transaction,
// this module is an isolated OS/network side effect: it speaks only in a search
// term and a vector of model::Package, with no model/, UI, or FTXUI dependencies
// beyond the shared Package type.
//
// The fetch is synchronous and blocking; the app layer runs it on a worker thread
// so the TUI stays responsive. See App::LaunchAurSearch.
#pragma once

#include <string>
#include <vector>

#include "model/package.hpp"

namespace pacseek::system {

// Queries the AUR RPC "search" endpoint (by name + description) for `term` and
// returns the matches as packages tagged Repo::Aur, ordered by popularity so the
// widely-used package outranks a typosquat. Sizes are left at 0 - the RPC
// reports none, since an AUR package has no size until it is built; votes,
// popularity, and the out-of-date flag are carried instead. Returns an empty
// vector on no matches; on failure also returns empty and fills `error`
// (network problem, malformed response, or a build without JSON support).
std::vector<model::Package> SearchAur(const std::string& term, std::string& error);

// The trust-relevant slice of one package's AUR RPC "info" record - the fields
// people check before building something from the AUR.
struct AurInfo {
  bool found = false;          // the AUR knows this name
  int votes = -1;
  double popularity = -1.0;
  std::string maintainer;      // empty when found = orphaned on the AUR
  std::string version;
  std::string last_modified;   // formatted local date
  std::string first_submitted; // formatted local date
  std::string out_of_date;     // formatted date when flagged, else empty
};

// Fetches one package's info record (RPC type=info). Synchronous and blocking
// like SearchAur; the app layer runs it on a worker thread. On failure returns
// found=false and fills `error`; an unknown name is found=false with no error.
AurInfo FetchAurInfo(const std::string& name, std::string& error);

}  // namespace pacseek::system
