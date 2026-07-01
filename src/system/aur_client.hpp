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
// returns the matches as packages tagged Repo::Aur. Sizes are left at 0 - the RPC
// reports none, since an AUR package has no size until it is built. Returns an
// empty vector on no matches; on failure also returns empty and fills `error`
// (network problem, malformed response, or a build without JSON support).
std::vector<model::Package> SearchAur(const std::string& term, std::string& error);

}  // namespace pacseek::system
