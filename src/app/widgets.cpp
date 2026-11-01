#include "widgets.h"

#include "fonts.h"

#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/image.h>
#include <wx/popupwin.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/toplevel.h>
#include <wx/utils.h>

#include <algorithm>
#include <cmath>
#include <memory>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>

#include <dwmapi.h>
#endif
#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

wxDEFINE_EVENT(EVT_TAB_CHANGED, wxCommandEvent);

namespace {

constexpr double kPi = 3.14159265358979323846;

void fillRounded(wxGraphicsContext& gc, const wxRect& rect, double radius, const wxColour& colour) {
    gc.SetPen(*wxTRANSPARENT_PEN);
    gc.SetBrush(wxBrush(colour));
    gc.DrawRoundedRectangle(rect.x, rect.y, rect.width, rect.height, radius);
}

void strokeRounded(wxGraphicsContext& gc, const wxRect& rect, double radius, const wxColour& colour, double width) {
    gc.SetBrush(*wxTRANSPARENT_BRUSH);
    gc.SetPen(gc.CreatePen(wxGraphicsPenInfo(colour, width)));
    const double inset = width / 2;
    gc.DrawRoundedRectangle(rect.x + inset, rect.y + inset, rect.width - width, rect.height - width, radius);
}

// Makes a transparent image in one colour at each common display scale. `draw`
// paints white on black, and that becomes the alpha channel, because text drawn
// straight onto a transparent image gets dark fringes.
wxBitmapBundle renderTag(const wxColour& colour, const std::function<void(wxDC&, double)>& draw) {
    wxVector<wxBitmap> bitmaps;
    for (double scale : {1.0, 1.5, 2.0}) {
        wxBitmap mask(static_cast<int>(std::lround(44 * scale)), static_cast<int>(std::lround(16 * scale)), 24);
        {
            wxMemoryDC dc(mask);
            dc.SetBackground(*wxBLACK_BRUSH);
            dc.Clear();
            draw(dc, scale);
        }
        wxImage image = mask.ConvertToImage();
        if (!image.HasAlpha()) image.InitAlpha();
        unsigned char* pixels = image.GetData();
        unsigned char* alpha = image.GetAlpha();
        const std::size_t count = static_cast<std::size_t>(image.GetWidth()) * image.GetHeight();
        for (std::size_t i = 0; i < count; ++i, pixels += 3) {
            alpha[i] = std::max({pixels[0], pixels[1], pixels[2]});
            pixels[0] = colour.Red();
            pixels[1] = colour.Green();
            pixels[2] = colour.Blue();
        }
        bitmaps.push_back(wxBitmap(image));
    }
    return wxBitmapBundle::FromBitmaps(bitmaps);
}

}  // namespace

const char* controlKeyName() {
#ifdef __WXOSX__
    return "Cmd";
#else
    return "Ctrl";
#endif
}

void drawGlyph(wxGraphicsContext& gc, Glyph glyph, double x, double y, double size, const wxColour& colour) {
    const double s = size / 16.0;
    const auto px = [&](double v) { return x + v * s; };
    const auto py = [&](double v) { return y + v * s; };

    gc.SetBrush(*wxTRANSPARENT_BRUSH);
    gc.SetPen(gc.CreatePen(wxGraphicsPenInfo(colour, 1.5 * s).Cap(wxCAP_ROUND).Join(wxJOIN_ROUND)));

    wxGraphicsPath path = gc.CreatePath();
    const auto line = [&](double x1, double y1, double x2, double y2) {
        path.MoveToPoint(px(x1), py(y1));
        path.AddLineToPoint(px(x2), py(y2));
    };
    const auto addFolder = [&] {
        path.MoveToPoint(px(2), py(12.5));
        path.AddLineToPoint(px(2), py(3.5));
        path.AddLineToPoint(px(6.5), py(3.5));
        path.AddLineToPoint(px(8), py(5.5));
        path.AddLineToPoint(px(14), py(5.5));
        path.AddLineToPoint(px(14), py(12.5));
        path.CloseSubpath();
    };

    switch (glyph) {
        case Glyph::None:
            return;
        case Glyph::Close:
            line(4.5, 4.5, 11.5, 11.5);
            line(11.5, 4.5, 4.5, 11.5);
            break;
        case Glyph::Plus:
            line(8, 3.5, 8, 12.5);
            line(3.5, 8, 12.5, 8);
            break;
        case Glyph::Chevron:
            path.MoveToPoint(px(4.5), py(6.5));
            path.AddLineToPoint(px(8), py(10));
            path.AddLineToPoint(px(11.5), py(6.5));
            break;
        case Glyph::Check:
            path.MoveToPoint(px(3.5), py(8.5));
            path.AddLineToPoint(px(6.5), py(11.5));
            path.AddLineToPoint(px(12.5), py(4.5));
            break;
        case Glyph::Search:
            path.AddCircle(px(7), py(7), 4.25 * s);
            line(10.25, 10.25, 13.5, 13.5);
            break;
        case Glyph::Sun:
            path.AddCircle(px(8), py(8), 2.75 * s);
            for (int ray = 0; ray < 8; ++ray) {
                const double angle = ray * kPi / 4;
                line(8 + 5.0 * std::cos(angle), 8 + 5.0 * std::sin(angle),
                     8 + 6.75 * std::cos(angle), 8 + 6.75 * std::sin(angle));
            }
            break;
        case Glyph::Moon:
            // A circle's arc from one horn to the other, then a shallower curve back.
            path.MoveToPoint(px(14), py(8.53));
            path.AddArc(px(8), py(8), 6.02 * s, 5 * kPi / 180, 265 * kPi / 180, true);
            path.AddQuadCurveToPoint(px(5.13), py(10.87), px(14), py(8.53));
            path.CloseSubpath();
            break;
        case Glyph::Folder:
            addFolder();
            break;
        case Glyph::FolderPlus:
            addFolder();
            line(8, 7.25, 8, 11);
            line(6.1, 9.1, 9.9, 9.1);
            break;
    }
    gc.StrokePath(path);
}

wxBitmapBundle methodTagBundle(const std::string& method) {
    wxString label = wxString::FromUTF8(method).Upper();
    if (label == "DELETE") label = "DEL";
    if (label == "OPTIONS") label = "OPT";
    if (label.length() > 5) label = label.Left(5);
    return renderTag(methodColour(method), [&](wxDC& dc, double scale) {
        dc.SetFont(wxFontInfo(wxSize(0, static_cast<int>(std::lround(13 * scale)))).FaceName(uiFont().GetFaceName()).Bold());
        dc.SetTextForeground(*wxWHITE);
        const wxSize text = dc.GetTextExtent(label);
        dc.DrawText(label, 0, (static_cast<int>(16 * scale) - text.y) / 2);
    });
}

wxBitmapBundle folderTagBundle() {
    return renderTag(palette().muted, [](wxDC& dc, double scale) {
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
        if (gc) drawGlyph(*gc, Glyph::Folder, 0, 0, 16 * scale, *wxWHITE);
    });
}

// ---------------------------------------------------------------------------
// FlatControl
// ---------------------------------------------------------------------------

FlatControl::FlatControl(wxWindow* parent, wxWindowID id, const wxString& label) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Create(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS);
    SetLabel(label);

    Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
        dc.Clear();
        {
            // Released before the text is drawn, so text gets native rendering.
            std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
            if (gc) drawShapes(*gc);
        }
        dc.SetFont(GetFont());
        drawText(dc);
    });
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& event) {
        hovered_ = true;
        Refresh();
        event.Skip();
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& event) {
        hovered_ = false;
        Refresh();
        event.Skip();
    });
    const auto press = [this](wxMouseEvent&) {
        pressed_ = true;
        if (!HasCapture()) CaptureMouse();
        Refresh();
    };
    Bind(wxEVT_LEFT_DOWN, press);
    Bind(wxEVT_LEFT_DCLICK, press);
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
        if (!pressed_) return;
        pressed_ = false;
        if (HasCapture()) ReleaseMouse();
        Refresh();
        if (GetClientRect().Contains(event.GetPosition())) activated();
    });
    Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) {
        pressed_ = false;
        Refresh();
    });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        if (handleKey(event.GetKeyCode())) return;
        if (event.GetKeyCode() == WXK_TAB) {
            // wxWANTS_CHARS delivers Tab here, so hand it back for navigation.
            Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
        } else {
            event.Skip();
        }
    });
    Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
        focusRing_ = false;
        Refresh();
        event.Skip();
    });
}

void FlatControl::SetFocusFromKbd() {
    // Clicking a tab can also land here, when the focus moves into the new page.
    focusRing_ = !wxGetMouseState().LeftIsDown();
    wxControl::SetFocusFromKbd();
    Refresh();
}

bool FlatControl::Enable(bool enable) {
    const bool changed = wxControl::Enable(enable);
    Refresh();
    return changed;
}

bool FlatControl::handleKey(int keyCode) {
    if (keyCode != WXK_SPACE && keyCode != WXK_RETURN && keyCode != WXK_NUMPAD_ENTER) return false;
    activated();
    return true;
}

void FlatControl::sendCommand(wxEventType type, int value) {
    wxCommandEvent event(type, GetId());
    event.SetEventObject(this);
    event.SetInt(value);
    ProcessWindowEvent(event);
}

// ---------------------------------------------------------------------------
// FlatButton
// ---------------------------------------------------------------------------

FlatButton::FlatButton(wxWindow* parent, const wxString& label, Kind kind, Glyph glyph, wxWindowID id)
    : FlatControl(parent, id, label), kind_(kind), glyph_(glyph) {
    if (kind == Kind::Primary || kind == Kind::Danger) SetFont(GetFont().Bold());
    SetInitialSize();
}

void FlatButton::setText(const wxString& label) {
    SetLabel(label);
    InvalidateBestSize();
    Refresh();
}

void FlatButton::setGlyph(Glyph glyph) {
    glyph_ = glyph;
    Refresh();
}

int FlatButton::glyphSize() const {
    return FromDIP(16);
}

wxSize FlatButton::DoGetBestClientSize() const {
    const wxSize text = GetLabel().empty() ? wxSize() : GetTextExtent(GetLabel());
    if (GetLabel().empty()) return FromDIP(wxSize(28, 28));
    int width = text.x + FromDIP(24);
    if (glyph_ != Glyph::None) width += glyphSize() + FromDIP(6);
    return wxSize(width, std::max(FromDIP(30), text.y + FromDIP(10)));
}

wxColour FlatButton::contentColour() const {
    const Palette& colours = palette();
    if (!IsEnabled()) return mixColours(colours.background, colours.muted, 0.6);
    switch (kind_) {
        case Kind::Primary:
        case Kind::Danger: return colours.onAccent;
        case Kind::Subtle: return colours.text;
        case Kind::Ghost: return isHovered() ? colours.text : colours.muted;
    }
    return colours.text;
}

void FlatButton::drawShapes(wxGraphicsContext& gc) {
    const Palette& colours = palette();
    const wxRect rect = GetClientRect();
    const double radius = FromDIP(6);

    if (!IsEnabled()) {
        if (kind_ != Kind::Ghost) fillRounded(gc, rect, radius, colours.field);
    } else if (kind_ == Kind::Primary) {
        fillRounded(gc, rect, radius, isPressed() || isHovered() ? colours.accentHover : colours.accent);
    } else if (kind_ == Kind::Danger) {
        fillRounded(gc, rect, radius, isPressed() || isHovered() ? mixColours(colours.dangerFill, colours.onAccent, 0.12)
                                                                 : colours.dangerFill);
    } else if (kind_ == Kind::Subtle || isHovered() || isPressed()) {
        fillRounded(gc, rect, radius, isHovered() || isPressed() ? colours.hover : colours.field);
    }
    if (showsFocusRing()) {
        const bool filled = kind_ == Kind::Primary || kind_ == Kind::Danger;
        strokeRounded(gc, rect, radius, filled ? colours.text : colours.accent, FromDIP(2));
    }

    if (glyph_ != Glyph::None) {
        const double size = glyphSize();
        const double x = GetLabel().empty() ? (rect.width - size) / 2 : FromDIP(10);
        drawGlyph(gc, glyph_, x, (rect.height - size) / 2, size, contentColour());
    }
}

void FlatButton::drawText(wxDC& dc) {
    if (GetLabel().empty()) return;
    const wxRect rect = GetClientRect();
    const wxSize text = dc.GetTextExtent(GetLabel());
    int x = (rect.width - text.x) / 2;
    if (glyph_ != Glyph::None) x = FromDIP(10) + glyphSize() + FromDIP(6);
    dc.SetTextForeground(contentColour());
    dc.DrawText(GetLabel(), x, (rect.height - text.y) / 2);
}

void FlatButton::activated() {
    sendCommand(wxEVT_BUTTON);
}

// ---------------------------------------------------------------------------
// FlatCheck
// ---------------------------------------------------------------------------

FlatCheck::FlatCheck(wxWindow* parent, const wxString& label, wxWindowID id) : FlatControl(parent, id, label) {
    SetInitialSize();
}

void FlatCheck::SetValue(bool value) {
    if (value_ == value) return;
    value_ = value;
    Refresh();
}

int FlatCheck::boxSize() const {
    return FromDIP(16);
}

wxSize FlatCheck::DoGetBestClientSize() const {
    const int box = boxSize();
    if (GetLabel().empty()) return wxSize(box, box);
    const wxSize text = GetTextExtent(GetLabel());
    return wxSize(box + FromDIP(8) + text.x, std::max(box, text.y) + FromDIP(4));
}

void FlatCheck::drawShapes(wxGraphicsContext& gc) {
    const Palette& colours = palette();
    const int box = boxSize();
    const wxRect rect(0, (GetClientSize().y - box) / 2, box, box);
    const double radius = FromDIP(4);

    if (value_) {
        fillRounded(gc, rect, radius, isHovered() ? colours.accentHover : colours.accent);
        drawGlyph(gc, Glyph::Check, rect.x, rect.y, box, colours.onAccent);
    } else {
        strokeRounded(gc, rect, radius, isHovered() ? colours.muted : mixColours(colours.background, colours.muted, 0.6),
                      FromDIP(15) / 10.0);
    }
    if (showsFocusRing()) strokeRounded(gc, rect, radius, colours.text, FromDIP(15) / 10.0);
}

void FlatCheck::drawText(wxDC& dc) {
    if (GetLabel().empty()) return;
    const wxSize text = dc.GetTextExtent(GetLabel());
    dc.SetTextForeground(palette().text);
    dc.DrawText(GetLabel(), boxSize() + FromDIP(8), (GetClientSize().y - text.y) / 2);
}

void FlatCheck::activated() {
    value_ = !value_;
    Refresh();
    sendCommand(wxEVT_CHECKBOX, value_ ? 1 : 0);
}

// ---------------------------------------------------------------------------
// ListPopup
// ---------------------------------------------------------------------------

// A floating list drawn by the app: what a Dropdown opens, and the right-click
// menus. On Windows it never takes the focus, so keys arrive at the window behind
// it; on GTK it holds a grab, so they arrive at the popup itself.
class ListPopup : public wxPopupTransientWindow, public ThemeAware {
public:
    struct Row {
        wxString label;
        wxString shortcut;
        bool divider = false;
    };

    explicit ListPopup(wxWindow* owner);

    void openBelow(wxWindow* control);
    void openAt(const wxPoint& screenPoint);
    bool handleKey(int keyCode);
    void applyTheme() override { Refresh(); }

    std::vector<Row> rows;
    int checked = wxNOT_FOUND;
    bool hasCheckColumn = false;
    int minWidth = 0;
    std::function<wxColour(int)> rowColour;
    std::function<void(int)> onChosen;
    std::function<void()> onClosed;

protected:
    void OnDismiss() override;

private:
    int padding() const { return FromDIP(4); }
    int textStart() const { return FromDIP(hasCheckColumn ? 32 : 12); }
    int rowHeightOf(std::size_t index) const { return rows[index].divider ? FromDIP(9) : rowHeight_; }
    bool isScrollable() const { return contentHeight_ > viewHeight_; }
    wxRect rowRect(std::size_t index) const;
    bool isFullyVisible(const wxRect& row) const;
    int rowAt(const wxPoint& point) const;

    void show(const wxPoint& origin, const wxSize& size);
    void prepare();
    void styleWindowFrame();
    void paint();
    void setHighlight(int index);
    void step(int direction);
    void scrollTo(int offset);
    void ensureVisible(int index);
    void choose(int index);
    void onCharHook(wxKeyEvent& event);

    wxWindow* owner_;
    wxWindow* keyWindow_ = nullptr;
    std::vector<int> rowTops_;
    int rowHeight_ = 0;
    int contentHeight_ = 0;
    int viewHeight_ = 0;
    int scrollY_ = 0;
    int highlight_ = wxNOT_FOUND;
    bool hasWindowFrame_ = false;
};

ListPopup::ListPopup(wxWindow* owner) : wxPopupTransientWindow(owner, wxBORDER_NONE), owner_(owner) {
#ifdef __WXGTK__
    // Without this GTK delivers no keys to the popup while it holds the grab.
    gtk_widget_set_can_focus(m_wxwindow, TRUE);
#endif
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, [this](wxPaintEvent&) { paint(); });
    Bind(wxEVT_MOTION, [this](wxMouseEvent& event) { setHighlight(rowAt(event.GetPosition())); });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { setHighlight(checked); });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
        const int index = rowAt(event.GetPosition());
        if (index != wxNOT_FOUND) choose(index);
    });
    Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& event) {
        const double notches = static_cast<double>(event.GetWheelRotation()) / event.GetWheelDelta();
        scrollTo(scrollY_ - static_cast<int>(std::lround(notches * 3)) * rowHeight_);
        setHighlight(rowAt(event.GetPosition()));
    });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        if (!handleKey(event.GetKeyCode())) event.Skip();
    });
}

void ListPopup::openBelow(wxWindow* control) {
    show(control->ClientToScreen(wxPoint(0, 0)), wxSize(0, control->GetSize().y + FromDIP(4)));
}

void ListPopup::openAt(const wxPoint& screenPoint) {
    show(screenPoint, wxSize(0, 0));
}

void ListPopup::show(const wxPoint& origin, const wxSize& size) {
    prepare();
    Position(origin, size);
    styleWindowFrame();
    keyWindow_ = wxGetTopLevelParent(owner_);
    keyWindow_->Bind(wxEVT_CHAR_HOOK, &ListPopup::onCharHook, this);
    Popup();
    Refresh();
}

void ListPopup::prepare() {
    SetFont(owner_->GetFont());
    rowHeight_ = std::max(FromDIP(30), GetCharHeight() + FromDIP(10));
    rowTops_.clear();
    contentHeight_ = 0;
    int labelWidth = 0;
    int shortcutWidth = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        rowTops_.push_back(contentHeight_);
        contentHeight_ += rowHeightOf(i);
        if (rows[i].divider) continue;
        labelWidth = std::max(labelWidth, GetTextExtent(rows[i].label).x);
        if (!rows[i].shortcut.empty()) shortcutWidth = std::max(shortcutWidth, GetTextExtent(rows[i].shortcut).x + FromDIP(24));
    }
    viewHeight_ = std::min(contentHeight_, rowHeight_ * 12);
    const int room = 2 * padding() + (isScrollable() ? FromDIP(6) : 0);
    const int width = room + textStart() + labelWidth + shortcutWidth + FromDIP(16);
    SetClientSize(std::max(std::min(width, FromDIP(560)), minWidth), viewHeight_ + 2 * padding());
    scrollY_ = 0;
    highlight_ = checked;
    ensureVisible(highlight_);
}

void ListPopup::styleWindowFrame() {
#ifdef __WXMSW__
    // Windows 11 can round a popup and outline it. Older versions refuse, and then
    // paint() draws the outline instead.
    constexpr DWORD kCornerPreference = 33;
    constexpr DWORD kBorderColour = 34;
    constexpr DWORD kRoundSmall = 3;
    const HWND window = static_cast<HWND>(GetHWND());
    const wxColour& border = palette().popupBorder;
    const COLORREF colour = RGB(border.Red(), border.Green(), border.Blue());
    hasWindowFrame_ = SUCCEEDED(DwmSetWindowAttribute(window, kCornerPreference, &kRoundSmall, sizeof(kRoundSmall))) &&
                      SUCCEEDED(DwmSetWindowAttribute(window, kBorderColour, &colour, sizeof(colour)));
#endif
}

wxRect ListPopup::rowRect(std::size_t index) const {
    const int width = GetClientSize().x - 2 * padding() - (isScrollable() ? FromDIP(6) : 0);
    return wxRect(padding(), padding() + rowTops_[index] - scrollY_, width, rowHeightOf(index));
}

bool ListPopup::isFullyVisible(const wxRect& row) const {
    return row.y >= padding() && row.y + row.height <= padding() + viewHeight_;
}

int ListPopup::rowAt(const wxPoint& point) const {
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const wxRect row = rowRect(i);
        if (!rows[i].divider && isFullyVisible(row) && row.Contains(point)) return static_cast<int>(i);
    }
    return wxNOT_FOUND;
}

void ListPopup::paint() {
    const Palette& colours = palette();
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(colours.popup));
    dc.Clear();
    {
        // Released before the text is drawn, so text gets native rendering.
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
        if (gc) {
            for (std::size_t i = 0; i < rows.size(); ++i) {
                const wxRect row = rowRect(i);
                if (rows[i].divider || !isFullyVisible(row)) continue;
                if (static_cast<int>(i) == highlight_) fillRounded(*gc, row, FromDIP(5), colours.hover);
                if (static_cast<int>(i) == checked) {
                    const double size = FromDIP(16);
                    drawGlyph(*gc, Glyph::Check, row.x + FromDIP(8), row.y + (row.height - size) / 2, size, colours.accent);
                }
            }
            if (isScrollable()) {
                const int thumbHeight = viewHeight_ * viewHeight_ / contentHeight_;
                const int thumbTop = padding() + viewHeight_ * scrollY_ / contentHeight_;
                fillRounded(*gc, wxRect(GetClientSize().x - FromDIP(6), thumbTop, FromDIP(3), thumbHeight), FromDIP(1),
                            colours.muted);
            }
        }
    }
    dc.SetFont(GetFont());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const wxRect row = rowRect(i);
        if (!isFullyVisible(row)) continue;
        if (rows[i].divider) {
            const int y = row.y + row.height / 2;
            dc.SetPen(wxPen(colours.popupBorder));
            dc.DrawLine(row.x, y, row.x + row.width, y);
            continue;
        }
        int labelEnd = row.x + row.width - FromDIP(8);
        if (!rows[i].shortcut.empty()) {
            const wxSize shortcut = dc.GetTextExtent(rows[i].shortcut);
            dc.SetTextForeground(colours.muted);
            dc.DrawText(rows[i].shortcut, labelEnd - shortcut.x, row.y + (row.height - shortcut.y) / 2);
            labelEnd -= shortcut.x + FromDIP(24);
        }
        const int labelStart = row.x + textStart();
        const wxString label = wxControl::Ellipsize(rows[i].label, dc, wxELLIPSIZE_END, labelEnd - labelStart);
        dc.SetTextForeground(rowColour ? rowColour(static_cast<int>(i)) : colours.text);
        dc.DrawText(label, labelStart, row.y + (row.height - dc.GetTextExtent(label).y) / 2);
    }
    if (!hasWindowFrame_) {
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.SetPen(wxPen(colours.popupBorder, std::max(1, FromDIP(1))));
        dc.DrawRectangle(GetClientRect());
    }
}

void ListPopup::setHighlight(int index) {
    if (index == highlight_) return;
    highlight_ = index;
    Refresh();
}

void ListPopup::step(int direction) {
    const int count = static_cast<int>(rows.size());
    if (count == 0) return;
    int index = highlight_ == wxNOT_FOUND ? (direction > 0 ? -1 : count) : highlight_;
    for (int tried = 0; tried < count; ++tried) {
        index = (index + direction + count) % count;
        if (rows[static_cast<std::size_t>(index)].divider) continue;
        ensureVisible(index);
        setHighlight(index);
        return;
    }
}

void ListPopup::scrollTo(int offset) {
    const int clamped = std::max(0, std::min(offset, contentHeight_ - viewHeight_));
    if (clamped == scrollY_) return;
    scrollY_ = clamped;
    Refresh();
}

void ListPopup::ensureVisible(int index) {
    if (index == wxNOT_FOUND || static_cast<std::size_t>(index) >= rows.size()) return;
    const std::size_t row = static_cast<std::size_t>(index);
    if (rowTops_[row] < scrollY_) {
        scrollTo(rowTops_[row]);
    } else if (rowTops_[row] + rowHeightOf(row) > scrollY_ + viewHeight_) {
        scrollTo(rowTops_[row] + rowHeightOf(row) - viewHeight_);
    }
}

void ListPopup::choose(int index) {
    // Runs once the popup is gone, since the choice may well open a dialog.
    const auto chosen = onChosen;
    wxWindow* const owner = owner_;
    DismissAndNotify();
    if (chosen) owner->CallAfter([chosen, index] { chosen(index); });
}

bool ListPopup::handleKey(int keyCode) {
    switch (keyCode) {
        case WXK_ESCAPE:
            DismissAndNotify();
            return true;
        case WXK_TAB:
            DismissAndNotify();
            return false;
        case WXK_UP:
        case WXK_NUMPAD_UP:
            step(-1);
            return true;
        case WXK_DOWN:
        case WXK_NUMPAD_DOWN:
            step(1);
            return true;
        case WXK_HOME:
        case WXK_NUMPAD_HOME:
            highlight_ = wxNOT_FOUND;
            step(1);
            return true;
        case WXK_END:
        case WXK_NUMPAD_END:
            highlight_ = wxNOT_FOUND;
            step(-1);
            return true;
        case WXK_RETURN:
        case WXK_NUMPAD_ENTER:
        case WXK_SPACE:
            if (highlight_ != wxNOT_FOUND) {
                choose(highlight_);
            } else {
                DismissAndNotify();
            }
            return true;
        default:
            return true;  // like a native menu, an open list takes every key
    }
}

void ListPopup::onCharHook(wxKeyEvent& event) {
    if (!handleKey(event.GetKeyCode())) event.Skip();
}

void ListPopup::OnDismiss() {
    if (keyWindow_) keyWindow_->Unbind(wxEVT_CHAR_HOOK, &ListPopup::onCharHook, this);
    keyWindow_ = nullptr;
    if (onClosed) onClosed();
}

void showContextMenu(wxWindow* owner, const std::vector<MenuEntry>& entries, const wxPoint& screenPosition,
                     std::function<void(int id)> onChosen) {
    auto* popup = new ListPopup(owner);
    std::vector<int> ids;
    std::vector<ColourRole> roles;
    for (const auto& entry : entries) {
        popup->rows.push_back({entry.label, entry.shortcut, entry.id == wxID_NONE});
        ids.push_back(entry.id);
        roles.push_back(entry.role);
    }
    popup->rowColour = [roles](int index) { return colourFor(roles[static_cast<std::size_t>(index)]); };
    popup->onChosen = [ids, onChosen = std::move(onChosen)](int index) { onChosen(ids[static_cast<std::size_t>(index)]); };
    // Destroy() waits for idle time, so this is safe from inside the dismissal.
    popup->onClosed = [popup] { popup->Destroy(); };
    owner->SetFocus();  // so the keyboard can drive the menu
    popup->openAt(screenPosition);
}

// ---------------------------------------------------------------------------
// Dropdown
// ---------------------------------------------------------------------------

Dropdown::Dropdown(wxWindow* parent, Kind kind, wxWindowID id) : FlatControl(parent, id, wxString()), kind_(kind) {}

void Dropdown::addItem(const wxString& item) {
    items_.push_back(item);
    if (GetLabel().empty()) {
        SetLabel(item);
        selection_ = static_cast<int>(items_.size()) - 1;
    }
    InvalidateBestSize();
    SetInitialSize();
}

void Dropdown::SetSelection(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= items_.size()) return;
    selection_ = index;
    SetLabel(items_[static_cast<std::size_t>(index)]);
    Refresh();
}

void Dropdown::setText(const wxString& text) {
    const auto it = std::find(items_.begin(), items_.end(), text);
    selection_ = it == items_.end() ? wxNOT_FOUND : static_cast<int>(it - items_.begin());
    SetLabel(text);
    Refresh();
}

wxSize Dropdown::DoGetBestClientSize() const {
    int widest = 0;
    int height = 0;
    for (const auto& item : items_) {
        const wxSize size = GetTextExtent(item);
        widest = std::max(widest, size.x);
        height = std::max(height, size.y);
    }
    return wxSize(widest + FromDIP(38), std::max(FromDIP(28), height + FromDIP(8)));
}

void Dropdown::drawShapes(wxGraphicsContext& gc) {
    const Palette& colours = palette();
    const wxRect rect = GetClientRect();
    const double radius = FromDIP(6);
    if (kind_ == Kind::Filled) {
        fillRounded(gc, rect, radius, isHovered() || isPressed() ? colours.hover : colours.field);
    } else if (isHovered() || isPressed()) {
        fillRounded(gc, wxRect(rect).Deflate(FromDIP(3), FromDIP(3)), radius - 1, colours.hover);
    }
    if (showsFocusRing()) strokeRounded(gc, rect, radius, colours.accent, FromDIP(2));
    const double size = FromDIP(14);
    drawGlyph(gc, Glyph::Chevron, rect.width - size - FromDIP(8), (rect.height - size) / 2, size, colours.muted);
}

void Dropdown::drawText(wxDC& dc) {
    const wxString label = wxControl::Ellipsize(GetLabel(), dc, wxELLIPSIZE_END, GetClientSize().x - FromDIP(38));
    dc.SetTextForeground(colouring_ ? colouring_(GetLabel()) : palette().text);
    dc.DrawText(label, FromDIP(10), (GetClientSize().y - dc.GetTextExtent(label).y) / 2);
}

void Dropdown::activated() {
    // Pressing the button while the list is open closes the list, and letting go
    // of the button must not open it straight back up.
    if (std::chrono::steady_clock::now() - popupClosedAt_ < std::chrono::milliseconds(250)) return;
    if (!popup_) {
        popup_ = new ListPopup(this);
        popup_->hasCheckColumn = true;
        popup_->rowColour = [this](int index) {
            return colouring_ ? colouring_(items_[static_cast<std::size_t>(index)]) : palette().text;
        };
        popup_->onChosen = [this](int index) { itemChosen(index); };
        popup_->onClosed = [this] { popupClosed(); };
    }
    popup_->rows.clear();
    for (const auto& item : items_) popup_->rows.push_back({item, wxString(), false});
    popup_->checked = selection_;
    popup_->minWidth = GetSize().x;
    SetFocus();  // so the keyboard can drive the list
    popup_->openBelow(this);
}

void Dropdown::itemChosen(int index) {
    SetSelection(index);
    sendCommand(wxEVT_CHOICE, index);
}

void Dropdown::popupClosed() {
    popupClosedAt_ = std::chrono::steady_clock::now();
    clearHover();
    Refresh();
}

bool Dropdown::handleKey(int keyCode) {
    if (popup_ && popup_->IsShown()) return popup_->handleKey(keyCode);

    const bool up = keyCode == WXK_UP || keyCode == WXK_NUMPAD_UP;
    const bool down = keyCode == WXK_DOWN || keyCode == WXK_NUMPAD_DOWN;
    if (up || down) {
        const int count = steppableCount_ >= 0 ? steppableCount_ : static_cast<int>(items_.size());
        const int current = GetSelection();
        const int next = current == wxNOT_FOUND ? 0 : current + (down ? 1 : -1);
        if (next >= 0 && next < count) {
            SetSelection(next);
            sendCommand(wxEVT_CHOICE, next);
        }
        return true;
    }
    return FlatControl::handleKey(keyCode);
}

// ---------------------------------------------------------------------------
// TabBar
// ---------------------------------------------------------------------------

TabBar::TabBar(wxWindow* parent, wxWindowID id) : FlatControl(parent, id, wxString()) {
    Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
        const int hovered = tabAt(event.GetPosition());
        if (hovered != hoveredTab_) {
            hoveredTab_ = hovered;
            Refresh();
        }
        event.Skip();
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& event) {
        hoveredTab_ = wxNOT_FOUND;
        Refresh();
        event.Skip();
    });
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) { pick(tabAt(event.GetPosition())); });
    Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent& event) { pick(tabAt(event.GetPosition())); });
    Bind(wxEVT_LEFT_UP, [](wxMouseEvent&) {});
}

void TabBar::addTab(const wxString& label) {
    tabs_.push_back({label, wxString()});
    InvalidateBestSize();
    SetInitialSize();
}

void TabBar::setBadge(std::size_t index, const wxString& badge) {
    if (index >= tabs_.size() || tabs_[index].badge == badge) return;
    tabs_[index].badge = badge;
    InvalidateBestSize();
    SetInitialSize();
    if (GetParent()) GetParent()->Layout();
    Refresh();
}

void TabBar::select(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= tabs_.size()) return;
    selection_ = index;
    Refresh();
}

void TabBar::pick(int index) {
    if (index == wxNOT_FOUND || index == selection_) return;
    select(index);
    sendCommand(EVT_TAB_CHANGED, index);
}

std::vector<wxRect> TabBar::tabRects() const {
    std::vector<wxRect> rects;
    const int height = GetClientSize().y;
    int x = FromDIP(4);
    for (const auto& tab : tabs_) {
        int width = GetTextExtent(tab.label).x + FromDIP(16);
        if (!tab.badge.empty()) width += GetTextExtent(tab.badge).x + FromDIP(5);
        rects.emplace_back(x, 0, width, height);
        x += width + FromDIP(2);
    }
    return rects;
}

int TabBar::tabAt(const wxPoint& point) const {
    const auto rects = tabRects();
    for (std::size_t i = 0; i < rects.size(); ++i)
        if (rects[i].Contains(point)) return static_cast<int>(i);
    return wxNOT_FOUND;
}

wxSize TabBar::DoGetBestClientSize() const {
    int width = FromDIP(8);
    for (const auto& tab : tabs_) {
        width += GetTextExtent(tab.label).x + FromDIP(18);
        if (!tab.badge.empty()) width += GetTextExtent(tab.badge).x + FromDIP(5);
    }
    return wxSize(width, FromDIP(36));
}

void TabBar::drawShapes(wxGraphicsContext& gc) {
    const auto rects = tabRects();
    if (static_cast<std::size_t>(selection_) >= rects.size()) return;
    const wxRect& rect = rects[static_cast<std::size_t>(selection_)];
    const int thickness = FromDIP(2);
    fillRounded(gc, wxRect(rect.x + FromDIP(8), rect.GetBottom() + 1 - thickness, rect.width - FromDIP(16), thickness),
                thickness / 2.0, palette().accent);
    if (showsFocusRing()) strokeRounded(gc, wxRect(rect).Deflate(0, FromDIP(5)), FromDIP(4), palette().accent, FromDIP(15) / 10.0);
}

void TabBar::drawText(wxDC& dc) {
    const Palette& colours = palette();
    const auto rects = tabRects();
    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        const bool active = static_cast<int>(i) == selection_ || static_cast<int>(i) == hoveredTab_;
        const wxSize text = dc.GetTextExtent(tabs_[i].label);
        const int x = rects[i].x + FromDIP(8);
        const int y = (rects[i].height - text.y) / 2;
        dc.SetTextForeground(active ? colours.text : colours.muted);
        dc.DrawText(tabs_[i].label, x, y);
        if (!tabs_[i].badge.empty()) {
            dc.SetTextForeground(colours.accent);
            dc.DrawText(tabs_[i].badge, x + text.x + FromDIP(5), y);
        }
    }
}

bool TabBar::handleKey(int keyCode) {
    if ((keyCode == WXK_LEFT || keyCode == WXK_NUMPAD_LEFT) && selection_ > 0) {
        pick(selection_ - 1);
        return true;
    }
    if ((keyCode == WXK_RIGHT || keyCode == WXK_NUMPAD_RIGHT) && static_cast<std::size_t>(selection_) + 1 < tabs_.size()) {
        pick(selection_ + 1);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Separator, TabbedBook
// ---------------------------------------------------------------------------

Separator::Separator(wxWindow* parent, wxOrientation orientation) : wxWindow(parent, wxID_ANY) {
    const int thickness = std::max(1, FromDIP(1));
    SetMinSize(orientation == wxHORIZONTAL ? wxSize(-1, thickness) : wxSize(thickness, -1));
}

void Separator::applyTheme() {
    SetBackgroundColour(palette().border);
}

void PageBook::DoShowPage(wxWindow* page, bool show) {
    if (!show) {
        page->Hide();
        return;
    }
    wxWindow* focused = wxWindow::FindFocus();
    page->Show();
    // The focus only needs a new home if it was on the page that was just hidden.
    const bool focusLost = !focused || !focused->IsShownOnScreen();
    auto* window = dynamic_cast<wxTopLevelWindow*>(wxGetTopLevelParent(page));
    if (focusLost && window && window->IsActive() && page->IsShownOnScreen()) page->SetFocus();
}

TabbedBook::TabbedBook(wxWindow* parent, wxWindowID barId) : wxPanel(parent) {
    bar_ = new TabBar(this, barId);
    book_ = new PageBook(this);

    header_ = new wxBoxSizer(wxHORIZONTAL);
    header_->Add(bar_, wxSizerFlags().Bottom());
    header_->AddStretchSpacer();

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(header_, wxSizerFlags().Expand());
    root->Add(new Separator(this), wxSizerFlags().Expand());
    root->Add(book_, wxSizerFlags(1).Expand());
    SetSizer(root);

    bar_->Bind(EVT_TAB_CHANGED, [this](wxCommandEvent& event) {
        book_->ChangeSelection(static_cast<std::size_t>(event.GetInt()));
    });
}

void TabbedBook::addPage(wxWindow* page, const wxString& label) {
    bar_->addTab(label);
    book_->AddPage(page, label, book_->GetPageCount() == 0);
    Layout();
}

// ---------------------------------------------------------------------------
// Field
// ---------------------------------------------------------------------------

Field::Field(wxWindow* parent, wxWindowID textId, long textStyle, Glyph glyph) : glyph_(glyph) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxCLIP_CHILDREN | wxBORDER_NONE);
    SetMinSize(wxSize(-1, FromDIP(32)));

    text_ = new wxTextCtrl(this, textId, wxString(), wxDefaultPosition, wxDefaultSize, textStyle | wxBORDER_NONE);

    sizer_ = new wxBoxSizer(wxHORIZONTAL);
    sizer_->AddSpacer(glyph == Glyph::None ? FromDIP(10) : FromDIP(32));
    sizer_->Add(text_, wxSizerFlags(1).CenterVertical());
    sizer_->AddSpacer(FromDIP(10));
    SetSizer(sizer_);

    Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
        dc.Clear();
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
        if (!gc) return;
        const Palette& colours = palette();
        const wxRect rect = GetClientRect();
        const double radius = FromDIP(6);
        fillRounded(*gc, rect, radius, colours.field);
        if (text_->HasFocus()) strokeRounded(*gc, rect, radius, colours.accent, FromDIP(15) / 10.0);
        if (glyph_ != Glyph::None) {
            const double size = FromDIP(16);
            drawGlyph(*gc, glyph_, FromDIP(10), (rect.height - size) / 2, size, colours.muted);
        }
    });
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { text_->SetFocus(); });
    const auto focusChanged = [this](wxFocusEvent& event) {
        Refresh();
        event.Skip();
    };
    text_->Bind(wxEVT_SET_FOCUS, focusChanged);
    text_->Bind(wxEVT_KILL_FOCUS, focusChanged);
}

void Field::addLeading(wxWindow* window) {
    auto* divider = new Separator(this, wxVERTICAL);
    // Inset from the edge: a child window paints over the outline drawn by this field.
    sizer_->Insert(0, window, wxSizerFlags().CenterVertical().Border(wxLEFT, FromDIP(3)));
    sizer_->Insert(1, divider, wxSizerFlags().Expand().Border(wxTOP | wxBOTTOM, FromDIP(8)));
    Layout();
}

void Field::applyTheme() {
    // The text box inherits this, which makes it blend into the fill.
    SetBackgroundColour(palette().field);
}

// ---------------------------------------------------------------------------
// Pill, MutedLabel, ThinSplitter
// ---------------------------------------------------------------------------

Pill::Pill(wxWindow* parent) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    SetFont(GetFont().Bold());

    Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        const wxColour behind = GetParent()->GetBackgroundColour();
        dc.SetBackground(wxBrush(behind));
        dc.Clear();
        {
            std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
            if (gc) fillRounded(*gc, GetClientRect(), FromDIP(6), mixColours(behind, colourFor(role_), 0.16));
        }
        dc.SetFont(GetFont());
        dc.SetTextForeground(colourFor(role_));
        const wxSize text = dc.GetTextExtent(text_);
        const wxSize size = GetClientSize();
        dc.DrawText(text_, (size.x - text.x) / 2, (size.y - text.y) / 2);
    });
}

void Pill::setContent(const wxString& text, ColourRole role) {
    text_ = text;
    role_ = role;
    InvalidateBestSize();
    SetInitialSize();
    Refresh();
}

wxSize Pill::DoGetBestClientSize() const {
    const wxSize text = GetTextExtent(text_);
    return wxSize(text.x + FromDIP(16), text.y + FromDIP(6));
}

RoleLabel::RoleLabel(wxWindow* parent, const wxString& text, ColourRole role, long style)
    : wxStaticText(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, style), role_(role) {}

void RoleLabel::setRole(ColourRole role) {
    role_ = role;
    SetForegroundColour(colourFor(role_));
}

void RoleLabel::applyTheme() {
    SetBackgroundColour(GetParent()->GetBackgroundColour());
    SetForegroundColour(colourFor(role_));
}

ThinSplitter::ThinSplitter(wxWindow* parent)
    : wxSplitterWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_NOBORDER) {}

void ThinSplitter::applyTheme() {
    SetBackgroundColour(GetParent()->GetBackgroundColour());
}

void ThinSplitter::DrawSash(wxDC& dc) {
    if (!IsSplit()) return;
    const wxSize size = GetClientSize();
    const int position = GetSashPosition();
    const int sash = GetSashSize();
    const int line = std::max(1, FromDIP(1));
    const bool vertical = GetSplitMode() == wxSPLIT_VERTICAL;

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(GetBackgroundColour()));
    if (vertical) {
        dc.DrawRectangle(position, 0, sash, size.y);
    } else {
        dc.DrawRectangle(0, position, size.x, sash);
    }
    dc.SetBrush(wxBrush(palette().border));
    if (vertical) {
        dc.DrawRectangle(position + (sash - line) / 2, 0, line, size.y);
    } else {
        dc.DrawRectangle(0, position + (sash - line) / 2, size.x, line);
    }
}
