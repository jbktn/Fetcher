#include "serialize.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

using nlohmann::json;
using nlohmann::ordered_json;

namespace fetcher {

namespace {

constexpr int kFormatVersion = 1;

const char* toString(BodyType type) {
    switch (type) {
        case BodyType::Json: return "json";
        case BodyType::Xml: return "xml";
        case BodyType::Text: return "text";
        case BodyType::Form: return "form";
        case BodyType::None: break;
    }
    return "none";
}

BodyType bodyTypeFromString(const std::string& text) {
    if (text == "json") return BodyType::Json;
    if (text == "xml") return BodyType::Xml;
    if (text == "text") return BodyType::Text;
    if (text == "form") return BodyType::Form;
    return BodyType::None;
}

const char* toString(AuthType type) {
    switch (type) {
        case AuthType::Bearer: return "bearer";
        case AuthType::Basic: return "basic";
        case AuthType::ApiKey: return "apiKey";
        case AuthType::None: break;
    }
    return "none";
}

AuthType authTypeFromString(const std::string& text) {
    if (text == "bearer") return AuthType::Bearer;
    if (text == "basic") return AuthType::Basic;
    if (text == "apiKey") return AuthType::ApiKey;
    return AuthType::None;
}

ordered_json rowsToJson(const std::vector<KeyValue>& rows) {
    ordered_json out = ordered_json::array();
    for (const auto& row : rows)
        out.push_back({{"key", row.key}, {"value", row.value}, {"enabled", row.enabled}});
    return out;
}

std::vector<KeyValue> rowsFromJson(const ordered_json& parent, const char* name) {
    std::vector<KeyValue> rows;
    if (!parent.contains(name)) return rows;
    for (const auto& item : parent.at(name)) {
        KeyValue row;
        row.key = item.value("key", std::string());
        row.value = item.value("value", std::string());
        row.enabled = item.value("enabled", true);
        rows.push_back(std::move(row));
    }
    return rows;
}

ordered_json requestToJson(const Request& request) {
    ordered_json out;
    out["id"] = request.id;
    out["name"] = request.name;
    out["method"] = request.method;
    out["url"] = request.url;
    out["params"] = rowsToJson(request.params);
    out["headers"] = rowsToJson(request.headers);
    out["bodyType"] = toString(request.bodyType);
    out["body"] = request.body;
    out["form"] = rowsToJson(request.form);
    out["auth"] = {
        {"type", toString(request.auth.type)},
        {"token", request.auth.token},
        {"username", request.auth.username},
        {"password", request.auth.password},
        {"apiKeyName", request.auth.apiKeyName},
        {"apiKeyValue", request.auth.apiKeyValue},
        {"apiKeyInQuery", request.auth.apiKeyInQuery},
    };
    out["options"] = {
        {"followRedirects", request.options.followRedirects},
        {"verifyTls", request.options.verifyTls},
        {"timeoutSeconds", request.options.timeoutSeconds},
    };
    return out;
}

Request requestFromJson(const ordered_json& in) {
    Request request;
    request.id = in.value("id", std::string());
    request.name = in.value("name", std::string());
    request.method = in.value("method", std::string("GET"));
    request.url = in.value("url", std::string());
    request.params = rowsFromJson(in, "params");
    request.headers = rowsFromJson(in, "headers");
    request.bodyType = bodyTypeFromString(in.value("bodyType", std::string()));
    request.body = in.value("body", std::string());
    request.form = rowsFromJson(in, "form");
    if (in.contains("auth")) {
        const auto& auth = in.at("auth");
        request.auth.type = authTypeFromString(auth.value("type", std::string()));
        request.auth.token = auth.value("token", std::string());
        request.auth.username = auth.value("username", std::string());
        request.auth.password = auth.value("password", std::string());
        request.auth.apiKeyName = auth.value("apiKeyName", std::string());
        request.auth.apiKeyValue = auth.value("apiKeyValue", std::string());
        request.auth.apiKeyInQuery = auth.value("apiKeyInQuery", false);
    }
    if (in.contains("options")) {
        const auto& options = in.at("options");
        request.options.followRedirects = options.value("followRedirects", true);
        request.options.verifyTls = options.value("verifyTls", true);
        request.options.timeoutSeconds = options.value("timeoutSeconds", 30);
    }
    return request;
}

ordered_json folderToJsonValue(const Folder& folder) {
    ordered_json out;
    out["id"] = folder.id;
    out["name"] = folder.name;
    out["folders"] = ordered_json::array();
    for (const auto& child : folder.folders) out["folders"].push_back(folderToJsonValue(child));
    out["requests"] = ordered_json::array();
    for (const auto& request : folder.requests) out["requests"].push_back(requestToJson(request));
    return out;
}

Folder folderFromJsonValue(const ordered_json& in) {
    Folder folder;
    folder.id = in.value("id", std::string());
    folder.name = in.value("name", std::string());
    if (in.contains("folders"))
        for (const auto& child : in.at("folders")) folder.folders.push_back(folderFromJsonValue(child));
    if (in.contains("requests"))
        for (const auto& request : in.at("requests")) folder.requests.push_back(requestFromJson(request));
    return folder;
}

std::string dump(const ordered_json& value) {
    return value.dump(2, ' ', false, ordered_json::error_handler_t::replace) + "\n";
}

ordered_json parseVersioned(const std::string& text) {
    ordered_json doc;
    try {
        doc = ordered_json::parse(text);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("not valid JSON: ") + e.what());
    }
    if (!doc.is_object()) throw std::runtime_error("expected a JSON object");
    int version = doc.value("version", 0);
    if (version < 1 || version > kFormatVersion)
        throw std::runtime_error("unsupported file version " + std::to_string(version));
    return doc;
}

template <typename Fn>
auto withFieldErrors(Fn&& fn) {
    try {
        return fn();
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("unexpected file contents: ") + e.what());
    }
}

}  // namespace

std::string folderToJson(const Folder& root) {
    ordered_json doc;
    doc["version"] = kFormatVersion;
    doc["root"] = folderToJsonValue(root);
    return dump(doc);
}

Folder folderFromJson(const std::string& text) {
    const ordered_json doc = parseVersioned(text);
    return withFieldErrors([&] { return folderFromJsonValue(doc.at("root")); });
}

std::string historyToJson(const std::vector<HistoryEntry>& entries) {
    ordered_json doc;
    doc["version"] = kFormatVersion;
    doc["entries"] = ordered_json::array();
    for (const auto& entry : entries) {
        doc["entries"].push_back({
            {"request", requestToJson(entry.request)},
            {"status", entry.status},
            {"error", entry.error},
            {"sentAt", entry.sentAt},
            {"totalMs", entry.totalMs},
        });
    }
    return dump(doc);
}

std::vector<HistoryEntry> historyFromJson(const std::string& text) {
    const ordered_json doc = parseVersioned(text);
    return withFieldErrors([&] {
        std::vector<HistoryEntry> entries;
        for (const auto& item : doc.at("entries")) {
            HistoryEntry entry;
            entry.request = requestFromJson(item.at("request"));
            entry.status = item.value("status", 0L);
            entry.error = item.value("error", std::string());
            entry.sentAt = item.value("sentAt", std::int64_t{0});
            entry.totalMs = item.value("totalMs", 0.0);
            entries.push_back(std::move(entry));
        }
        return entries;
    });
}

}  // namespace fetcher
