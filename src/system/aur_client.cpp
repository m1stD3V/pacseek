#include "system/aur_client.hpp"

#include <algorithm>
#include <ctime>
#include <mutex>
#include <string>

#include <curl/curl.h>

#ifdef PACSEEK_HAVE_JSON
#include <nlohmann/json.hpp>
#endif

// Stamped in by CMake from the project version (see main.cpp); the fallback
// keeps this translation unit compiling outside the CMake toolchain.
#ifndef PACSEEK_VERSION
#define PACSEEK_VERSION "0.0.0-dev"
#endif

namespace pacseek::system {

namespace {

// AUR RPC v5. The query form (arg=…) keeps URL-escaping to the single term, and
// by=name-desc mirrors the app's local search, which matches name + description.
constexpr char kRpcBase[] = "https://aur.archlinux.org/rpc/?v=5&type=search&by=name-desc&arg=";

// The per-package info record behind the detail pane's AUR section.
constexpr char kRpcInfoBase[] = "https://aur.archlinux.org/rpc/?v=5&type=info&arg[]=";

// Upper bound on a response body. The largest legitimate RPC search result is
// well under a megabyte, so anything past this is a misbehaving endpoint; stop
// reading rather than buffering it into memory.
constexpr size_t kMaxResponseBytes = 8 * 1024 * 1024;

// curl_global_init is not thread-safe, so do it exactly once before any worker
// thread spins up an easy handle.
void EnsureCurlInitialized() {
  static std::once_flag once;
  std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

// libcurl write callback: appends the received bytes to the std::string in
// `out`, aborting the transfer (by under-consuming) once it exceeds the cap.
size_t AppendToString(char* data, size_t size, size_t nmemb, void* out) {
  const size_t bytes = size * nmemb;
  auto* body = static_cast<std::string*>(out);
  if (body->size() + bytes > kMaxResponseBytes) {
    return 0;  // curl turns this into CURLE_WRITE_ERROR
  }
  body->append(data, bytes);
  return bytes;
}

// GETs `url` into `body`. Returns true on a 2xx response; otherwise fills `error`.
bool HttpGet(const std::string& url, std::string& body, std::string& error) {
  EnsureCurlInitialized();
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    error = "could not initialize network client";
    return false;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AppendToString);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  // The only endpoint is the AUR RPC over TLS: pin the request AND any redirect
  // it serves to https, so a tampered response can never bounce the client to
  // another scheme, and cap the redirect chain.
#if CURL_AT_LEAST_VERSION(7, 85, 0)
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
  curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
  curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
  // This runs on a worker thread; without NOSIGNAL, curl's resolver timeouts
  // use SIGALRM, which is unsafe in a multithreaded process.
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  // Accept every encoding curl supports - compressed transfers keep the load on
  // the AUR's bandwidth down, per their usage guidelines.
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  // Identify the client so the AUR can attribute (and if ever needed, contact
  // about) traffic from pacseek, as their usage guidelines ask.
  curl_easy_setopt(curl, CURLOPT_USERAGENT,
                   "pacseek/" PACSEEK_VERSION " (+https://codeberg.org/m1stD3V/pacseek)");

  const CURLcode code = curl_easy_perform(curl);
  long http_status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
  curl_easy_cleanup(curl);

  if (code != CURLE_OK) {
    // Connectivity failures (no DNS, no route, timed out) almost always mean the
    // machine is offline rather than anything wrong with the request, so trade the
    // raw curl string for a plain-language hint. Everything else keeps the curl
    // detail, which is more useful for the rarer failure modes.
    switch (code) {
      case CURLE_COULDNT_RESOLVE_HOST:
      case CURLE_COULDNT_RESOLVE_PROXY:
      case CURLE_COULDNT_CONNECT:
      case CURLE_OPERATION_TIMEDOUT:
        error = "AUR unreachable · check your network connection";
        break;
      case CURLE_WRITE_ERROR:
        // Our own write callback aborts past the size cap (see AppendToString).
        error = "AUR response too large · aborted";
        break;
      default:
        error = std::string("AUR request failed: ") + curl_easy_strerror(code);
        break;
    }
    return false;
  }
  if (http_status < 200 || http_status >= 300) {
    error = "AUR returned HTTP " + std::to_string(http_status);
    return false;
  }
  return true;
}

// URL-escapes `text` for use as a single query argument. Empty on failure.
std::string UrlEscape(const std::string& text) {
  EnsureCurlInitialized();
  char* escaped = curl_easy_escape(nullptr, text.c_str(), static_cast<int>(text.size()));
  if (escaped == nullptr) {
    return {};
  }
  std::string out = escaped;
  curl_free(escaped);
  return out;
}

// Formats an AUR unix timestamp as a local "YYYY-MM-DD", or "" when absent.
std::string FormatAurDate(int64_t when) {
  if (when <= 0) {
    return {};
  }
  const std::time_t seconds = static_cast<std::time_t>(when);
  std::tm calendar;
  if (localtime_r(&seconds, &calendar) == nullptr) {
    return {};
  }
  char buffer[16];
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &calendar) == 0) {
    return {};
  }
  return std::string(buffer);
}

}  // namespace

#ifdef PACSEEK_HAVE_JSON

std::vector<model::Package> SearchAur(const std::string& term, std::string& error) {
  error.clear();
  std::vector<model::Package> results;

  // Escape the term so spaces or RPC metacharacters can't break out of the query.
  const std::string escaped = UrlEscape(term);
  if (escaped.empty() && !term.empty()) {
    error = "could not encode search term";
    return results;
  }
  const std::string url = std::string(kRpcBase) + escaped;

  std::string body;
  if (!HttpGet(url, body, error)) {
    return results;
  }

  nlohmann::json json = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
  if (json.is_discarded() || !json.is_object()) {
    error = "AUR returned a malformed response";
    return results;
  }
  // The RPC reports its own failures in-band with type "error".
  if (json.value("type", "") == "error") {
    error = "AUR: " + json.value("error", std::string("unknown error"));
    return results;
  }

  const auto items = json.find("results");
  if (items == json.end() || !items->is_array()) {
    return results;  // no "results" array means no matches
  }
  results.reserve(items->size());
  for (const auto& item : *items) {
    model::Package package;
    package.name = item.value("Name", std::string());
    if (package.name.empty()) {
      continue;
    }
    package.version = item.value("Version", std::string());
    // Description is null for some packages; value() handles the missing key but
    // a present-but-null field would throw, so read it defensively.
    if (auto desc = item.find("Description");
        desc != item.end() && desc->is_string()) {
      package.description = desc->get<std::string>();
    }
    // Trust signals. Read defensively like Description: OutOfDate is null unless
    // flagged, and a missing numeric field keeps its "unknown" sentinel.
    if (auto votes = item.find("NumVotes"); votes != item.end() && votes->is_number()) {
      package.aur_votes = votes->get<int>();
    }
    if (auto pop = item.find("Popularity"); pop != item.end() && pop->is_number()) {
      package.aur_popularity = pop->get<double>();
    }
    if (auto ood = item.find("OutOfDate"); ood != item.end() && ood->is_number()) {
      package.aur_out_of_date = true;
    }
    package.repo = model::Repo::Aur;
    results.push_back(std::move(package));
  }

  // The RPC returns results unordered; present the widely-used package first so
  // it outranks look-alikes. Popularity (votes decayed by recency) breaks less
  // gameably than raw votes; ties fall back to votes, then name. The catalog's
  // sorts are stable, so this order survives as the tiebreak among the 0-byte
  // sizes the size sort sees.
  std::stable_sort(results.begin(), results.end(),
                   [](const model::Package& a, const model::Package& b) {
                     if (a.aur_popularity != b.aur_popularity) {
                       return a.aur_popularity > b.aur_popularity;
                     }
                     if (a.aur_votes != b.aur_votes) {
                       return a.aur_votes > b.aur_votes;
                     }
                     return a.name < b.name;
                   });
  return results;
}

AurInfo FetchAurInfo(const std::string& name, std::string& error) {
  error.clear();
  AurInfo info;

  const std::string escaped = UrlEscape(name);
  if (escaped.empty()) {
    error = "could not encode package name";
    return info;
  }

  std::string body;
  if (!HttpGet(std::string(kRpcInfoBase) + escaped, body, error)) {
    return info;
  }

  nlohmann::json json = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
  if (json.is_discarded() || !json.is_object()) {
    error = "AUR returned a malformed response";
    return info;
  }
  if (json.value("type", "") == "error") {
    error = "AUR: " + json.value("error", std::string("unknown error"));
    return info;
  }

  const auto items = json.find("results");
  if (items == json.end() || !items->is_array() || items->empty()) {
    return info;  // the AUR doesn't know this name: found stays false, no error
  }
  const auto& item = items->front();
  info.found = true;
  info.version = item.value("Version", std::string());
  if (auto votes = item.find("NumVotes"); votes != item.end() && votes->is_number()) {
    info.votes = votes->get<int>();
  }
  if (auto pop = item.find("Popularity"); pop != item.end() && pop->is_number()) {
    info.popularity = pop->get<double>();
  }
  // Maintainer is null for orphaned packages; leaving it empty is the signal.
  if (auto maint = item.find("Maintainer"); maint != item.end() && maint->is_string()) {
    info.maintainer = maint->get<std::string>();
  }
  if (auto modified = item.find("LastModified");
      modified != item.end() && modified->is_number()) {
    info.last_modified = FormatAurDate(modified->get<int64_t>());
  }
  if (auto submitted = item.find("FirstSubmitted");
      submitted != item.end() && submitted->is_number()) {
    info.first_submitted = FormatAurDate(submitted->get<int64_t>());
  }
  if (auto ood = item.find("OutOfDate"); ood != item.end() && ood->is_number()) {
    info.out_of_date = FormatAurDate(ood->get<int64_t>());
    if (info.out_of_date.empty()) {
      info.out_of_date = "yes";  // flagged, but the timestamp didn't render
    }
  }
  return info;
}

#else  // PACSEEK_HAVE_JSON

std::vector<model::Package> SearchAur(const std::string&, std::string& error) {
  error = "AUR search unavailable: built without nlohmann-json";
  return {};
}

AurInfo FetchAurInfo(const std::string&, std::string& error) {
  error = "AUR info unavailable: built without nlohmann-json";
  return {};
}

#endif  // PACSEEK_HAVE_JSON

}  // namespace pacseek::system
