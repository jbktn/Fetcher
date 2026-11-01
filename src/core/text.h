// Small text utilities with no dependencies on the rest of the app.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace fetcher {

std::string base64Encode(std::string_view data);

// Re-indents valid JSON, or returns nothing if the text is not valid JSON.
// Works on the text itself rather than a parsed value, so key order and number
// formatting are kept exactly: 1.10 stays 1.10 and huge integers stay intact.
std::optional<std::string> prettyJson(std::string_view text);

bool isValidUtf8(std::string_view text);

// A quick check for content that would not display as text.
bool looksBinary(std::string_view data);

// "application/json; charset=utf-8" -> "application/json" (lower case).
std::string mimeTypeOf(std::string_view contentType);
// "application/json; charset=utf-8" -> "utf-8" (lower case), or empty.
std::string charsetOf(std::string_view contentType);

// "OK" for 200 and so on, or an empty string for unknown codes.
const char* statusReason(long status);

std::string formatBytes(std::size_t bytes);
std::string formatDuration(double milliseconds);

}  // namespace fetcher
