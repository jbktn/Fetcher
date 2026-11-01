#include "text.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <vector>

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

bool isMarkupSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

std::string_view trimMarkupSpace(std::string_view text) {
    while (!text.empty() && isMarkupSpace(text.front())) text.remove_prefix(1);
    while (!text.empty() && isMarkupSpace(text.back())) text.remove_suffix(1);
    return text;
}

bool isNameStart(char c) {
    const auto byte = static_cast<unsigned char>(c);
    return std::isalpha(byte) || c == '_' || c == ':' || byte >= 0x80;
}

bool isNameChar(char c) {
    return isNameStart(c) || std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '.';
}

bool startsWith(std::string_view text, std::size_t at, std::string_view prefix) {
    return text.compare(at, prefix.size(), prefix) == 0;
}

// Lays out one document. Runs in linear time, since a response can be large and
// its contents are out of anyone's control.
class MarkupPrinter {
public:
    MarkupPrinter(std::string_view text, bool html) : text_(text), html_(html) {}

    std::string print() {
        while (pos_ < text_.size()) {
            if (text_[pos_] != '<' || !printMarkup()) printText();
        }
        if (!out_.empty()) out_.pop_back();  // the last line's newline
        return std::move(out_);
    }

private:
    // Past this, more indent only pushes the text out of sight. It also stops
    // deeply nested input from producing output quadratic in its size.
    static constexpr std::size_t kMaxIndent = 40;
    static constexpr std::size_t kMaxInlineLength = 120;

    void startLine(std::size_t depth) { out_.append(std::min(depth, kMaxIndent) * 2, ' '); }

    void line(std::string_view content, std::size_t depth) {
        startLine(depth);
        out_ += content;
        out_ += '\n';
    }

    // Each non-blank line of `content`, indented to `depth` with its own edges trimmed.
    void lines(std::string_view content, std::size_t depth) {
        while (!content.empty()) {
            const std::size_t end = content.find('\n');
            const std::string_view part = trimMarkupSpace(content.substr(0, end));
            if (!part.empty()) line(part, depth);
            if (end == std::string_view::npos) break;
            content.remove_prefix(end + 1);
        }
    }

    void printText() {
        const std::size_t end = text_.find('<', pos_ + 1);
        lines(text_.substr(pos_, end == std::string_view::npos ? end : end - pos_), stack_.size());
        pos_ = end == std::string_view::npos ? text_.size() : end;
    }

    // Prints what starts at the '<' at pos_, or returns false if that isn't markup
    // after all, as in "a < b" or a tag that never ends.
    bool printMarkup() {
        if (startsWith(text_, pos_, "<!--")) {
            printDelimited("-->", false);
        } else if (startsWith(text_, pos_, "<![CDATA[")) {
            printDelimited("]]>", true);
        } else if (startsWith(text_, pos_, "<!") || startsWith(text_, pos_, "<?")) {
            const std::size_t end = tagEnd(pos_);
            if (end == std::string_view::npos) return false;
            line(tidyTag(text_.substr(pos_, end - pos_)), stack_.size());
            pos_ = end;
        } else if (startsWith(text_, pos_, "</") && pos_ + 2 < text_.size() && isNameStart(text_[pos_ + 2])) {
            return printClosingTag();
        } else if (pos_ + 1 < text_.size() && isNameStart(text_[pos_ + 1])) {
            return printOpeningTag();
        } else {
            return false;
        }
        return true;
    }

    void printDelimited(std::string_view closer, bool verbatim) {
        const std::size_t found = text_.find(closer, pos_);
        const std::size_t end = found == std::string_view::npos ? text_.size() : found + closer.size();
        const std::string_view whole = text_.substr(pos_, end - pos_);
        if (verbatim) {
            line(whole, stack_.size());
        } else {
            lines(whole, stack_.size());
        }
        pos_ = end;
    }

    // Just past the '>' that ends the tag at `start`, ignoring any inside quotes.
    std::size_t tagEnd(std::size_t start) const {
        char quote = 0;
        for (std::size_t i = start; i < text_.size(); ++i) {
            const char c = text_[i];
            if (quote) {
                if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '>') {
                return i + 1;
            }
        }
        return std::string_view::npos;
    }

    // A tag on one line, with runs of space outside quotes reduced to one.
    static std::string tidyTag(std::string_view tag) {
        std::string out;
        out.reserve(tag.size());
        char quote = 0;
        bool pendingSpace = false;
        for (const char c : tag) {
            if (quote) {
                out += c;
                if (c == quote) quote = 0;
            } else if (isMarkupSpace(c)) {
                pendingSpace = true;
            } else {
                if (pendingSpace && c != '>') out += ' ';
                pendingSpace = false;
                out += c;
                if (c == '"' || c == '\'') quote = c;
            }
        }
        return out;
    }

    std::string nameAt(std::size_t start) const {
        std::size_t end = start;
        while (end < text_.size() && isNameChar(text_[end])) ++end;
        std::string name(text_.substr(start, end - start));
        return html_ ? lowerTrimmed(name) : name;
    }

    static bool isIn(const std::string& name, std::initializer_list<std::string_view> names) {
        return std::find(names.begin(), names.end(), name) != names.end();
    }

    static bool isVoidElement(const std::string& name) {
        return isIn(name, {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param",
                           "source", "track", "wbr"});
    }

    // Whether opening `name` ends an open `current`, whose end tag HTML lets you leave out.
    static bool closesImplicitly(const std::string& current, const std::string& name) {
        if (current == name) return isIn(name, {"li", "p", "tr", "td", "th", "dt", "dd", "option"});
        return (isIn(current, {"td", "th"}) && isIn(name, {"td", "th"})) ||
               (isIn(current, {"dt", "dd"}) && isIn(name, {"dt", "dd"})) ||
               (isIn(current, {"thead", "tbody", "tfoot"}) && isIn(name, {"thead", "tbody", "tfoot"}));
    }

    void push(const std::string& name) {
        stack_.push_back(name);
        ++openCounts_[name];
    }

    void pop() {
        --openCounts_[stack_.back()];
        stack_.pop_back();
    }

    bool printClosingTag() {
        const std::size_t end = tagEnd(pos_);
        if (end == std::string_view::npos) return false;
        const std::string name = nameAt(pos_ + 2);
        // A stray end tag matches nothing, so it leaves the nesting alone.
        const auto open = openCounts_.find(name);
        if (open != openCounts_.end() && open->second > 0) {
            while (stack_.back() != name) pop();
            pop();
        }
        line(tidyTag(text_.substr(pos_, end - pos_)), stack_.size());
        pos_ = end;
        return true;
    }

    bool printOpeningTag() {
        const std::size_t end = tagEnd(pos_);
        if (end == std::string_view::npos) return false;
        const std::string name = nameAt(pos_ + 1);
        const std::string tag = tidyTag(text_.substr(pos_, end - pos_));
        const bool selfClosed = end >= 2 && text_[end - 2] == '/';

        if (html_) {
            if (!stack_.empty() && closesImplicitly(stack_.back(), name)) pop();
            if (!selfClosed && isIn(name, {"script", "style", "pre", "textarea"})) {
                pos_ = end;
                printRawElement(name, tag);
                return true;
            }
        }
        pos_ = end;
        if (selfClosed || (html_ && isVoidElement(name))) {
            line(tag, stack_.size());
        } else if (!printInlineElement(name, tag)) {
            line(tag, stack_.size());
            push(name);
        }
        return true;
    }

    // The closing tag of `name` at `at`, if that's what is there.
    std::size_t closingTagEnd(const std::string& name, std::size_t at) const {
        if (!startsWith(text_, at, "</") || nameAt(at + 2) != name) return std::string_view::npos;
        return tagEnd(at);
    }

    // <title>Short text</title> reads best on one line.
    bool printInlineElement(const std::string& name, const std::string& tag) {
        const std::size_t next = text_.find('<', pos_);
        if (next == std::string_view::npos) return false;
        const std::string_view content = trimMarkupSpace(text_.substr(pos_, next - pos_));
        if (content.size() > kMaxInlineLength || content.find('\n') != std::string_view::npos) return false;
        const std::size_t end = closingTagEnd(name, next);
        if (end == std::string_view::npos) return false;
        startLine(stack_.size());
        out_ += tag;
        out_ += content;
        out_ += tidyTag(text_.substr(next, end - next));
        out_ += '\n';
        pos_ = end;
        return true;
    }

    // Finds the end tag of a script or similar, whatever case it is in.
    std::size_t findRawClosingTag(const std::string& name) const {
        for (std::size_t at = text_.find("</", pos_); at != std::string_view::npos; at = text_.find("</", at + 2)) {
            if (nameAt(at + 2) == name) return at;
        }
        return std::string_view::npos;
    }

    void printRawElement(const std::string& name, const std::string& tag) {
        const std::size_t closer = findRawClosingTag(name);
        const std::size_t contentEnd = closer == std::string_view::npos ? text_.size() : closer;
        const std::string_view content = text_.substr(pos_, contentEnd - pos_);
        const std::size_t tagStop = closer == std::string_view::npos ? closer : tagEnd(closer);
        const std::string closing =
            tagStop == std::string_view::npos ? std::string() : tidyTag(text_.substr(closer, tagStop - closer));

        if (name == "pre" || name == "textarea") {
            // Whitespace is part of the content here.
            startLine(stack_.size());
            out_ += tag;
            out_ += content;
            out_ += closing;
            out_ += '\n';
        } else if (trimMarkupSpace(content).empty()) {
            line(tag + closing, stack_.size());
        } else {
            line(tag, stack_.size());
            lines(content, stack_.size() + 1);
            if (!closing.empty()) line(closing, stack_.size());
        }
        pos_ = tagStop == std::string_view::npos ? text_.size() : tagStop;
    }

    std::string_view text_;
    bool html_;
    std::size_t pos_ = 0;
    std::string out_;
    std::vector<std::string> stack_;
    std::map<std::string, std::size_t> openCounts_;
};

}  // namespace

std::string prettyMarkup(std::string_view text, bool html) {
    return MarkupPrinter(text, html).print();
}

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
