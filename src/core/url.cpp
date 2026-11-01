#include "url.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace fetcher {

namespace {

constexpr const char* kHex = "0123456789ABCDEF";

bool isUnreserved(unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string trim(const std::string& text) {
    const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    auto begin = std::find_if_not(text.begin(), text.end(), isSpace);
    auto end = std::find_if_not(text.rbegin(), std::make_reverse_iterator(begin), isSpace).base();
    return std::string(begin, end);
}

// Splits "scheme://authority/path?query#fragment" around the query.
struct QuerySplit {
    std::string before;    // up to but excluding '?'
    std::string query;     // excluding '?'
    std::string fragment;  // including '#'
};

QuerySplit splitQuery(const std::string& url) {
    QuerySplit split;
    std::string rest = url;
    auto hash = rest.find('#');
    if (hash != std::string::npos) {
        split.fragment = rest.substr(hash);
        rest.erase(hash);
    }
    auto question = rest.find('?');
    if (question != std::string::npos) {
        split.query = rest.substr(question + 1);
        rest.erase(question);
    }
    split.before = rest;
    return split;
}

// Removes "." and ".." segments as described in RFC 3986 section 5.2.4.
std::string removeDotSegments(const std::string& path) {
    std::vector<std::string> output;
    std::size_t pos = 0;
    bool endsWithSlash = false;
    while (pos <= path.size()) {
        auto next = path.find('/', pos);
        std::string segment =
            path.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        bool last = next == std::string::npos;
        if (segment == ".") {
            endsWithSlash = last;
        } else if (segment == "..") {
            if (output.size() > 1) output.pop_back();
            endsWithSlash = last;
        } else {
            output.push_back(segment);
            endsWithSlash = false;
        }
        if (last) break;
        pos = next + 1;
    }
    std::string result;
    for (std::size_t i = 0; i < output.size(); ++i) {
        if (i > 0) result += '/';
        result += output[i];
    }
    if (endsWithSlash) result += '/';
    return result;
}

int defaultPort(const std::string& scheme) {
    if (scheme == "https") return 443;
    if (scheme == "http") return 80;
    return 0;
}

}  // namespace

std::string percentEncode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
        if (isUnreserved(c)) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0xF];
        }
    }
    return out;
}

std::string percentDecode(std::string_view text, bool plusAsSpace) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '+' && plusAsSpace) {
            out += ' ';
        } else if (c == '%' && i + 2 < text.size() && hexValue(text[i + 1]) >= 0 &&
                   hexValue(text[i + 2]) >= 0) {
            out += static_cast<char>(hexValue(text[i + 1]) * 16 + hexValue(text[i + 2]));
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

std::vector<KeyValue> parseQuery(const std::string& url) {
    std::vector<KeyValue> params;
    const std::string query = splitQuery(url).query;
    std::size_t pos = 0;
    while (pos < query.size()) {
        auto end = query.find('&', pos);
        if (end == std::string::npos) end = query.size();
        std::string_view pair(query.data() + pos, end - pos);
        if (!pair.empty()) {
            KeyValue param;
            auto equals = pair.find('=');
            param.key = percentDecode(pair.substr(0, equals), true);
            if (equals != std::string_view::npos)
                param.value = percentDecode(pair.substr(equals + 1), true);
            params.push_back(std::move(param));
        }
        pos = end + 1;
    }
    return params;
}

std::string withQuery(const std::string& url, const std::vector<KeyValue>& params) {
    QuerySplit split = splitQuery(url);
    std::string query;
    for (const auto& param : params) {
        if (!param.enabled || (param.key.empty() && param.value.empty())) continue;
        if (!query.empty()) query += '&';
        query += percentEncode(param.key);
        if (!param.value.empty()) {
            query += '=';
            query += percentEncode(param.value);
        }
    }
    std::string result = split.before;
    if (!query.empty()) result += '?' + query;
    return result + split.fragment;
}

std::vector<KeyValue> mergeParams(const std::string& url, const std::vector<KeyValue>& previous) {
    const std::vector<KeyValue> fromUrl = parseQuery(url);
    std::vector<KeyValue> merged;
    std::size_t next = 0;
    for (const auto& row : previous) {
        if (!row.enabled) {
            merged.push_back(row);
        } else if (next < fromUrl.size()) {
            merged.push_back(fromUrl[next++]);
        }
    }
    while (next < fromUrl.size()) merged.push_back(fromUrl[next++]);
    return merged;
}

std::string normalizeUrl(const std::string& input) {
    std::string url = trim(input);
    if (url.empty()) return url;
    auto hash = url.find('#');
    if (hash != std::string::npos) url.erase(hash);

    auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
        url.insert(0, "http://");
        schemeEnd = 4;
    }
    auto authorityEnd = url.find_first_of("/?", schemeEnd + 3);
    if (authorityEnd == std::string::npos) return url;

    std::string out = url.substr(0, authorityEnd);
    for (std::size_t i = authorityEnd; i < url.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(url[i]);
        static constexpr std::string_view kIllegal = "\"<>\\^`{|}";
        if (c <= 0x20 || c >= 0x7F || kIllegal.find(static_cast<char>(c)) != kIllegal.npos) {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0xF];
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

bool parseUrl(const std::string& url, UrlParts& parts) {
    auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos || schemeEnd == 0) return false;
    parts.scheme = lower(url.substr(0, schemeEnd));

    auto authorityStart = schemeEnd + 3;
    auto authorityEnd = url.find_first_of("/?#", authorityStart);
    parts.authority = url.substr(
        authorityStart, authorityEnd == std::string::npos ? std::string::npos : authorityEnd - authorityStart);
    if (parts.authority.empty()) return false;

    std::string rest = authorityEnd == std::string::npos ? std::string() : url.substr(authorityEnd);
    auto hash = rest.find('#');
    if (hash != std::string::npos) rest.erase(hash);
    if (rest.empty() || rest[0] != '/') rest.insert(0, "/");
    parts.path = rest;

    std::string hostPort = parts.authority;
    auto at = hostPort.rfind('@');
    if (at != std::string::npos) hostPort.erase(0, at + 1);
    parts.port = defaultPort(parts.scheme);
    if (!hostPort.empty() && hostPort[0] == '[') {  // IPv6 literal
        auto close = hostPort.find(']');
        if (close == std::string::npos) return false;
        parts.host = lower(hostPort.substr(0, close + 1));
        if (close + 2 < hostPort.size() && hostPort[close + 1] == ':')
            parts.port = std::atoi(hostPort.c_str() + close + 2);
    } else {
        auto colon = hostPort.rfind(':');
        parts.host = lower(hostPort.substr(0, colon));
        if (colon != std::string::npos && colon + 1 < hostPort.size())
            parts.port = std::atoi(hostPort.c_str() + colon + 1);
    }
    return !parts.host.empty();
}

std::string resolveUrl(const std::string& base, const std::string& reference) {
    if (reference.find("://") != std::string::npos &&
        reference.find("://") < reference.find_first_of("/?#"))
        return reference;

    UrlParts parts;
    if (!parseUrl(base, parts)) return reference;
    const std::string origin = parts.scheme + "://" + parts.authority;

    if (reference.rfind("//", 0) == 0) return parts.scheme + ":" + reference;
    if (reference.empty()) return base;

    std::string basePath = parts.path;
    auto question = basePath.find('?');
    std::string baseQuery;
    if (question != std::string::npos) {
        baseQuery = basePath.substr(question);
        basePath.erase(question);
    }
    if (reference[0] == '?') return origin + basePath + reference;
    if (reference[0] == '#') return origin + basePath + baseQuery + reference;

    auto suffixStart = reference.find_first_of("?#");
    std::string refPath = reference.substr(0, suffixStart);
    std::string suffix = suffixStart == std::string::npos ? std::string() : reference.substr(suffixStart);
    if (refPath[0] != '/') {
        refPath = basePath.substr(0, basePath.rfind('/') + 1) + refPath;
    }
    return origin + removeDotSegments(refPath) + suffix;
}

bool sameOrigin(const std::string& a, const std::string& b) {
    UrlParts pa, pb;
    if (!parseUrl(a, pa) || !parseUrl(b, pb)) return false;
    return pa.scheme == pb.scheme && pa.host == pb.host && pa.port == pb.port;
}

}  // namespace fetcher
