// Flat, themed controls drawn by the app so they look the same on every
// platform. Text entry, the menu bar and lists stay native.
#pragma once

#include "theme.h"

#include <wx/bmpbndl.h>
#include <wx/control.h>
#include <wx/panel.h>
#include <wx/simplebook.h>
#include <wx/splitter.h>
#include <wx/stattext.h>

#include <chrono>
#include <functional>
#include <string>
#include <vector>

class wxBoxSizer;
class wxGraphicsContext;
class wxTextCtrl;

enum class Glyph { None, Close, Plus, Chevron, Check, Search, Sun, Moon, Folder, FolderPlus };

// Draws a line icon on a 16-unit grid scaled to `size`, with its corner at (x, y).
void drawGlyph(wxGraphicsContext& gc, Glyph glyph, double x, double y, double size, const wxColour& colour);

// A method tag such as "GET" in its colour, for lists and trees.
wxBitmapBundle methodTagBundle(const std::string& method);
// A folder icon on the same canvas as the method tags, so names line up.
wxBitmapBundle folderTagBundle();

// "Ctrl" or, on macOS, "Cmd", for text that mentions a shortcut.
const char* controlKeyName();

// Base for clickable controls: tracks hover and press, handles the keyboard, and
// paints with antialiased shapes first and native text on top.
class FlatControl : public wxControl, public ThemeAware {
public:
    FlatControl(wxWindow* parent, wxWindowID id, const wxString& label);

    void applyTheme() override { Refresh(); }
    bool Enable(bool enable = true) override;
    bool AcceptsFocusFromKeyboard() const override { return keyboardFocusable_ && IsEnabled(); }
    void SetFocusFromKbd() override;
    void setKeyboardFocusable(bool focusable) { keyboardFocusable_ = focusable; }

protected:
    // Only keyboard users need to see where the focus is.
    bool showsFocusRing() const { return focusRing_; }

    virtual void drawShapes(wxGraphicsContext& gc) = 0;
    virtual void drawText(wxDC& dc) = 0;
    virtual void activated() = 0;  // clicked, or Space/Enter when focused
    virtual bool handleKey(int keyCode);

    void sendCommand(wxEventType type, int value = 0);
    void clearHover() { hovered_ = false; }
    bool isHovered() const { return hovered_; }
    bool isPressed() const { return pressed_; }

private:
    bool hovered_ = false;
    bool pressed_ = false;
    bool keyboardFocusable_ = true;
    bool focusRing_ = false;
};

// Sends wxEVT_BUTTON.
class FlatButton : public FlatControl {
public:
    enum class Kind {
        Primary,  // accent fill, for the main action
        Subtle,   // quiet fill
        Ghost,    // no fill until hovered
        Danger,   // red fill, for an action that destroys something
    };

    FlatButton(wxWindow* parent, const wxString& label, Kind kind = Kind::Subtle, Glyph glyph = Glyph::None,
               wxWindowID id = wxID_ANY);

    void setText(const wxString& label);
    void setGlyph(Glyph glyph);
    void click() { activated(); }

protected:
    wxSize DoGetBestClientSize() const override;
    void drawShapes(wxGraphicsContext& gc) override;
    void drawText(wxDC& dc) override;
    void activated() override;

private:
    wxColour contentColour() const;
    int glyphSize() const;

    Kind kind_;
    Glyph glyph_;
};

// A checkbox with an optional label. Sends wxEVT_CHECKBOX.
class FlatCheck : public FlatControl {
public:
    FlatCheck(wxWindow* parent, const wxString& label = wxString(), wxWindowID id = wxID_ANY);

    bool GetValue() const { return value_; }
    void SetValue(bool value);

protected:
    wxSize DoGetBestClientSize() const override;
    void drawShapes(wxGraphicsContext& gc) override;
    void drawText(wxDC& dc) override;
    void activated() override;

private:
    int boxSize() const;

    bool value_ = true;
};

class ListPopup;

// A selector that opens a list of its items. Sends wxEVT_CHOICE.
class Dropdown : public FlatControl {
public:
    enum class Kind { Filled, Bare };

    explicit Dropdown(wxWindow* parent, Kind kind = Kind::Filled, wxWindowID id = wxID_ANY);

    void addItem(const wxString& item);
    // The index of the chosen item, or wxNOT_FOUND when showing other text.
    int GetSelection() const { return selection_; }
    void SetSelection(int index);
    wxString text() const { return GetLabel(); }
    // Shows any text, even one that isn't an item, such as a custom HTTP method.
    void setText(const wxString& text);
    // Colours the current text, for example by HTTP method.
    void setTextColouring(std::function<wxColour(const wxString&)> colouring) { colouring_ = std::move(colouring); }
    // Arrow keys step through the first `count` items only.
    void setSteppableCount(int count) { steppableCount_ = count; }

protected:
    wxSize DoGetBestClientSize() const override;
    void drawShapes(wxGraphicsContext& gc) override;
    void drawText(wxDC& dc) override;
    void activated() override;
    bool handleKey(int keyCode) override;

private:
    void itemChosen(int index);
    void popupClosed();

    Kind kind_;
    std::vector<wxString> items_;
    int selection_ = wxNOT_FOUND;
    std::function<wxColour(const wxString&)> colouring_;
    int steppableCount_ = -1;
    ListPopup* popup_ = nullptr;
    std::chrono::steady_clock::time_point popupClosedAt_;
};

// One row of a right-click menu. An id of wxID_NONE makes it a divider.
struct MenuEntry {
    int id;
    wxString label;
    wxString shortcut;
    ColourRole role = ColourRole::Text;
};

// Opens a menu at a screen position. `onChosen` runs after the menu has closed,
// and not at all if it was dismissed.
void showContextMenu(wxWindow* owner, const std::vector<MenuEntry>& entries, const wxPoint& screenPosition,
                     std::function<void(int id)> onChosen);

// Sent by TabBar when the user picks a tab; GetInt() is its index.
wxDECLARE_EVENT(EVT_TAB_CHANGED, wxCommandEvent);

// A row of text tabs with an underline on the selected one.
class TabBar : public FlatControl {
public:
    explicit TabBar(wxWindow* parent, wxWindowID id = wxID_ANY);

    void addTab(const wxString& label);
    // A short note after the label: a count, or a dot for "in use".
    void setBadge(std::size_t index, const wxString& badge);
    int selection() const { return selection_; }
    void select(int index);

protected:
    wxSize DoGetBestClientSize() const override;
    void drawShapes(wxGraphicsContext& gc) override;
    void drawText(wxDC& dc) override;
    void activated() override {}
    bool handleKey(int keyCode) override;

private:
    struct Tab {
        wxString label;
        wxString badge;
    };
    std::vector<wxRect> tabRects() const;
    int tabAt(const wxPoint& point) const;
    void pick(int index);

    std::vector<Tab> tabs_;
    int selection_ = 0;
    int hoveredTab_ = wxNOT_FOUND;
};

// A hairline between areas.
class Separator : public wxWindow, public ThemeAware {
public:
    Separator(wxWindow* parent, wxOrientation orientation = wxHORIZONTAL);
    void applyTheme() override;
};

// A stack of pages that leaves the keyboard focus alone. wxSimplebook gives every
// page it shows the focus, which would pull the caret out of the URL box whenever
// a response arrives.
class PageBook : public wxSimplebook {
public:
    explicit PageBook(wxWindow* parent) : wxSimplebook(parent) {}

protected:
    void DoShowPage(wxWindow* page, bool show) override;
};

// Pages under a TabBar, with room for extra controls at the right of the tabs.
class TabbedBook : public wxPanel {
public:
    explicit TabbedBook(wxWindow* parent, wxWindowID barId = wxID_ANY);

    wxWindow* pageParent() { return book_; }
    void addPage(wxWindow* page, const wxString& label);
    void setBadge(std::size_t index, const wxString& badge) { bar_->setBadge(index, badge); }
    // Controls in this row should be children of this TabbedBook.
    wxBoxSizer* headerSizer() { return header_; }

private:
    TabBar* bar_;
    PageBook* book_;
    wxBoxSizer* header_;
};

// A filled, rounded text box whose outline lights up with the keyboard focus.
class Field : public wxPanel, public ThemeAware {
public:
    Field(wxWindow* parent, wxWindowID textId = wxID_ANY, long textStyle = 0, Glyph glyph = Glyph::None);

    wxTextCtrl* text() { return text_; }
    // Puts a control inside the box before the text, set off by a hairline.
    void addLeading(wxWindow* window);

    void applyTheme() override;

private:
    wxTextCtrl* text_;
    wxBoxSizer* sizer_;
    Glyph glyph_;
};

// A rounded, tinted label such as a response status.
class Pill : public wxWindow, public ThemeAware {
public:
    explicit Pill(wxWindow* parent);

    void setContent(const wxString& text, ColourRole role);
    void applyTheme() override { Refresh(); }

protected:
    wxSize DoGetBestClientSize() const override;

private:
    wxString text_;
    ColourRole role_ = ColourRole::Muted;
};

// Static text that stays in its role's colour across theme changes.
class RoleLabel : public wxStaticText, public ThemeAware {
public:
    RoleLabel(wxWindow* parent, const wxString& text, ColourRole role, long style = 0);
    void setRole(ColourRole role);
    void applyTheme() override;

private:
    ColourRole role_;
};

// Static text in the secondary colour.
class MutedLabel : public RoleLabel {
public:
    MutedLabel(wxWindow* parent, const wxString& text, long style = 0)
        : RoleLabel(parent, text, ColourRole::Muted, style) {}
};

// A splitter whose sash is a single hairline.
class ThinSplitter : public wxSplitterWindow, public ThemeAware {
public:
    explicit ThinSplitter(wxWindow* parent);

    void applyTheme() override;
    void DrawSash(wxDC& dc) override;
};
