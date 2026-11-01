// WinHTTP transport, used on Windows.
//
// WinHTTP's blocking calls cannot be interrupted cleanly, so the session is
// opened in asynchronous mode and this thread waits on an event between steps.
// That lets it notice cancellation and the deadline at any point.
#include "http_client.h"

#include "url.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <memory>

#ifndef WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL
#define WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL 133
#endif
#ifndef WINHTTP_OPTION_HTTP_PROTOCOL_USED
#define WINHTTP_OPTION_HTTP_PROTOCOL_USED 134
#endif
#ifndef WINHTTP_PROTOCOL_FLAG_HTTP2
#define WINHTTP_PROTOCOL_FLAG_HTTP2 0x1
#endif
#ifndef WINHTTP_OPTION_DECOMPRESSION
#define WINHTTP_OPTION_DECOMPRESSION 118
#endif
#ifndef WINHTTP_DECOMPRESSION_FLAG_ALL
#define WINHTTP_DECOMPRESSION_FLAG_ALL 0x3
#endif

namespace fetcher::detail {

namespace {

std::wstring toWide(const std::string& text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

std::string toUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                           nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), length, nullptr, nullptr);
    return utf8;
}

std::string describeError(DWORD code, DWORD secureFailure) {
    if (code == ERROR_WINHTTP_SECURE_FAILURE) {
        if (secureFailure & WINHTTP_CALLBACK_STATUS_FLAG_CERT_DATE_INVALID)
            return "The server's TLS certificate has expired or is not yet valid.";
        if (secureFailure & WINHTTP_CALLBACK_STATUS_FLAG_CERT_CN_INVALID)
            return "The server's TLS certificate does not match this host name.";
        if (secureFailure & WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CA)
            return "The server's TLS certificate is not issued by a trusted authority.";
        if (secureFailure & WINHTTP_CALLBACK_STATUS_FLAG_CERT_REVOKED)
            return "The server's TLS certificate has been revoked.";
        return "A secure connection to the server could not be established.";
    }
    if (code == ERROR_WINHTTP_TIMEOUT) return "The request timed out.";
    if (code == ERROR_WINHTTP_NAME_NOT_RESOLVED) return "The server's host name could not be resolved.";
    if (code == ERROR_WINHTTP_CANNOT_CONNECT) return "Could not connect to the server.";
    if (code == ERROR_WINHTTP_UNRECOGNIZED_SCHEME || code == ERROR_WINHTTP_INVALID_URL)
        return "The URL is not valid. Only http:// and https:// URLs are supported.";

    wchar_t* buffer = nullptr;
    HMODULE module = GetModuleHandleW(L"winhttp.dll");
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS |
                        FORMAT_MESSAGE_FROM_SYSTEM | (module ? FORMAT_MESSAGE_FROM_HMODULE : 0);
    const DWORD length = FormatMessageW(flags, module, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::string message;
    if (length > 0 && buffer) {
        message = toUtf8(std::wstring(buffer, length));
        LocalFree(buffer);
        while (!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == ' '))
            message.pop_back();
    }
    return message.empty() ? "Network error " + std::to_string(code) + "." : message;
}

// State shared with the WinHTTP callback, which runs on a system thread pool.
struct Exchange {
    HANDLE stepDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE closed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD error = 0;
    DWORD secureFailure = 0;
    DWORD bytes = 0;

    ~Exchange() {
        CloseHandle(stepDone);
        CloseHandle(closed);
    }
};

void CALLBACK onStatus(HINTERNET, DWORD_PTR context, DWORD status, LPVOID info, DWORD infoLength) {
    auto* exchange = reinterpret_cast<Exchange*>(context);
    switch (status) {
        case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
        case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
            SetEvent(exchange->stepDone);
            break;
        case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
            exchange->bytes = infoLength;
            SetEvent(exchange->stepDone);
            break;
        case WINHTTP_CALLBACK_STATUS_SECURE_FAILURE:
            // Always followed by REQUEST_ERROR, which wakes the waiting thread.
            exchange->secureFailure = *static_cast<DWORD*>(info);
            break;
        case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
            exchange->error = static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError;
            SetEvent(exchange->stepDone);
            break;
        case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
            SetEvent(exchange->closed);
            break;
        default:
            break;
    }
}

struct InternetHandle {
    HINTERNET handle = nullptr;
    ~InternetHandle() {
        if (handle) WinHttpCloseHandle(handle);
    }
};

enum class Wait { Done, Cancelled, TimedOut };

Wait waitForStep(const Exchange& exchange, const std::atomic<bool>& cancel, bool hasDeadline,
                 Clock::time_point deadline) {
    for (;;) {
        if (WaitForSingleObject(exchange.stepDone, 50) == WAIT_OBJECT_0) return Wait::Done;
        if (cancel.load()) return Wait::Cancelled;
        if (hasDeadline && Clock::now() >= deadline) return Wait::TimedOut;
    }
}

HeaderList::const_iterator findHeaderEntry(const HeaderList& headers, const std::string& name) {
    const std::string* value = findHeader(headers, name);
    return std::find_if(headers.begin(), headers.end(), [&](const auto& h) { return &h.second == value; });
}

bool queryRawHeaders(HINTERNET request, DWORD flags, std::wstring& out) {
    DWORD size = 0;
    WinHttpQueryHeaders(request, flags, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &size,
                        WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return false;
    out.resize(size / sizeof(wchar_t));
    if (!WinHttpQueryHeaders(request, flags, WINHTTP_HEADER_NAME_BY_INDEX, out.data(), &size, WINHTTP_NO_HEADER_INDEX))
        return false;
    out.resize(size / sizeof(wchar_t));
    return true;
}

}  // namespace

void sendOnce(const PreparedRequest& request, const std::atomic<bool>& cancel, bool hasDeadline,
              Clock::time_point deadline, std::size_t maxBodyBytes, HttpResponse& response) {
    const auto start = Clock::now();

    const auto fail = [&](DWORD code, DWORD secureFailure = 0) {
        response.error = describeError(code, secureFailure);
    };

    std::wstring url = toWide(request.url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
        fail(GetLastError());
        return;
    }
    if (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS) {
        fail(ERROR_WINHTTP_UNRECOGNIZED_SCHEME);
        return;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (path.empty()) path = L"/";

    InternetHandle session;
    session.handle = WinHttpOpen(L"Fetcher", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                 WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    if (!session.handle) {
        fail(GetLastError());
        return;
    }
    DWORD protocols = WINHTTP_PROTOCOL_FLAG_HTTP2;
    WinHttpSetOption(session.handle, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &protocols, sizeof(protocols));
    DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(session.handle, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));
    // The deadline in waitForStep() is what enforces timeouts, so don't let
    // WinHTTP's own per-step defaults cut a longer limit short.
    WinHttpSetTimeouts(session.handle, 0, 0, 0, 0);

    InternetHandle connection;
    connection.handle = WinHttpConnect(session.handle, host.c_str(), parts.nPort, 0);
    if (!connection.handle) {
        fail(GetLastError());
        return;
    }

    const std::wstring method = toWide(request.method);
    HINTERNET handle = WinHttpOpenRequest(connection.handle, method.c_str(), path.c_str(), nullptr,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                          parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!handle) {
        fail(GetLastError());
        return;
    }

    // The context must outlive every callback, the last of which is
    // HANDLE_CLOSING, so finish() waits for that before anything is freed.
    auto exchange = std::make_unique<Exchange>();
    DWORD_PTR context = reinterpret_cast<DWORD_PTR>(exchange.get());
    WinHttpSetOption(handle, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context));
    WinHttpSetStatusCallback(handle, onStatus,
                             WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_SECURE_FAILURE |
                                 WINHTTP_CALLBACK_FLAG_HANDLES,
                             0);

    const auto finish = [&] {
        WinHttpCloseHandle(handle);
        WaitForSingleObject(exchange->closed, INFINITE);
    };

    // Redirects are handled by sendRequest() so credentials can be dropped when
    // they lead to another site.
    DWORD disableFeatures = WINHTTP_DISABLE_REDIRECTS;
    WinHttpSetOption(handle, WINHTTP_OPTION_DISABLE_FEATURE, &disableFeatures, sizeof(disableFeatures));

    if (!request.options.verifyTls) {
        DWORD security = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(handle, WINHTTP_OPTION_SECURITY_FLAGS, &security, sizeof(security));
    }

    for (std::size_t i = 0; i < request.headers.size(); ++i) {
        const auto& [name, value] = request.headers[i];
        // The first use of a name replaces WinHTTP's own default for it; repeats
        // of that name are then sent as additional headers.
        const bool repeated = &*findHeaderEntry(request.headers, name) != &request.headers[i];
        const std::wstring line = toWide(name) + L": " + toWide(value);
        if (!WinHttpAddRequestHeaders(handle, line.c_str(), static_cast<DWORD>(line.size()),
                                      WINHTTP_ADDREQ_FLAG_ADD | (repeated ? 0 : WINHTTP_ADDREQ_FLAG_REPLACE))) {
            response.error = "The header \"" + name + "\" is not valid.";
            finish();
            return;
        }
    }

    const auto waitStep = [&]() -> bool {
        switch (waitForStep(*exchange, cancel, hasDeadline, deadline)) {
            case Wait::Cancelled:
                response.cancelled = true;
                response.error = "Request cancelled.";
                return false;
            case Wait::TimedOut:
                response.error = "The request timed out.";
                return false;
            case Wait::Done:
                break;
        }
        if (exchange->error != 0) {
            fail(exchange->error, exchange->secureFailure);
            return false;
        }
        return true;
    };

    const DWORD bodySize = static_cast<DWORD>(request.body.size());
    if (!WinHttpSendRequest(handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            bodySize ? const_cast<char*>(request.body.data()) : WINHTTP_NO_REQUEST_DATA, bodySize,
                            bodySize, reinterpret_cast<DWORD_PTR>(exchange.get()))) {
        fail(GetLastError());
        finish();
        return;
    }
    if (!waitStep()) {
        finish();
        return;
    }

    if (!WinHttpReceiveResponse(handle, nullptr)) {
        fail(GetLastError());
        finish();
        return;
    }
    if (!waitStep()) {
        finish();
        return;
    }
    response.firstByteMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    std::wstring raw;
    if (queryRawHeaders(handle, WINHTTP_QUERY_RAW_HEADERS_CRLF | WINHTTP_QUERY_FLAG_REQUEST_HEADERS, raw))
        response.sentLog += toUtf8(raw);

    if (queryRawHeaders(handle, WINHTTP_QUERY_RAW_HEADERS_CRLF, raw)) {
        const std::string headers = toUtf8(raw);
        std::size_t pos = 0;
        while (pos < headers.size()) {
            auto end = headers.find('\n', pos);
            if (end == std::string::npos) end = headers.size();
            parseHeaderLine(headers.substr(pos, end - pos), response);
            pos = end + 1;
        }
    }
    // The raw status line doesn't show which protocol was really negotiated.
    DWORD protocolUsed = 0;
    DWORD protocolUsedSize = sizeof(protocolUsed);
    if (WinHttpQueryOption(handle, WINHTTP_OPTION_HTTP_PROTOCOL_USED, &protocolUsed, &protocolUsedSize) &&
        (protocolUsed & WINHTTP_PROTOCOL_FLAG_HTTP2))
        response.httpVersion = "HTTP/2";

    constexpr DWORD kChunk = 64 * 1024;
    auto buffer = std::make_unique<char[]>(kChunk);
    for (;;) {
        exchange->bytes = 0;
        if (!WinHttpReadData(handle, buffer.get(), kChunk, nullptr)) {
            fail(GetLastError());
            break;
        }
        if (!waitStep()) break;
        if (exchange->bytes == 0) break;  // end of the body

        if (response.body.size() + exchange->bytes > maxBodyBytes) {
            response.body.append(buffer.get(), maxBodyBytes - response.body.size());
            response.truncated = true;
            break;
        }
        response.body.append(buffer.get(), exchange->bytes);
    }

    finish();
}

}  // namespace fetcher::detail
