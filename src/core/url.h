// URL helpers. Query parameters in a KeyValue are always in decoded form; they
// are encoded when written back into a URL.
#pragma once

#include "model.h"

#include <string>
#include <string_view>
#include <vector>

namespace fetcher {

std::string percentEncode(std::string_view text);
std::string percentDecode(std::string_view text, bool plusAsSpace);

std::vector<KeyValue> parseQuery(const std::string& url);

// Returns `url` with its query string replaced by the enabled entries of
// `params`. The fragment, if any, is kept.
std::string withQuery(const std::string& url, const std::vector<KeyValue>& params);

// Rows for the params table after the URL was edited: each enabled row takes
// the next parameter from the URL, and disabled rows stay where they were.
std::vector<KeyValue> mergeParams(const std::string& url, const std::vector<KeyValue>& previous);

// Prepares user input for sending: trims it, adds http:// if no scheme was
// given, drops the fragment and escapes characters that are illegal in a URL.
// Existing %XX sequences are left alone.
std::string normalizeUrl(const std::string& url);

struct UrlParts {
    std::string scheme;     // lower case
    std::string authority;  // user:pass@host:port, as written
    std::string host;       // lower case
    int port = 0;           // explicit port, or the scheme's default
    std::string path;       // path and query, always starts with '/'
};

// Returns false when `url` has no scheme://authority part.
bool parseUrl(const std::string& url, UrlParts& parts);

// Resolves a Location header against the URL it came from (RFC 3986 section 5).
std::string resolveUrl(const std::string& base, const std::string& reference);

bool sameOrigin(const std::string& a, const std::string& b);

}  // namespace fetcher
