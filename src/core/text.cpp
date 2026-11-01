#include "text.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace fetcher {

namespace {

std::string lowerTrimmed(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace

std::string base64Encode(std::string_view data) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < data.size(); i += 3) {
        const std::size_t remaining = data.size() - i;
        const unsigned a = static_cast<unsigned char>(data[i]);
        const unsigned b = remaining > 1 ? static_cast<unsigned char>(data[i + 1]) : 0;
        const unsigned c = remaining > 2 ? static_cast<unsigned char>(data[i + 2]) : 0;
        out += kAlphabet[a >> 2];
        out += kAlphabet[((a & 0x03) << 4) | (b >> 4)];
        out += remaining > 1 ? kAlphabet[((b & 0x0F) << 2) | (c >> 6)] : '=';
        out += remaining > 2 ? kAlphabet[c & 0x3F] : '=';
    }
    return out;
}

std::optional<std::string> prettyJson(std::string_view text) {
    if (!nlohmann::json::accept(text.begin(), text.end())) return std::nullopt;

    std::string out;
    out.reserve(text.size() + text.size() / 4);
    int depth = 0;
    bool inString = false;
    bool escaped = false;

    const auto newline = [&] {
        out += '\n';
        out.append(static_cast<std::size_t>(depth) * 2, ' ');
    };

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            out += c;
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        switch (c) {
            case ' ':
            case '\t':
            case '\r':
            case '\n':
                break;
            case '"':
                inString = true;
                out += c;
                break;
            case '{':
            case '[': {
                out += c;
                std::size_t next = text.find_first_not_of(" \t\r\n", i + 1);
                if (next != std::string_view::npos && (text[next] == '}' || text[next] == ']')) {
                    out += text[next];  // keep empty containers on one line
                    i = next;
                } else {
                    ++depth;
                    newline();
                }
                break;
            }
            case '}':
            case ']':
                --depth;
                newline();
                out += c;
                break;
            case ',':
                out += c;
                newline();
                break;
            case ':':
                out += ": ";
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

bool isValidUtf8(std::string_view text) {
    std::size_t i = 0;
    const std::size_t size = text.size();
    while (i < size) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t extra = 0;
        unsigned minimum = 0, value = 0;
        if (c < 0x80) {
            ++i;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            extra = 1; minimum = 0x80; value = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2; minimum = 0x800; value = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3; minimum = 0x10000; value = c & 0x07;
        } else {
            return false;
        }
        if (size - i <= extra) return false;
        for (std::size_t k = 1; k <= extra; ++k) {
            const unsigned char next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0) != 0x80) return false;
            value = (value << 6) | (next & 0x3F);
        }
        if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return false;
        i += extra + 1;
    }
    return true;
}

bool looksBinary(std::string_view data) {
    const std::string_view head = data.substr(0, 8192);
    return head.find('\0') != std::string_view::npos;
}

std::string mimeTypeOf(std::string_view contentType) {
    return lowerTrimmed(contentType.substr(0, contentType.find(';')));
}

std::string charsetOf(std::string_view contentType) {
    const std::string lowered = lowerTrimmed(contentType);
    const auto position = lowered.find("charset=");
    if (position == std::string::npos) return {};
    std::string value = lowered.substr(position + 8);
    value = value.substr(0, value.find(';'));
    value.erase(std::remove(value.begin(), value.end(), '"'), value.end());
    return lowerTrimmed(value);
}

const char* statusReason(long status) {
    switch (status) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 203: return "Non-Authoritative Information";
        case 204: return "No Content";
        case 205: return "Reset Content";
        case 206: return "Partial Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 402: return "Payment Required";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 406: return "Not Acceptable";
        case 407: return "Proxy Authentication Required";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 412: return "Precondition Failed";
        case 413: return "Content Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 416: return "Range Not Satisfiable";
        case 418: return "I'm a Teapot";
        case 422: return "Unprocessable Content";
        case 425: return "Too Early";
        case 426: return "Upgrade Required";
        case 428: return "Precondition Required";
        case 429: return "Too Many Requests";
        case 431: return "Request Header Fields Too Large";
        case 451: return "Unavailable For Legal Reasons";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        default: return "";
    }
}

std::string formatBytes(std::size_t bytes) {
    char buffer[32];
    if (bytes < 1024) {
        std::snprintf(buffer, sizeof(buffer), "%zu B", bytes);
    } else if (bytes < 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    return buffer;
}

std::string formatDuration(double milliseconds) {
    char buffer[32];
    if (milliseconds < 1000.0) {
        std::snprintf(buffer, sizeof(buffer), "%.0f ms", milliseconds);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f s", milliseconds / 1000.0);
    }
    return buffer;
}

}  // namespace fetcher
