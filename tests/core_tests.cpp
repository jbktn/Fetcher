// Tests for the GUI-free core, including real HTTP exchanges against a small
// server running on the loopback interface.
#include "core/http_client.h"
#include "core/serialize.h"
#include "core/store.h"
#include "core/text.h"
#include "core/url.h"
#include "core/workspace.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
static void closeSocket(SocketHandle s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
static const int INVALID_SOCKET = -1;
static void closeSocket(SocketHandle s) { close(s); }
#endif

using namespace fetcher;

namespace {

int failures = 0;
int checks = 0;

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        ++checks;                                                                           \
        if (!(condition)) {                                                                 \
            ++failures;                                                                     \
            std::cerr << __FILE__ << ":" << __LINE__ << ": failed: " #condition << "\n";    \
        }                                                                                   \
    } while (false)

#define CHECK_EQ(actual, expected)                                                          \
    do {                                                                                    \
        ++checks;                                                                           \
        const auto actualValue = (actual);                                                  \
        const auto expectedValue = (expected);                                              \
        if (!(actualValue == expectedValue)) {                                              \
            ++failures;                                                                     \
            std::cerr << __FILE__ << ":" << __LINE__ << ": " #actual " was [" << actualValue \
                      << "], expected [" << expectedValue << "]\n";                         \
        }                                                                                   \
    } while (false)

// Answers each connection using a handler that sees the raw request text.
class TestServer {
public:
    using Handler = std::function<std::string(const std::string& request)>;

    explicit TestServer(Handler handler) : handler_(std::move(handler)) {
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        listen(listener_, 16);
        socklen_t length = sizeof(address);
        getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length);
        port_ = ntohs(address.sin_port);
        thread_ = std::thread([this] { serve(); });
    }

    ~TestServer() {
        stopping_ = true;
        // Wake accept() with one last connection.
        SocketHandle wake = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<unsigned short>(port_));
        connect(wake, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        closeSocket(wake);
        thread_.join();
        closeSocket(listener_);
    }

    std::string url(const std::string& path, const char* host = "127.0.0.1") const {
        return std::string("http://") + host + ":" + std::to_string(port_) + path;
    }

private:
    void serve() {
        std::vector<std::thread> workers;
        while (!stopping_) {
            SocketHandle client = accept(listener_, nullptr, nullptr);
            if (client == INVALID_SOCKET || stopping_) {
                if (client != INVALID_SOCKET) closeSocket(client);
                break;
            }
            workers.emplace_back([this, client] { respond(client); });
        }
        for (auto& worker : workers) worker.join();
    }

    void respond(SocketHandle client) {
        std::string request;
        char buffer[8192];
        std::size_t expectedTotal = std::string::npos;
        while (request.size() < expectedTotal) {
            const auto received = recv(client, buffer, sizeof(buffer), 0);
            if (received <= 0) break;
            request.append(buffer, static_cast<std::size_t>(received));
            const auto headerEnd = request.find("\r\n\r\n");
            if (headerEnd != std::string::npos && expectedTotal == std::string::npos) {
                std::size_t contentLength = 0;
                std::string lower = request.substr(0, headerEnd);
                for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                const auto position = lower.find("content-length:");
                if (position != std::string::npos)
                    contentLength = static_cast<std::size_t>(std::strtoul(lower.c_str() + position + 15, nullptr, 10));
                expectedTotal = headerEnd + 4 + contentLength;
            }
        }
        const std::string response = handler_(request);
        std::size_t sent = 0;
        while (sent < response.size()) {
            const auto written = send(client, response.data() + sent, static_cast<int>(response.size() - sent), 0);
            if (written <= 0) break;
            sent += static_cast<std::size_t>(written);
        }
#ifdef _WIN32
        shutdown(client, SD_SEND);
#else
        shutdown(client, SHUT_WR);
#endif
        closeSocket(client);
    }

    Handler handler_;
    SocketHandle listener_;
    int port_ = 0;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
};

std::string reply(const std::string& statusLine, const std::string& body, const std::string& extraHeaders = "") {
    return "HTTP/1.1 " + statusLine + "\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\nConnection: close\r\n" + extraHeaders + "\r\n" + body;
}

std::string echoBack(const std::string& request) {
    return reply("200 OK", request, "Content-Type: text/plain\r\nX-Test: one\r\nX-Test-Two: two\r\n");
}

bool containsIgnoreCase(std::string haystack, std::string needle) {
    for (auto& c : haystack) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (auto& c : needle) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return haystack.find(needle) != std::string::npos;
}

HttpResponse send(const Request& request) {
    std::atomic<bool> cancel{false};
    return sendRequest(prepareRequest(request), cancel);
}

Request requestTo(const std::string& url, const std::string& method = "GET") {
    Request request;
    request.method = method;
    request.url = url;
    return request;
}

void testUrlHelpers() {
    CHECK_EQ(percentEncode("a b&c=d/e"), std::string("a%20b%26c%3Dd%2Fe"));
    CHECK_EQ(percentDecode("a%20b+c", true), std::string("a b c"));
    CHECK_EQ(percentDecode("a%20b+c", false), std::string("a b+c"));
    CHECK_EQ(percentDecode("100%", true), std::string("100%"));
    CHECK_EQ(percentDecode("%4", true), std::string("%4"));
    CHECK_EQ(percentDecode("x%41", true), std::string("xA"));

    auto params = parseQuery("https://x.test/p?a=1&b=two%20words&flag#frag");
    CHECK_EQ(params.size(), std::size_t{3});
    CHECK_EQ(params[0].key, std::string("a"));
    CHECK_EQ(params[1].value, std::string("two words"));
    CHECK_EQ(params[2].key, std::string("flag"));
    CHECK_EQ(params[2].value, std::string(""));
    CHECK(parseQuery("https://x.test/p").empty());

    std::vector<KeyValue> rows{{"q", "a&b", true}, {"off", "1", false}, {"flag", "", true}};
    CHECK_EQ(withQuery("https://x.test/p?old=1#top", rows), std::string("https://x.test/p?q=a%26b&flag#top"));
    CHECK_EQ(withQuery("https://x.test/p?old=1", {}), std::string("https://x.test/p"));

    std::vector<KeyValue> previous{{"a", "1", true}, {"keep", "off", false}, {"b", "2", true}};
    auto merged = mergeParams("http://x/?a=9&c=3&d=4", previous);
    CHECK_EQ(merged.size(), std::size_t{4});
    CHECK_EQ(merged[0].value, std::string("9"));
    CHECK_EQ(merged[1].key, std::string("keep"));
    CHECK(!merged[1].enabled);
    CHECK_EQ(merged[2].key, std::string("c"));
    CHECK_EQ(merged[3].key, std::string("d"));

    CHECK_EQ(normalizeUrl("  localhost:3000/a b  "), std::string("http://localhost:3000/a%20b"));
    CHECK_EQ(normalizeUrl("https://x.test/p?q=%20ok#frag"), std::string("https://x.test/p?q=%20ok"));
    CHECK_EQ(normalizeUrl("https://x.test/\xC5\xBC"), std::string("https://x.test/%C5%BC"));
    CHECK_EQ(normalizeUrl(""), std::string(""));

    UrlParts parts;
    CHECK(parseUrl("HTTPS://User@Example.com:8443/a?b=1#c", parts));
    CHECK_EQ(parts.scheme, std::string("https"));
    CHECK_EQ(parts.host, std::string("example.com"));
    CHECK_EQ(parts.port, 8443);
    CHECK_EQ(parts.path, std::string("/a?b=1"));
    CHECK(parseUrl("http://[::1]:81", parts));
    CHECK_EQ(parts.host, std::string("[::1]"));
    CHECK_EQ(parts.port, 81);
    CHECK_EQ(parts.path, std::string("/"));
    CHECK(parseUrl("https://x.test", parts));
    CHECK_EQ(parts.port, 443);
    CHECK(!parseUrl("no-scheme", parts));

    const std::string base = "https://x.test:1/a/b/c?q=1";
    CHECK_EQ(resolveUrl(base, "http://y.test/z"), std::string("http://y.test/z"));
    CHECK_EQ(resolveUrl(base, "//y.test/z"), std::string("https://y.test/z"));
    CHECK_EQ(resolveUrl(base, "/z"), std::string("https://x.test:1/z"));
    CHECK_EQ(resolveUrl(base, "z"), std::string("https://x.test:1/a/b/z"));
    CHECK_EQ(resolveUrl(base, "../z?r=2"), std::string("https://x.test:1/a/z?r=2"));
    CHECK_EQ(resolveUrl(base, "../../../z"), std::string("https://x.test:1/z"));
    CHECK_EQ(resolveUrl(base, "./"), std::string("https://x.test:1/a/b/"));
    CHECK_EQ(resolveUrl(base, "?r=2"), std::string("https://x.test:1/a/b/c?r=2"));
    CHECK_EQ(resolveUrl(base, "/next?u=http://y.test"), std::string("https://x.test:1/next?u=http://y.test"));

    CHECK(sameOrigin("https://x.test/a", "https://X.test:443/b"));
    CHECK(!sameOrigin("https://x.test/a", "http://x.test/a"));
    CHECK(!sameOrigin("https://x.test/a", "https://y.test/a"));
    CHECK(!sameOrigin("https://x.test/a", "https://x.test:8443/a"));
}

void testText() {
    CHECK_EQ(base64Encode(""), std::string(""));
    CHECK_EQ(base64Encode("f"), std::string("Zg=="));
    CHECK_EQ(base64Encode("fo"), std::string("Zm8="));
    CHECK_EQ(base64Encode("foo"), std::string("Zm9v"));
    CHECK_EQ(base64Encode("user:p\xC3\xA4ss"), std::string("dXNlcjpww6Rzcw=="));

    CHECK(!prettyJson("{not json").has_value());
    CHECK(!prettyJson("").has_value());
    CHECK_EQ(*prettyJson("{\"a\":1}"), std::string("{\n  \"a\": 1\n}"));
    CHECK_EQ(*prettyJson("[]"), std::string("[]"));
    CHECK_EQ(*prettyJson("{ }"), std::string("{}"));
    CHECK_EQ(*prettyJson(" [1, [2,{\"x\":null}]] "),
             std::string("[\n  1,\n  [\n    2,\n    {\n      \"x\": null\n    }\n  ]\n]"));
    // Key order and exact number text must survive.
    CHECK_EQ(*prettyJson("{\"z\":1.10,\"a\":123456789012345678901234567890}"),
             std::string("{\n  \"z\": 1.10,\n  \"a\": 123456789012345678901234567890\n}"));
    // Structural characters inside strings are not structure.
    CHECK_EQ(*prettyJson("{\"a\":\"[,{\\\":}]\"}"), std::string("{\n  \"a\": \"[,{\\\":}]\"\n}"));

    CHECK_EQ(prettyMarkup("", true), std::string());
    CHECK_EQ(prettyMarkup("<!doctype html><html><body class=\"x\"><h1>Hello</h1><br><img src=a></body></html>", true),
             std::string("<!doctype html>\n<html>\n  <body class=\"x\">\n    <h1>Hello</h1>\n    <br>\n"
                         "    <img src=a>\n  </body>\n</html>"));
    // Blank lines go, and a tag spread over lines is pulled together. Space and '>'
    // inside quotes belong to the value.
    CHECK_EQ(prettyMarkup("\n\n<html\n  lang=\"en\"\n\n  title=\"a  > b\"\n  >\n\n<p>x</p>\n\n</html>\n", true),
             std::string("<html lang=\"en\" title=\"a  > b\">\n  <p>x</p>\n</html>"));
    // Case doesn't matter in HTML, and some end tags can be left out.
    CHECK_EQ(prettyMarkup("<UL><li>a<li>b</ul><DIV></div>", true),
             std::string("<UL>\n  <li>\n    a\n  <li>\n    b\n</ul>\n<DIV></div>"));
    // Scripts and the like aren't markup. <pre> keeps its whitespace.
    CHECK_EQ(prettyMarkup("<div><script>if (a<b) { c(\"<p>\"); }</SCRIPT><pre> x\n  y </pre></div>", true),
             std::string("<div>\n  <script>\n    if (a<b) { c(\"<p>\"); }\n  </SCRIPT>\n  <pre> x\n  y </pre>\n</div>"));
    CHECK_EQ(prettyMarkup("<script src=\"a.js\"></script>", true), std::string("<script src=\"a.js\"></script>"));
    // Broken markup still comes out, with nothing lost.
    CHECK_EQ(prettyMarkup("</b>a < b<div", true), std::string("</b>\na\n< b\n<div"));
    CHECK_EQ(prettyMarkup("<div><span></div><p>x", true), std::string("<div>\n  <span>\n</div>\n<p>\n  x"));
    CHECK_EQ(prettyMarkup("<!-- a\n b --><p>", true), std::string("<!-- a\nb -->\n<p>"));
    // In XML, case matters, nothing is implicit and CDATA stays as it is.
    CHECK_EQ(prettyMarkup("<?xml version=\"1.0\"?><a><B/><b>x</b><br><![CDATA[ <y> ]]></br></a>", false),
             std::string("<?xml version=\"1.0\"?>\n<a>\n  <B/>\n  <b>x</b>\n  <br>\n    <![CDATA[ <y> ]]>\n  </br>\n</a>"));
    // Indent stops growing, so very deep input can't blow up the output.
    {
        std::string deep;
        for (int i = 0; i < 20000; ++i) deep += "<a>";
        CHECK(prettyMarkup(deep, false).size() < deep.size() * 40);
    }

    CHECK(isValidUtf8("plain"));
    CHECK(isValidUtf8("\xC5\xBC\xE2\x82\xAC\xF0\x9F\x98\x80"));
    CHECK(!isValidUtf8("\xC5"));
    CHECK(!isValidUtf8("\xFF\xFE"));
    CHECK(!isValidUtf8("\xC0\x80"));        // overlong
    CHECK(!isValidUtf8("\xED\xA0\x80"));    // surrogate
    CHECK(!isValidUtf8("\xF4\x90\x80\x80"));  // past U+10FFFF

    CHECK(looksBinary(std::string_view("a\0b", 3)));
    CHECK(!looksBinary("text"));

    CHECK_EQ(mimeTypeOf("Application/JSON; charset=UTF-8"), std::string("application/json"));
    CHECK_EQ(charsetOf("Application/JSON; Charset=\"UTF-8\"; x=y"), std::string("utf-8"));
    CHECK_EQ(charsetOf("text/plain"), std::string(""));

    CHECK_EQ(std::string(statusReason(404)), std::string("Not Found"));
    CHECK_EQ(formatBytes(512), std::string("512 B"));
    CHECK_EQ(formatBytes(2048), std::string("2.0 KB"));
    CHECK_EQ(formatDuration(12.4), std::string("12 ms"));
    CHECK_EQ(formatDuration(1500), std::string("1.50 s"));
}

void testPrepare() {
    Request request = requestTo(" example.test/a b?x=1 ", " post ");
    request.headers = {{"X-On", "1", true}, {"X-Off", "2", false}, {"", "orphan", true}};
    request.bodyType = BodyType::Json;
    request.body = "{}";
    auto prepared = prepareRequest(request);
    CHECK_EQ(prepared.method, std::string("POST"));
    CHECK_EQ(prepared.url, std::string("http://example.test/a%20b?x=1"));
    CHECK(findHeader(prepared.headers, "x-on"));
    CHECK(!findHeader(prepared.headers, "X-Off"));
    CHECK_EQ(*findHeader(prepared.headers, "Content-Type"), std::string("application/json"));
    CHECK_EQ(prepared.body, std::string("{}"));
    CHECK(findHeader(prepared.headers, "User-Agent"));

    // A Content-Type the user set wins over the automatic one.
    request.headers = {{"content-type", "application/vnd.api+json", true}};
    prepared = prepareRequest(request);
    CHECK_EQ(*findHeader(prepared.headers, "Content-Type"), std::string("application/vnd.api+json"));

    Request form = requestTo("http://x.test/");
    form.bodyType = BodyType::Form;
    form.form = {{"a b", "c&d", true}, {"off", "x", false}, {"e", "", true}};
    prepared = prepareRequest(form);
    CHECK_EQ(prepared.body, std::string("a%20b=c%26d&e="));
    CHECK_EQ(*findHeader(prepared.headers, "Content-Type"), std::string("application/x-www-form-urlencoded"));

    // A body left in the editor is not sent when the type is "No body".
    Request none = requestTo("http://x.test/");
    none.body = "leftover";
    CHECK(prepareRequest(none).body.empty());

    Request auth = requestTo("http://x.test/");
    auth.auth.type = AuthType::Bearer;
    auth.auth.token = "tok";
    CHECK_EQ(*findHeader(prepareRequest(auth).headers, "Authorization"), std::string("Bearer tok"));

    auth.auth.type = AuthType::Basic;
    auth.auth.username = "foo";
    auth.auth.password = "bar";
    CHECK_EQ(*findHeader(prepareRequest(auth).headers, "Authorization"), std::string("Basic Zm9vOmJhcg=="));

    // An explicit Authorization header is not overwritten by the Auth tab.
    auth.headers = {{"Authorization", "Custom x", true}};
    CHECK_EQ(*findHeader(prepareRequest(auth).headers, "Authorization"), std::string("Custom x"));
    auth.headers.clear();

    auth.auth.type = AuthType::ApiKey;
    auth.auth.apiKeyName = "X-Key";
    auth.auth.apiKeyValue = "secret";
    CHECK_EQ(*findHeader(prepareRequest(auth).headers, "X-Key"), std::string("secret"));

    auth.auth.apiKeyName = "api key";
    auth.auth.apiKeyValue = "a&b";
    auth.auth.apiKeyInQuery = true;
    CHECK_EQ(prepareRequest(auth).url, std::string("http://x.test/?api%20key=a%26b"));
    auth.url = "http://x.test/?a=1";
    CHECK_EQ(prepareRequest(auth).url, std::string("http://x.test/?a=1&api%20key=a%26b"));
}

void testWorkspace() {
    Workspace workspace;
    const std::string folderA = workspace.addFolder(Workspace::kRootId, "A");
    const std::string folderB = workspace.addFolder(folderA, "B");
    CHECK(!folderA.empty());
    CHECK(workspace.addFolder("missing", "x").empty());

    Request request = requestTo("http://x.test/");
    request.name = "One";
    const std::string id = workspace.addRequest(folderB, request);
    CHECK(!id.empty());
    CHECK(workspace.findRequest(id));
    CHECK_EQ(workspace.parentOfRequest(id)->id, folderB);

    const std::string copy = workspace.duplicateRequest(id);
    CHECK(copy != id);
    CHECK_EQ(workspace.findRequest(copy)->name, std::string("One copy"));
    CHECK_EQ(workspace.findFolder(folderB)->requests[1].id, copy);

    Request edited = *workspace.findRequest(id);
    edited.method = "PUT";
    CHECK(workspace.updateRequest(edited));
    CHECK_EQ(workspace.findRequest(id)->method, std::string("PUT"));

    CHECK(workspace.moveRequest(id, Workspace::kRootId));
    CHECK_EQ(workspace.parentOfRequest(id)->id, std::string(Workspace::kRootId));
    CHECK(!workspace.moveRequest(id, "missing"));

    // A folder cannot move into itself or its own subtree.
    CHECK(!workspace.moveFolder(folderA, folderB));
    CHECK(!workspace.moveFolder(folderA, folderA));
    CHECK(workspace.moveFolder(folderB, Workspace::kRootId));
    CHECK_EQ(workspace.parentOfFolder(folderB)->id, std::string(Workspace::kRootId));
    CHECK_EQ(workspace.findFolder(folderB)->requests.size(), std::size_t{1});
    CHECK_EQ(workspace.findFolder(folderB)->name, std::string("B"));
    CHECK(workspace.findFolder(folderA)->folders.empty());

    CHECK(!workspace.renameFolder(Workspace::kRootId, "x"));
    CHECK(workspace.renameFolder(folderB, "C"));
    CHECK_EQ(workspace.findFolder(folderB)->name, std::string("C"));

    // Importing gives every item a new id, even when imported twice.
    Folder imported = *workspace.findFolder(folderB);
    const std::string first = workspace.importFolder(Workspace::kRootId, imported);
    const std::string second = workspace.importFolder(Workspace::kRootId, imported);
    CHECK(first != second);
    CHECK(first != folderB);
    CHECK(workspace.findFolder(first)->requests[0].id != copy);
    CHECK(workspace.findFolder(first)->requests[0].id != workspace.findFolder(second)->requests[0].id);

    CHECK(workspace.removeRequest(copy));
    CHECK(!workspace.findRequest(copy));
    CHECK(workspace.removeFolder(folderB));
    CHECK(!workspace.findFolder(folderB));
    CHECK(!workspace.removeFolder(Workspace::kRootId));
}

Request sampleRequest() {
    Request request;
    request.id = "abc";
    request.name = "N\xC3\xA4me \"quoted\"";
    request.method = "PATCH";
    request.url = "https://x.test/p?a=1";
    request.params = {{"a", "1", true}, {"b", "2", false}};
    request.headers = {{"X-A", "b", true}};
    request.bodyType = BodyType::Form;
    request.body = "{\n\t\"k\": 1.10\n}\n";
    request.form = {{"f", "v", true}};
    request.auth.type = AuthType::ApiKey;
    request.auth.token = "t";
    request.auth.username = "u";
    request.auth.password = "p";
    request.auth.apiKeyName = "k";
    request.auth.apiKeyValue = "v";
    request.auth.apiKeyInQuery = true;
    request.options.followRedirects = false;
    request.options.verifyTls = false;
    request.options.timeoutSeconds = 7;
    return request;
}

void testSerialization() {
    Folder root;
    root.id = "root";
    Folder child;
    child.id = "c";
    child.name = "Child";
    child.requests.push_back(sampleRequest());
    root.folders.push_back(child);
    root.requests.push_back(Request{});

    const Folder back = folderFromJson(folderToJson(root));
    CHECK_EQ(back.folders.size(), std::size_t{1});
    CHECK_EQ(back.folders[0].name, std::string("Child"));
    CHECK(back.folders[0].requests[0] == sampleRequest());
    CHECK(back.requests[0] == Request{});

    // Missing fields fall back to defaults, so older files keep loading.
    const Folder sparse = folderFromJson(R"({"version":1,"root":{"requests":[{"url":"http://x"}]}})");
    CHECK_EQ(sparse.requests[0].url, std::string("http://x"));
    CHECK_EQ(sparse.requests[0].method, std::string("GET"));
    CHECK(sparse.requests[0].options.verifyTls);

    const auto rejects = [](const std::string& text) {
        try {
            folderFromJson(text);
            return false;
        } catch (const std::runtime_error&) {
            return true;
        }
    };
    CHECK(rejects("not json"));
    CHECK(rejects("[]"));
    CHECK(rejects(R"({"version":99,"root":{}})"));
    CHECK(rejects(R"({"root":{}})"));
    CHECK(rejects(R"({"version":1})"));
    CHECK(rejects(R"({"version":1,"root":{"requests":[{"url":5}]}})"));

    std::vector<HistoryEntry> history(1);
    history[0].request = sampleRequest();
    history[0].status = 201;
    history[0].error = "e";
    history[0].sentAt = 1700000000;
    history[0].totalMs = 12.5;
    const auto historyBack = historyFromJson(historyToJson(history));
    CHECK_EQ(historyBack.size(), std::size_t{1});
    CHECK(historyBack[0].request == sampleRequest());
    CHECK_EQ(historyBack[0].status, 201L);
    CHECK_EQ(historyBack[0].sentAt, std::int64_t{1700000000});
}

void testStore(const std::filesystem::path& directory) {
    namespace fs = std::filesystem;
    fs::remove_all(directory);

    Store store(directory / "nested" / "data");
    Workspace workspace;
    CHECK(store.load(workspace).empty());  // missing file is not an error

    workspace.addRequest(Workspace::kRootId, sampleRequest());
    store.save(workspace);
    CHECK(fs::exists(store.directory() / "collections.json"));
    CHECK(!fs::exists(store.directory() / "collections.json.tmp"));

    Workspace loaded;
    CHECK(store.load(loaded).empty());
    CHECK_EQ(loaded.root().requests.size(), std::size_t{1});
    CHECK_EQ(loaded.root().requests[0].url, sampleRequest().url);

#ifndef _WIN32
    const auto perms = fs::status(store.directory() / "collections.json").permissions();
    CHECK((perms & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
#endif

    std::vector<HistoryEntry> history(2);
    history[1].status = 500;
    store.save(history);
    std::vector<HistoryEntry> historyBack;
    CHECK(store.load(historyBack).empty());
    CHECK_EQ(historyBack.size(), std::size_t{2});
    CHECK_EQ(historyBack[1].status, 500L);

    // A damaged file must be kept aside, not overwritten by the next save.
    writeFileAtomic(store.directory() / "collections.json", "{broken");
    Workspace afterDamage;
    const std::string problem = store.load(afterDamage);
    CHECK(!problem.empty());
    CHECK(afterDamage.root().requests.empty());
    CHECK(!fs::exists(store.directory() / "collections.json"));
    int keptAside = 0;
    for (const auto& entry : fs::directory_iterator(store.directory())) {
        if (entry.path().filename().u8string().rfind("collections.json.damaged-", 0) == 0) {
            ++keptAside;
            CHECK_EQ(readFile(entry.path()), std::string("{broken"));
        }
    }
    CHECK_EQ(keptAside, 1);

    fs::remove_all(directory);
}

void testHttpBasics() {
    TestServer server(echoBack);

    auto response = send(requestTo(server.url("/hello?x=1")));
    CHECK_EQ(response.error, std::string(""));
    CHECK(response.received);
    CHECK_EQ(response.status, 200L);
    CHECK_EQ(response.statusText, std::string("OK"));
    CHECK(response.body.rfind("GET /hello?x=1 HTTP/1.1\r\n", 0) == 0);
    CHECK_EQ(*findHeader(response.headers, "X-Test"), std::string("one"));
    CHECK_EQ(*findHeader(response.headers, "x-test-two"), std::string("two"));
    CHECK(response.totalMs > 0);
    CHECK(response.sentLog.find("GET /hello?x=1") != std::string::npos);
    CHECK(containsIgnoreCase(response.body, "user-agent: fetcher/"));

    for (const char* method : {"POST", "PUT", "PATCH", "DELETE", "OPTIONS", "PROPFIND"}) {
        Request request = requestTo(server.url("/m"), method);
        request.bodyType = BodyType::Json;
        request.body = "{\"hello\":\"w\xC3\xB6rld\"}";
        response = send(request);
        CHECK_EQ(response.status, 200L);
        CHECK(response.body.rfind(std::string(method) + " /m HTTP/1.1\r\n", 0) == 0);
        CHECK(response.body.find(request.body) != std::string::npos);
        CHECK(containsIgnoreCase(response.body, "content-type: application/json"));
    }

    // HEAD returns headers but never a body.
    response = send(requestTo(server.url("/h"), "HEAD"));
    CHECK_EQ(response.status, 200L);
    CHECK(response.body.empty());

    // An empty POST is valid and declares its length.
    response = send(requestTo(server.url("/empty"), "POST"));
    CHECK_EQ(response.status, 200L);
    CHECK(containsIgnoreCase(response.body, "content-length: 0"));
    CHECK(!containsIgnoreCase(response.body, "content-type:"));

    // Large bodies arrive without curl's "Expect: 100-continue" stall.
    Request large = requestTo(server.url("/big"), "POST");
    large.bodyType = BodyType::Text;
    large.body.assign(300000, 'x');
    response = send(large);
    CHECK_EQ(response.status, 200L);
    CHECK(response.body.find(large.body) != std::string::npos);
    CHECK(!containsIgnoreCase(response.body.substr(0, 1000), "expect:"));

    // Custom headers replace defaults, and repeats are all sent.
    Request custom = requestTo(server.url("/c"));
    custom.headers = {{"User-Agent", "my-agent", true}, {"X-Multi", "one", true}, {"X-Multi", "two", true}};
    response = send(custom);
    CHECK(containsIgnoreCase(response.body, "user-agent: my-agent"));
    CHECK(!containsIgnoreCase(response.body, "user-agent: fetcher"));
    CHECK(containsIgnoreCase(response.body, "x-multi: one"));
    CHECK(containsIgnoreCase(response.body, "x-multi: two"));
}

void testHttpStatusesAndBodies() {
    TestServer server([](const std::string& request) -> std::string {
        if (request.find(" /missing ") != std::string::npos) return reply("404 Not Found", "nope");
        if (request.find(" /error ") != std::string::npos) return reply("500 Internal Server Error", "boom");
        if (request.find(" /noreason ") != std::string::npos) return "HTTP/1.1 204\r\nConnection: close\r\n\r\n";
        if (request.find(" /binary ") != std::string::npos)
            return reply("200 OK", std::string("\x00\x01\x02\xFF", 4), "Content-Type: application/octet-stream\r\n");
        if (request.find(" /large ") != std::string::npos) return reply("200 OK", std::string(4096, 'y'));
        return reply("200 OK", "ok");
    });

    auto response = send(requestTo(server.url("/missing")));
    CHECK_EQ(response.status, 404L);
    CHECK_EQ(response.statusText, std::string("Not Found"));
    CHECK_EQ(response.body, std::string("nope"));
    CHECK_EQ(response.error, std::string(""));

    response = send(requestTo(server.url("/error")));
    CHECK_EQ(response.status, 500L);

    response = send(requestTo(server.url("/noreason")));
    CHECK_EQ(response.status, 204L);
    CHECK_EQ(response.statusText, std::string("No Content"));

    response = send(requestTo(server.url("/binary")));
    CHECK_EQ(response.body.size(), std::size_t{4});
    CHECK(looksBinary(response.body));

    std::atomic<bool> cancel{false};
    response = sendRequest(prepareRequest(requestTo(server.url("/large"))), cancel, 1000);
    CHECK(response.truncated);
    CHECK_EQ(response.status, 200L);
    CHECK_EQ(response.body.size(), std::size_t{1000});
    CHECK_EQ(response.error, std::string(""));
}

void testRedirects() {
    std::atomic<int> otherHits{0};
    TestServer other([&](const std::string& request) {
        ++otherHits;
        return echoBack(request);
    });
    const std::string otherUrl = other.url("/landed");

    TestServer server([&](const std::string& request) -> std::string {
        const auto target = [&](const char* status, const std::string& where) {
            return reply(status, "", "Location: " + where + "\r\n");
        };
        if (request.find(" /r302 ") != std::string::npos) return target("302 Found", "/final");
        if (request.find(" /r303 ") != std::string::npos) return target("303 See Other", "/final");
        if (request.find(" /r307 ") != std::string::npos) return target("307 Temporary Redirect", "/final");
        if (request.find(" /relative/a ") != std::string::npos) return target("301 Moved Permanently", "../final");
        if (request.find(" /cross ") != std::string::npos) return target("302 Found", otherUrl);
        if (request.find(" /loop ") != std::string::npos) return target("302 Found", "/loop");
        return echoBack(request);
    });

    auto response = send(requestTo(server.url("/r302")));
    CHECK_EQ(response.status, 200L);
    CHECK_EQ(response.redirects, 1);
    CHECK(response.body.rfind("GET /final ", 0) == 0);
    CHECK_EQ(response.finalUrl, server.url("/final"));
    // The log shows both hops.
    CHECK(response.sentLog.find("GET /r302") != std::string::npos);
    CHECK(response.sentLog.find("GET /final") != std::string::npos);

    response = send(requestTo(server.url("/relative/a")));
    CHECK(response.body.rfind("GET /final ", 0) == 0);

    // POST becomes GET and loses its body on 302 and 303.
    for (const char* path : {"/r302", "/r303"}) {
        Request post = requestTo(server.url(path), "POST");
        post.bodyType = BodyType::Json;
        post.body = "{\"gone\":true}";
        response = send(post);
        CHECK(response.body.rfind("GET /final ", 0) == 0);
        CHECK(response.body.find("gone") == std::string::npos);
        CHECK(!containsIgnoreCase(response.body, "content-type:"));
    }

    // 303 turns other methods into GET as well.
    response = send(requestTo(server.url("/r303"), "PUT"));
    CHECK(response.body.rfind("GET /final ", 0) == 0);

    // 307 keeps the method and body.
    Request post = requestTo(server.url("/r307"), "POST");
    post.bodyType = BodyType::Json;
    post.body = "{\"kept\":true}";
    response = send(post);
    CHECK(response.body.rfind("POST /final ", 0) == 0);
    CHECK(response.body.find("kept") != std::string::npos);

    // Redirects can be turned off.
    Request manual = requestTo(server.url("/r302"));
    manual.options.followRedirects = false;
    response = send(manual);
    CHECK_EQ(response.status, 302L);
    CHECK_EQ(response.redirects, 0);
    CHECK_EQ(*findHeader(response.headers, "Location"), std::string("/final"));

    // Credentials stay on the original site, other headers travel on.
    Request secret = requestTo(server.url("/cross"));
    secret.auth.type = AuthType::Bearer;
    secret.auth.token = "top-secret";
    secret.headers = {{"Cookie", "session=1", true}, {"X-Keep", "yes", true}};
    response = send(secret);
    CHECK_EQ(response.status, 200L);
    CHECK(otherHits.load() == 1);
    CHECK(response.body.rfind("GET /landed ", 0) == 0);
    CHECK(!containsIgnoreCase(response.body, "authorization:"));
    CHECK(response.body.find("top-secret") == std::string::npos);
    CHECK(!containsIgnoreCase(response.body, "cookie:"));
    CHECK(containsIgnoreCase(response.body, "x-keep: yes"));

    // Same-site redirects keep them.
    Request sameSite = requestTo(server.url("/r302"));
    sameSite.auth.type = AuthType::Bearer;
    sameSite.auth.token = "still-here";
    response = send(sameSite);
    CHECK(response.body.find("still-here") != std::string::npos);

    // A redirect loop stops instead of hanging, and says why.
    response = send(requestTo(server.url("/loop")));
    CHECK_EQ(response.status, 302L);
    CHECK_EQ(response.redirects, 20);
    CHECK(response.error.find("redirects") != std::string::npos);
}

void testHttpFailures() {
    using Clock = std::chrono::steady_clock;

    std::atomic<bool> releaseServer{false};
    TestServer slow([&](const std::string&) {
        const auto start = Clock::now();
        while (!releaseServer && Clock::now() - start < std::chrono::seconds(10))
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return reply("200 OK", "late");
    });

    // The timeout is honoured.
    Request timed = requestTo(slow.url("/slow"));
    timed.options.timeoutSeconds = 1;
    auto start = Clock::now();
    auto response = send(timed);
    auto elapsed = Clock::now() - start;
    CHECK(!response.error.empty());
    CHECK(response.error.find("timed out") != std::string::npos);
    CHECK(!response.cancelled);
    CHECK(elapsed >= std::chrono::milliseconds(900));
    CHECK(elapsed < std::chrono::seconds(5));

    // Cancelling from another thread returns promptly.
    std::atomic<bool> cancel{false};
    std::thread canceller([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        cancel = true;
    });
    start = Clock::now();
    response = sendRequest(prepareRequest(requestTo(slow.url("/slow"))), cancel);
    elapsed = Clock::now() - start;
    canceller.join();
    CHECK(response.cancelled);
    CHECK(!response.received);
    CHECK(elapsed < std::chrono::seconds(4));
    releaseServer = true;

    // Nothing is listening on this port: bind one, note it, close it.
    std::string deadUrl;
    {
        TestServer closed(echoBack);
        deadUrl = closed.url("/");
    }
    response = send(requestTo(deadUrl));
    CHECK(!response.received);
    CHECK(!response.error.empty());
    CHECK(!response.cancelled);

    // Only http and https are allowed, so a saved request can't read local files.
    response = send(requestTo("file:///etc/passwd"));
    CHECK(!response.received);
    CHECK(!response.error.empty());
    response = send(requestTo("ftp://127.0.0.1/"));
    CHECK(!response.received);
    CHECK(!response.error.empty());
}

}  // namespace

int main() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    const auto directory = std::filesystem::temp_directory_path() / ("fetcher-tests-" + newId());

    testUrlHelpers();
    testText();
    testPrepare();
    testWorkspace();
    testSerialization();
    testStore(directory);
    testHttpBasics();
    testHttpStatusesAndBodies();
    testRedirects();
    testHttpFailures();

    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
