#include "http_client.h"

#include "text.h"
#include "url.h"

#include <algorithm>
#include <cctype>

namespace fetcher {

namespace {

constexpr const char* kUserAgent = "Fetcher/" FETCHER_VERSION;
constexpr int kMaxRedirects = 20;

bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
               return std::tolower(x) == std::tolower(y);
           });
}

void removeHeader(HeaderList& headers, const std::string& name) {
    headers.erase(std::remove_if(headers.begin(), headers.end(),
                                 [&](const auto& h) { return equalsIgnoreCase(h.first, name); }),
                  headers.end());
}

void addDefaultHeader(HeaderList& headers, const std::string& name, const std::string& value) {
    if (!findHeader(headers, name)) headers.emplace_back(name, value);
}

const char* contentTypeFor(BodyType type) {
    switch (type) {
        case BodyType::Json: return "application/json";
        case BodyType::Xml: return "application/xml";
        case BodyType::Text: return "text/plain; charset=utf-8";
        case BodyType::Form: return "application/x-www-form-urlencoded";
        case BodyType::None: break;
    }
    return nullptr;
}

std::string encodeForm(const std::vector<KeyValue>& rows) {
    std::string out;
    for (const auto& row : rows) {
        if (!row.enabled || row.key.empty()) continue;
        if (!out.empty()) out += '&';
        out += percentEncode(row.key) + "=" + percentEncode(row.value);
    }
    return out;
}

void applyAuth(const Auth& auth, PreparedRequest& out) {
    switch (auth.type) {
        case AuthType::Bearer:
            if (!auth.token.empty()) addDefaultHeader(out.headers, "Authorization", "Bearer " + auth.token);
            break;
        case AuthType::Basic:
            if (!auth.username.empty() || !auth.password.empty())
                addDefaultHeader(out.headers, "Authorization",
                                 "Basic " + base64Encode(auth.username + ":" + auth.password));
            break;
        case AuthType::ApiKey:
            if (auth.apiKeyName.empty()) break;
            if (auth.apiKeyInQuery) {
                // out.url is already normalized, so it has no fragment to step around.
                const char separator = out.url.find('?') == std::string::npos ? '?' : '&';
                out.url += separator + percentEncode(auth.apiKeyName) + "=" + percentEncode(auth.apiKeyValue);
            } else {
                addDefaultHeader(out.headers, auth.apiKeyName, auth.apiKeyValue);
            }
            break;
        case AuthType::None:
            break;
    }
}

bool isRedirect(long status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

}  // namespace

const std::string* findHeader(const HeaderList& headers, const std::string& name) {
    for (const auto& header : headers)
        if (equalsIgnoreCase(header.first, name)) return &header.second;
    return nullptr;
}

PreparedRequest prepareRequest(const Request& request) {
    PreparedRequest out;
    out.method = request.method;
    out.method.erase(std::remove_if(out.method.begin(), out.method.end(),
                                    [](unsigned char c) { return std::isspace(c) != 0; }),
                     out.method.end());
    std::transform(out.method.begin(), out.method.end(), out.method.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (out.method.empty()) out.method = "GET";

    out.url = normalizeUrl(request.url);
    out.options = request.options;

    for (const auto& row : request.headers)
        if (row.enabled && !row.key.empty()) out.headers.emplace_back(row.key, row.value);

    applyAuth(request.auth, out);

    if (request.bodyType == BodyType::Form) {
        out.body = encodeForm(request.form);
    } else if (request.bodyType != BodyType::None) {
        out.body = request.body;
    }
    if (const char* type = contentTypeFor(request.bodyType))
        addDefaultHeader(out.headers, "Content-Type", type);

    addDefaultHeader(out.headers, "User-Agent", kUserAgent);
    addDefaultHeader(out.headers, "Accept", "*/*");
    return out;
}

HttpResponse sendRequest(const PreparedRequest& request, const std::atomic<bool>& cancel,
                         std::size_t maxBodyBytes) {
    const auto start = detail::Clock::now();
    const bool hasDeadline = request.options.timeoutSeconds > 0;
    const auto deadline = start + std::chrono::seconds(request.options.timeoutSeconds);

    PreparedRequest hop = request;
    HttpResponse response;
    bool firstHop = true;
    double firstByteMs = 0;

    for (;;) {
        const std::string log = std::move(response.sentLog);
        const int redirects = response.redirects;
        response = HttpResponse{};
        response.sentLog = log;
        response.redirects = redirects;
        response.finalUrl = hop.url;

        detail::sendOnce(hop, cancel, hasDeadline, deadline, maxBodyBytes, response);
        if (firstHop) firstByteMs = response.firstByteMs;
        firstHop = false;

        const std::string* location = findHeader(response.headers, "Location");
        if (!response.received || !response.error.empty() || !request.options.followRedirects ||
            !isRedirect(response.status) || !location || location->empty())
            break;
        if (response.redirects >= kMaxRedirects) {
            response.error = "Stopped after " + std::to_string(kMaxRedirects) + " redirects.";
            break;
        }

        const std::string target = normalizeUrl(resolveUrl(hop.url, *location));
        const bool downgradeToGet =
            (response.status == 303 && hop.method != "HEAD") ||
            ((response.status == 301 || response.status == 302) && hop.method == "POST");
        if (downgradeToGet) {
            hop.method = "GET";
            hop.body.clear();
            removeHeader(hop.headers, "Content-Type");
            removeHeader(hop.headers, "Content-Length");
        }
        // Never forward credentials to a different site.
        if (!sameOrigin(hop.url, target)) {
            removeHeader(hop.headers, "Authorization");
            removeHeader(hop.headers, "Proxy-Authorization");
            removeHeader(hop.headers, "Cookie");
        }
        hop.url = target;
        ++response.redirects;
    }

    response.firstByteMs = firstByteMs;
    response.totalMs =
        std::chrono::duration<double, std::milli>(detail::Clock::now() - start).count();
    return response;
}

namespace detail {

void parseHeaderLine(std::string line, HttpResponse& response) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (line.empty()) return;

    if (line.rfind("HTTP/", 0) == 0) {
        response.headers.clear();
        const auto firstSpace = line.find(' ');
        response.httpVersion = line.substr(0, firstSpace);
        response.status = 0;
        response.statusText.clear();
        if (firstSpace != std::string::npos) {
            const auto secondSpace = line.find(' ', firstSpace + 1);
            try {
                response.status = std::stol(line.substr(firstSpace + 1, secondSpace - firstSpace - 1));
            } catch (const std::exception&) {
                response.status = 0;
            }
            if (secondSpace != std::string::npos) response.statusText = line.substr(secondSpace + 1);
        }
        // HTTP/2 and HTTP/3 have no reason phrase on the wire.
        if (response.statusText.empty()) response.statusText = statusReason(response.status);
        response.received = true;
        return;
    }

    const auto colon = line.find(':');
    if (colon == std::string::npos) return;
    std::string value = line.substr(colon + 1);
    const auto firstChar = value.find_first_not_of(" \t");
    value.erase(0, firstChar == std::string::npos ? value.size() : firstChar);
    response.headers.emplace_back(line.substr(0, colon), std::move(value));
}

}  // namespace detail

}  // namespace fetcher
