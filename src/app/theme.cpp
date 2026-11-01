#include "theme.h"

#include <wx/app.h>
#include <wx/config.h>
#include <wx/settings.h>
#include <wx/toplevel.h>
#include <wx/window.h>

#include <algorithm>
#include <cctype>

#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

namespace {

constexpr const char* kConfigKey = "/Window/Theme";

ThemeMode currentMode = ThemeMode::System;
bool currentDark = false;

const Palette kLight = {
    wxColour(0xFF, 0xFF, 0xFF),  // background
    wxColour(0xF7, 0xF7, 0xF8),  // sidebar
    wxColour(0xF1, 0xF1, 0xF3),  // field
    wxColour(0xE8, 0xE8, 0xEB),  // hover
    wxColour(0xE4, 0xE4, 0xE7),  // border
    wxColour(0xFF, 0xFF, 0xFF),  // popup
    wxColour(0xD4, 0xD4, 0xD9),  // popupBorder
    wxColour(0x18, 0x18, 0x1B),  // text
    wxColour(0x6B, 0x6B, 0x75),  // muted
    wxColour(0x4F, 0x6B, 0xF5),  // accent
    wxColour(0x3F, 0x5A, 0xE0),  // accentHover
    wxColour(0xFF, 0xFF, 0xFF),  // onAccent
    wxColour(0x15, 0x80, 0x3D),  // success
    wxColour(0xB4, 0x53, 0x09),  // warning
    wxColour(0xDC, 0x26, 0x26),  // danger
    wxColour(0xDC, 0x26, 0x26),  // dangerFill
    wxColour(0x3B, 0x4F, 0xD8),  // synProperty
    wxColour(0x15, 0x80, 0x3D),  // synString
    wxColour(0xB4, 0x53, 0x09),  // synNumber
    wxColour(0x7C, 0x3A, 0xED),  // synKeyword
    wxColour(0x6B, 0x6B, 0x75),  // synPunctuation
    wxColour(0x9C, 0x9C, 0xA6),  // synComment
};

const Palette kDark = {
    wxColour(0x18, 0x18, 0x1B),  // background
    wxColour(0x12, 0x12, 0x15),  // sidebar
    wxColour(0x26, 0x26, 0x2A),  // field
    wxColour(0x30, 0x30, 0x35),  // hover
    wxColour(0x2B, 0x2B, 0x30),  // border
    wxColour(0x22, 0x22, 0x26),  // popup
    wxColour(0x3A, 0x3A, 0x41),  // popupBorder
    wxColour(0xF4, 0xF4, 0xF5),  // text
    wxColour(0xA1, 0xA1, 0xAA),  // muted
    wxColour(0x5B, 0x74, 0xF6),  // accent
    wxColour(0x6F, 0x86, 0xFF),  // accentHover
    wxColour(0xFF, 0xFF, 0xFF),  // onAccent
    wxColour(0x4A, 0xDE, 0x80),  // success
    wxColour(0xFB, 0xBF, 0x24),  // warning
    wxColour(0xF8, 0x71, 0x71),  // danger
    wxColour(0xDC, 0x26, 0x26),  // dangerFill
    wxColour(0x93, 0xB4, 0xFF),  // synProperty
    wxColour(0x86, 0xEF, 0xAC),  // synString
    wxColour(0xFD, 0xBA, 0x74),  // synNumber
    wxColour(0xC4, 0xB5, 0xFD),  // synKeyword
    wxColour(0xA1, 0xA1, 0xAA),  // synPunctuation
    wxColour(0x71, 0x71, 0x7A),  // synComment
};

bool resolveDark(ThemeMode mode) {
    switch (mode) {
        case ThemeMode::Light: return false;
        case ThemeMode::Dark: return true;
        case ThemeMode::System: break;
    }
    return wxSystemSettings::GetAppearance().IsDark();
}

void applyAppearance(ThemeMode mode) {
    if (!wxTheApp) return;
    switch (mode) {
        case ThemeMode::Light: wxTheApp->SetAppearance(wxApp::Appearance::Light); break;
        case ThemeMode::Dark: wxTheApp->SetAppearance(wxApp::Appearance::Dark); break;
        case ThemeMode::System: wxTheApp->SetAppearance(wxApp::Appearance::System); break;
    }
}

}  // namespace

const Palette& palette() {
    return currentDark ? kDark : kLight;
}

wxColour colourFor(ColourRole role) {
    const Palette& colours = palette();
    switch (role) {
        case ColourRole::Text: return colours.text;
        case ColourRole::Muted: return colours.muted;
        case ColourRole::Success: return colours.success;
        case ColourRole::Warning: return colours.warning;
        case ColourRole::Danger: return colours.danger;
    }
    return colours.text;
}

bool isDarkTheme() {
    return currentDark;
}

ThemeMode themeMode() {
    return currentMode;
}

void initTheme() {
    const wxString saved = wxConfigBase::Get()->Read(kConfigKey, "system");
    currentMode = saved == "light" ? ThemeMode::Light : saved == "dark" ? ThemeMode::Dark : ThemeMode::System;
    applyAppearance(currentMode);
    currentDark = resolveDark(currentMode);

#ifdef __WXGTK__
    // Borderless text boxes sit inside the app's own rounded fields, so GTK's
    // focus outline would be one frame too many. Framed boxes in dialogs keep theirs.
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, "entry.flat, entry.flat:focus { border: none; outline: none; box-shadow: none; }",
                                    -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
#endif
}

void setThemeMode(ThemeMode mode, wxWindow* root) {
    currentMode = mode;
    wxConfigBase::Get()->Write(kConfigKey, mode == ThemeMode::Light  ? "light"
                                           : mode == ThemeMode::Dark ? "dark"
                                                                     : "system");
    applyAppearance(mode);
    refreshTheme(root);
}

void refreshTheme(wxWindow* root) {
    currentDark = resolveDark(currentMode);
    if (root) applyThemeTo(root);
}

wxColour methodColour(const std::string& method) {
    std::string upper = method;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    const Palette& colours = palette();
    if (upper == "GET") return colours.success;
    if (upper == "POST") return colours.warning;
    if (upper == "PUT") return colours.synProperty;
    if (upper == "PATCH") return colours.synKeyword;
    if (upper == "DELETE") return colours.danger;
    return colours.muted;
}

wxColour mixColours(const wxColour& under, const wxColour& over, double amount) {
    const auto mix = [amount](unsigned char a, unsigned char b) {
        return static_cast<unsigned char>(a + (b - a) * amount + 0.5);
    };
    return wxColour(mix(under.Red(), over.Red()), mix(under.Green(), over.Green()),
                    mix(under.Blue(), over.Blue()));
}

void applyThemeTo(wxWindow* window) {
    if (auto* aware = dynamic_cast<ThemeAware*>(window)) {
        aware->applyTheme();
    } else if (window->IsTopLevel()) {
        window->SetBackgroundColour(palette().background);
    } else {
        // Containers, labels, text boxes and lists take after what they sit on.
        window->SetBackgroundColour(window->GetParent()->GetBackgroundColour());
        window->SetForegroundColour(palette().text);
    }
    for (wxWindow* child : window->GetChildren()) applyThemeTo(child);
    window->Refresh();
}
