// Conversions between wxWidgets strings and the UTF-8 strings used by the core.
#pragma once

#include <wx/string.h>

#include <filesystem>
#include <string>
#include <string_view>

// The app is the core's only client, so it uses the core's names directly.
namespace fetcher {}
using namespace fetcher;

inline wxString toWx(std::string_view text) {
    return wxString::FromUTF8(text.data(), text.size());
}

inline std::string toUtf8(const wxString& text) {
    return text.utf8_string();
}

inline std::filesystem::path toPath(const wxString& text) {
#ifdef __WINDOWS__
    return std::filesystem::path(text.ToStdWstring());
#else
    return std::filesystem::path(text.utf8_string());
#endif
}
