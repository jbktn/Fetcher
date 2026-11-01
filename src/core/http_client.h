// Turns a saved Request into bytes on the wire.
//
// The transport is the operating system's own where it has one (WinHTTP on
// Windows) and libcurl elsewhere. Both implement sendOnce(), a single exchange
// with no redirect handling; redirect policy lives in sendRequest() so that it
// behaves the same on every platform.
#pragma once

#include "model.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace fetcher {

using HeaderList = std::vector<std::pair<std::string, std::string>>;

struct PreparedRequest {
    std::string method;
    std::string url;
    HeaderList headers;
    std::string body;
    RequestOptions options;
};

struct HttpResponse {
    bool received = false;  // an HTTP response arrived, whatever its status
    bool cancelled = false;
    std::string error;  // why no complete response is available

    long status = 0;
    std::string statusText;
    std::string httpVersion;
    std::string finalUrl;
    int redirects = 0;

    HeaderList headers;
    std::string sentLog;  // request headers actually sent, one block per hop
    std::string body;
    bool truncated = false;  // the body exceeded the size limit

    double totalMs = 0;
    double firstByteMs = 0;
};

// Applies auth and default headers and builds the body. Does no I/O.
PreparedRequest prepareRequest(const Request& request);

const std::string* findHeader(const HeaderList& headers, const std::string& name);

// Blocks until the exchange finishes, fails or `cancel` becomes true. Safe to
// call from a worker thread; touches no global state besides one-time library
// initialisation.
HttpResponse sendRequest(const PreparedRequest& request, const std::atomic<bool>& cancel,
                         std::size_t maxBodyBytes = 64 * 1024 * 1024);

// Shared by the transports.
namespace detail {

using Clock = std::chrono::steady_clock;

// `deadline` is ignored when `hasDeadline` is false. Fills `received`, `status`,
// `statusText`, `httpVersion`, `headers`, `body`, `truncated`, `firstByteMs`,
// `cancelled` and `error`, and appends to `sentLog`.
void sendOnce(const PreparedRequest& request, const std::atomic<bool>& cancel, bool hasDeadline,
              Clock::time_point deadline, std::size_t maxBodyBytes, HttpResponse& response);

// Feeds one raw header line ("HTTP/1.1 200 OK", "Name: value"). A status line
// starts a new response, discarding headers of any interim one.
void parseHeaderLine(std::string line, HttpResponse& response);

}  // namespace detail

}  // namespace fetcher
