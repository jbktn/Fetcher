// The app's colours and the Light / Dark / System switch.
#pragma once

#include <wx/colour.h>

#include <string>

class wxWindow;

enum class ThemeMode { System, Light, Dark };

struct Palette {
    wxColour background;  // the main content area
    wxColour sidebar;
    wxColour field;       // filled inputs and subtle buttons
    wxColour hover;       // anything under the pointer
    wxColour border;      // hairlines between areas
    wxColour popup;       // lists floating above everything else
    wxColour popupBorder;
    wxColour text;
    wxColour muted;       // secondary text and icons
    wxColour accent;
    wxColour accentHover;
    wxColour onAccent;    // text on top of the accent colour
    wxColour success;
    wxColour warning;
    wxColour danger;
    wxColour dangerFill;  // behind onAccent text, for a destructive button

    // Syntax colouring in the code editors.
    wxColour synProperty;
    wxColour synString;
    wxColour synNumber;
    wxColour synKeyword;
    wxColour synPunctuation;
    wxColour synComment;
};

// What a colour means. Controls that outlive a theme change keep one of these
// rather than a colour, which would go stale.
enum class ColourRole { Text, Muted, Success, Warning, Danger };

const Palette& palette();
wxColour colourFor(ColourRole role);
bool isDarkTheme();
ThemeMode themeMode();

// Restores the saved choice. Call once at startup, before creating any window.
void initTheme();
// Changes and remembers the mode, and recolours `root` and everything in it.
void setThemeMode(ThemeMode mode, wxWindow* root);
// For when the system's appearance changed while the app is following it.
void refreshTheme(wxWindow* root);

wxColour methodColour(const std::string& method);
// `amount` of `over` on top of `under`.
wxColour mixColours(const wxColour& under, const wxColour& over, double amount);

// Windows that need more than "same background as my parent" implement this.
class ThemeAware {
public:
    virtual ~ThemeAware() = default;
    virtual void applyTheme() = 0;
};

// Recolours a window and all its children. Parents are done first, so children
// can take their background from them.
void applyThemeTo(wxWindow* window);
