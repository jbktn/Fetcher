// libcurl transport, used on macOS and Linux.
#include "http_client.h"

#include <curl/curl.h>

#include <mutex>

namespace fetcher::detail {

namespace {

struct Transfer {
    HttpResponse* response;
    const std::atomic<bool>* cancel;
    std::size_t maxBodyBytes;
    std::string sent;
    Clock::time_point start;
    bool sawBodyOrHeader = false;
};

void noteFirstByte(Transfer& transfer) {
    if (transfer.sawBodyOrHeader) return;
    transfer.sawBodyOrHeader = true;
    transfer.response->firstByteMs =
        std::chrono::duration<double, std::milli>(Clock::now() - transfer.start).count();
}

std::size_t onHeader(char* data, std::size_t size, std::size_t count, void* user) {
    auto& transfer = *static_cast<Transfer*>(user);
    noteFirstByte(transfer);
    const std::size_t length = size * count;
    parseHeaderLine(std::string(data, length), *transfer.response);
    return length;
}

std::size_t onBody(char* data, std::size_t size, std::size_t count, void* user) {
    auto& transfer = *static_cast<Transfer*>(user);
    const std::size_t length = size * count;
    std::string& body = transfer.response->body;
    if (body.size() + length > transfer.maxBodyBytes) {
        body.append(data, transfer.maxBodyBytes - body.size());
        transfer.response->truncated = true;
        return 0;  // makes curl stop with CURLE_WRITE_ERROR, handled by the caller
    }
    body.append(data, length);
    return length;
}

int onProgress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const auto& transfer = *static_cast<Transfer*>(user);
    return transfer.cancel->load() ? 1 : 0;
}

int onDebug(CURL*, curl_infotype type, char* data, std::size_t size, void* user) {
    if (type == CURLINFO_HEADER_OUT) static_cast<Transfer*>(user)->sent.append(data, size);
    return 0;
}

}  // namespace

void sendOnce(const PreparedRequest& request, const std::atomic<bool>& cancel, bool hasDeadline,
              Clock::time_point deadline, std::size_t maxBodyBytes, HttpResponse& response) {
    static std::once_flag globalInit;
    std::call_once(globalInit, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "Could not start the HTTP library.";
        return;
    }

    Transfer transfer{&response, &cancel, maxBodyBytes, {}, Clock::now()};
    char errorBuffer[CURL_ERROR_SIZE] = "";

    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);  // required when used from worker threads
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);  // see sendRequest()
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");  // everything libcurl can decompress
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, request.options.verifyTls ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, request.options.verifyTls ? 2L : 0L);

    if (hasDeadline) {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(remaining > 1 ? remaining : 1));
    }

    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, onHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);  // output goes to onDebug, not stderr
    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, onDebug);
    curl_easy_setopt(curl, CURLOPT_DEBUGDATA, &transfer);

    const bool hasBody = !request.body.empty();
    if (request.method == "HEAD") {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    } else if (hasBody || request.method == "POST" || request.method == "PUT" ||
               request.method == "PATCH") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
    }
    if (request.method != "HEAD") curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());

    curl_slist* headers = nullptr;
    for (const auto& [name, value] : request.headers) {
        // "Name:" would tell curl to remove the header; "Name;" sends it empty.
        const std::string line = value.empty() ? name + ";" : name + ": " + value;
        headers = curl_slist_append(headers, line.c_str());
    }
    // Stop curl from adding headers the user did not ask for.
    if (!findHeader(request.headers, "Expect")) headers = curl_slist_append(headers, "Expect:");
    if (!findHeader(request.headers, "Content-Type")) headers = curl_slist_append(headers, "Content-Type:");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    const CURLcode code = curl_easy_perform(curl);

    if (code == CURLE_ABORTED_BY_CALLBACK) {
        response.cancelled = true;
        response.error = "Request cancelled.";
    } else if (code == CURLE_WRITE_ERROR && response.truncated) {
        // We stopped reading on purpose once the size limit was hit.
    } else if (code == CURLE_OPERATION_TIMEDOUT) {
        response.error = "The request timed out.";
    } else if (code != CURLE_OK) {
        response.error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(code);
    }

    response.sentLog += transfer.sent;

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

}  // namespace fetcher::detail
