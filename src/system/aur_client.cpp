#include "system/aur_client.hpp"

#include <mutex>
#include <string>

#include <curl/curl.h>

#ifdef PACSEEK_HAVE_JSON
#include <nlohmann/json.hpp>
#endif

namespace pacseek::system {

namespace {

// AUR RPC v5. The query form (arg=…) keeps URL-escaping to the single term, and
// by=name-desc mirrors the app's local search, which matches name + description.
constexpr char kRpcBase[] = "https://aur.archlinux.org/rpc/?v=5&type=search&by=name-desc&arg=";

// curl_global_init is not thread-safe, so do it exactly once before any worker
// thread spins up an easy handle.
void EnsureCurlInitialized() {
  static std::once_flag once;
  std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

// libcurl write callback: appends the received bytes to the std::string in `out`.
size_t AppendToString(char* data, size_t size, size_t nmemb, void* out) {
  const size_t bytes = size * nmemb;
  static_cast<std::string*>(out)->append(data, bytes);
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
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
  // Identify the client so the AUR can attribute (and if ever needed, contact
  // about) traffic from pacseek, as their usage guidelines ask.
  curl_easy_setopt(curl, CURLOPT_USERAGENT,
                   "pacseek/0.1 (+https://codeberg.org/m1stD3V/pacseek)");

  const CURLcode code = curl_easy_perform(curl);
  long http_status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
  curl_easy_cleanup(curl);

  if (code != CURLE_OK) {
    error = std::string("AUR request failed: ") + curl_easy_strerror(code);
    return false;
  }
  if (http_status < 200 || http_status >= 300) {
    error = "AUR returned HTTP " + std::to_string(http_status);
    return false;
  }
  return true;
}

}  // namespace

#ifdef PACSEEK_HAVE_JSON

std::vector<model::Package> SearchAur(const std::string& term, std::string& error) {
  error.clear();
  std::vector<model::Package> results;

  EnsureCurlInitialized();
  // Escape the term so spaces or RPC metacharacters can't break out of the query.
  char* escaped = curl_easy_escape(nullptr, term.c_str(), static_cast<int>(term.size()));
  if (escaped == nullptr) {
    error = "could not encode search term";
    return results;
  }
  const std::string url = std::string(kRpcBase) + escaped;
  curl_free(escaped);

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
    package.repo = model::Repo::Aur;
    results.push_back(std::move(package));
  }
  return results;
}

#else  // PACSEEK_HAVE_JSON

std::vector<model::Package> SearchAur(const std::string&, std::string& error) {
  error = "AUR search unavailable: built without nlohmann-json";
  return {};
}

#endif  // PACSEEK_HAVE_JSON

}  // namespace pacseek::system
