// Plain data types shared by the whole app. Nothing here depends on the GUI
// toolkit, so the core can be unit tested without a display.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fetcher {

struct KeyValue {
    std::string key;
    std::string value;
    bool enabled = true;

    bool operator==(const KeyValue& o) const {
        return key == o.key && value == o.value && enabled == o.enabled;
    }
    bool operator!=(const KeyValue& o) const { return !(*this == o); }
};

enum class BodyType { None, Json, Xml, Text, Form };
enum class AuthType { None, Bearer, Basic, ApiKey };

struct Auth {
    AuthType type = AuthType::None;
    std::string token;                // Bearer
    std::string username, password;   // Basic
    std::string apiKeyName, apiKeyValue;
    bool apiKeyInQuery = false;       // otherwise sent as a header

    bool operator==(const Auth& o) const {
        return type == o.type && token == o.token && username == o.username &&
               password == o.password && apiKeyName == o.apiKeyName &&
               apiKeyValue == o.apiKeyValue && apiKeyInQuery == o.apiKeyInQuery;
    }
    bool operator!=(const Auth& o) const { return !(*this == o); }
};

struct RequestOptions {
    bool followRedirects = true;
    bool verifyTls = true;
    int timeoutSeconds = 30;  // whole exchange, redirects included; 0 = no limit

    bool operator==(const RequestOptions& o) const {
        return followRedirects == o.followRedirects && verifyTls == o.verifyTls &&
               timeoutSeconds == o.timeoutSeconds;
    }
    bool operator!=(const RequestOptions& o) const { return !(*this == o); }
};

struct Request {
    std::string id;  // empty until saved to a collection
    std::string name;
    std::string method = "GET";
    std::string url;  // includes the query string of the enabled params
    std::vector<KeyValue> params;
    std::vector<KeyValue> headers;
    BodyType bodyType = BodyType::None;
    std::string body;            // Json, Xml and Text bodies
    std::vector<KeyValue> form;  // Form bodies
    Auth auth;
    RequestOptions options;

    bool operator==(const Request& o) const {
        return id == o.id && name == o.name && method == o.method && url == o.url &&
               params == o.params && headers == o.headers && bodyType == o.bodyType &&
               body == o.body && form == o.form && auth == o.auth && options == o.options;
    }
    bool operator!=(const Request& o) const { return !(*this == o); }
};

struct Folder {
    std::string id;
    std::string name;
    std::vector<Folder> folders;
    std::vector<Request> requests;
};

struct HistoryEntry {
    Request request;
    long status = 0;      // 0 when no HTTP response arrived
    std::string error;
    std::int64_t sentAt = 0;  // seconds since the Unix epoch
    double totalMs = 0;
};

// Random identifier used for saved requests and folders.
std::string newId();

}  // namespace fetcher
